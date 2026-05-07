#include "chat/chat_service_compactor.hpp"

#include "chat/chat_service_history.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace yac::chat::internal {

namespace {

std::size_t CountNonSystem(const std::vector<ChatMessage>& history) {
  return static_cast<std::size_t>(
      std::ranges::count_if(history, [](const ChatMessage& message) {
        return message.role != ChatRole::System;
      }));
}

CompactionOutcome RunTruncate(std::vector<ChatMessage>& history,
                              std::mutex& history_mutex,
                              const ChatConfig& config) {
  CompactionOutcome outcome;
  std::scoped_lock lock(history_mutex);
  const auto before = CountNonSystem(history);
  const auto keep_last =
      static_cast<std::size_t>(config.auto_compact_keep_last);
  if (before <= keep_last) {
    return outcome;
  }
  CompactHistory(history, keep_last);
  outcome.performed = true;
  outcome.messages_removed = before - keep_last;
  return outcome;
}

std::string_view RoleLabel(ChatRole role) {
  switch (role) {
    case ChatRole::System:
      return "System";
    case ChatRole::User:
      return "User";
    case ChatRole::Assistant:
      return "Assistant";
    case ChatRole::Tool:
      return "Tool";
  }
  return "Message";
}

std::string RenderSliceForSummary(const std::vector<ChatMessage>& slice) {
  std::string text;
  text.reserve(slice.size() * 64);
  for (const auto& message : slice) {
    text.append(RoleLabel(message.role));
    text.append(": ");
    text.append(message.content);
    if (!message.tool_name.empty()) {
      text.append(" [tool=");
      text.append(message.tool_name);
      text.append("]");
    }
    text.append("\n");
  }
  return text;
}

constexpr std::string_view kSummarySystemPrompt =
    "You compact prior conversation into a faithful, concise summary. "
    "Preserve user intent, decisions, named entities, file paths, and "
    "open threads. Omit pleasantries. Output a single paragraph; do not "
    "use lists or headings.";

// Snapshots the slice that would be dropped under truncate semantics so the
// summarize path replaces the same range. Must be called with the history
// mutex held. `dropped` is the slice; `leading_system_count` is how many
// system messages precede the first non-system message.
struct SliceBoundaries {
  std::size_t leading_system_count = 0;
  std::size_t drop_count = 0;
  std::vector<ChatMessage> dropped;
};

SliceBoundaries SnapshotSlice(const std::vector<ChatMessage>& history,
                              std::size_t keep_last) {
  SliceBoundaries result;
  for (const auto& message : history) {
    if (message.role != ChatRole::System) {
      break;
    }
    ++result.leading_system_count;
  }
  const auto non_system = CountNonSystem(history);
  if (non_system <= keep_last) {
    return result;
  }
  result.drop_count = non_system - keep_last;

  std::size_t consumed = 0;
  for (std::size_t i = result.leading_system_count;
       i < history.size() && consumed < result.drop_count; ++i) {
    if (history[i].role == ChatRole::System) {
      continue;
    }
    result.dropped.push_back(history[i]);
    ++consumed;
  }
  return result;
}

void ReplaceSliceWithSummary(std::vector<ChatMessage>& history,
                             std::size_t leading_system_count,
                             std::size_t drop_count,
                             const std::string& summary_text) {
  std::vector<ChatMessage> compacted;
  compacted.reserve(history.size() - drop_count + 1);

  // Preserve any leading system prefix unchanged.
  for (std::size_t i = 0; i < leading_system_count; ++i) {
    compacted.push_back(std::move(history[i]));
  }

  compacted.push_back(ChatMessage{
      .role = ChatRole::System,
      .status = ChatMessageStatus::Complete,
      .content = "[Earlier conversation summary]\n" + summary_text});

  // Skip drop_count non-system messages, keep everything else.
  std::size_t skipped = 0;
  for (std::size_t i = leading_system_count; i < history.size(); ++i) {
    if (history[i].role != ChatRole::System && skipped < drop_count) {
      ++skipped;
      continue;
    }
    compacted.push_back(std::move(history[i]));
  }

  history = std::move(compacted);
}

// Issues a synchronous, non-streaming summarization request against the
// provider. Does NOT hold the history mutex during the call. Returns the
// concatenated assistant text on success, or an empty optional on failure.
std::optional<std::string> RunSummarizationCall(
    provider::LanguageModelProvider& provider, const ModelId& model,
    const std::vector<ChatMessage>& dropped, std::stop_token stop_token,
    std::string& failure_reason) {
  ChatRequest request;
  request.model = model;
  request.stream = false;
  request.temperature = 0.1;
  request.messages = {
      ChatMessage{.role = ChatRole::System,
                  .status = ChatMessageStatus::Complete,
                  .content = std::string(kSummarySystemPrompt)},
      ChatMessage{.role = ChatRole::User,
                  .status = ChatMessageStatus::Complete,
                  .content = "Summarize the following conversation slice:\n\n" +
                             RenderSliceForSummary(dropped)},
  };

  std::string buffered;
  bool errored = false;
  auto sink = [&buffered, &errored, &failure_reason](ChatEvent event) {
    if (auto* delta = event.As<TextDeltaEvent>()) {
      buffered += delta->text;
    } else if (auto* error = event.As<ErrorEvent>()) {
      errored = true;
      failure_reason = error->text;
    }
  };

  try {
    provider.CompleteStream(request, std::move(sink), stop_token);
  } catch (const std::exception& error) {
    failure_reason = error.what();
    return std::nullopt;
  }

  if (errored) {
    return std::nullopt;
  }
  if (buffered.empty()) {
    failure_reason = "summarization returned empty text";
    return std::nullopt;
  }
  return buffered;
}

CompactionOutcome RunSummarize(std::vector<ChatMessage>& history,
                               std::mutex& history_mutex,
                               const ChatConfig& config,
                               provider::LanguageModelProvider& provider,
                               std::stop_token stop_token) {
  SliceBoundaries boundaries;
  {
    std::scoped_lock lock(history_mutex);
    boundaries = SnapshotSlice(
        history, static_cast<std::size_t>(config.auto_compact_keep_last));
  }
  if (boundaries.drop_count == 0) {
    return {};
  }

  std::string failure_reason;
  const auto summary = RunSummarizationCall(
      provider, config.model, boundaries.dropped, stop_token, failure_reason);
  if (!summary) {
    // Fall back to truncate so the caller still gets relief from token
    // pressure. The failure_reason propagates so the host can surface it.
    auto outcome = RunTruncate(history, history_mutex, config);
    outcome.failure_reason = failure_reason.empty()
                                 ? std::string("summarization failed")
                                 : failure_reason;
    return outcome;
  }

  CompactionOutcome outcome;
  {
    std::scoped_lock lock(history_mutex);
    // Recompute boundaries under the lock — history may have changed during
    // the network call. If the new shape no longer needs compaction, skip.
    const auto fresh = SnapshotSlice(
        history, static_cast<std::size_t>(config.auto_compact_keep_last));
    if (fresh.drop_count == 0) {
      return outcome;
    }
    ReplaceSliceWithSummary(history, fresh.leading_system_count,
                            fresh.drop_count, *summary);
    outcome.performed = true;
    outcome.messages_removed = fresh.drop_count;
  }
  return outcome;
}

}  // namespace

CompactionOutcome MaybeAutoCompactHistory(
    std::vector<ChatMessage>& history, std::mutex& history_mutex,
    const ChatConfig& config, provider::LanguageModelProvider& provider,
    const std::function<void(ChatEvent)>& emit_event,
    std::stop_token stop_token) {
  CompactionOutcome outcome;
  if (config.auto_compact_mode == "summarize") {
    outcome =
        RunSummarize(history, history_mutex, config, provider, stop_token);
  } else {
    outcome = RunTruncate(history, history_mutex, config);
  }
  if (outcome.performed && emit_event) {
    emit_event(ChatEvent{ConversationCompactedEvent{
        .reason = CompactReason::Auto,
        .messages_removed = static_cast<int>(outcome.messages_removed),
    }});
  }
  return outcome;
}

}  // namespace yac::chat::internal
