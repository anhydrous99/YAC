#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::chat::internal {

struct CompactionOutcome {
  bool performed = false;
  std::size_t messages_removed = 0;
  std::string failure_reason;  // empty on success
};

// Synchronously compacts `history` if `config.auto_compact_mode` is set and
// the history has more non-system messages than
// `config.auto_compact_keep_last`. Modes:
//   - "summarize": runs a non-streaming side-call against `provider` to
//     produce a paragraph summary of the dropped slice, then replaces the
//     slice with a single synthetic system message containing that summary.
//     On error, falls back to truncate.
//   - "truncate": delegates to `internal::CompactHistory`, which preserves
//     the leading system prefix and inserts a synthetic
//     "[Earlier conversation compacted. N messages removed.]" note.
//
// Emits `ConversationCompactedEvent` via `emit_event` on success (any mode).
// Holds `history_mutex` only while reading/writing `history` — the side-call
// runs without it so the worker thread isn't blocked on the network.
//
// Re-entry: `provider.CompleteStream` here bypasses
// `ChatService::SubmitUserMessage`, so any `UsageReportedEvent` emitted by
// the side-call never reaches the prompt processor's
// `on_usage_reported_` callback. The summarization side-call cannot trigger
// further auto-compaction.
CompactionOutcome MaybeAutoCompactHistory(
    std::vector<ChatMessage>& history, std::mutex& history_mutex,
    const ChatConfig& config, provider::LanguageModelProvider& provider,
    const std::function<void(ChatEvent)>& emit_event,
    std::stop_token stop_token);

}  // namespace yac::chat::internal
