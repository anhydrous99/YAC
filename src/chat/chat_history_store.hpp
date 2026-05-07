#pragma once

#include "chat/chat_service_compactor.hpp"
#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"
#include "tool_call/executor.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::chat {

// Owns the conversation history vector previously held inline by
// `ChatService`. Synchronization is provided externally via the shared
// `history_mutex` reference passed at construction so the existing
// ChatService invariant — a single mutex guarding history alongside the
// pending prompt queue, the active flag, and last_usage — is preserved
// without forcing callers to juggle two locks. The locking topology
// mirrors what `chat_service_prompt_processor.cpp` already relied on
// before the extraction.
//
// Append/Clear/Compact-style helpers documented as "caller must hold
// history_mutex" assume the caller has already locked the shared mutex
// (matching the legacy free-function contract). Methods that perform a
// network side-call (`MaybeAutoCompact`) or take a defensive copy
// (`Snapshot`) acquire the mutex internally and document so explicitly.
class ChatHistoryStore {
 public:
  explicit ChatHistoryStore(std::mutex& history_mutex);

  ChatHistoryStore(const ChatHistoryStore&) = delete;
  ChatHistoryStore& operator=(const ChatHistoryStore&) = delete;
  ChatHistoryStore(ChatHistoryStore&&) = delete;
  ChatHistoryStore& operator=(ChatHistoryStore&&) = delete;
  ~ChatHistoryStore() = default;

  // ---- Caller must hold the shared history mutex. ----

  // Append a queued user prompt (active status) — used by the prompt
  // processor as the first step of ProcessPrompt.
  void AppendActiveUserMessage(ChatMessageId id, const std::string& content);
  // Append an assistant turn that requested tool calls.
  void AppendAssistantToolRound(ChatMessageId assistant_id,
                                const std::string& content,
                                const std::vector<ToolCallRequest>& tool_calls);
  // Append a tool-result message corresponding to a prior request.
  void AppendToolResult(ChatMessageId tool_message_id,
                        const ToolCallRequest& tool_request,
                        const ::yac::tool_call::ToolExecutionResult& result);
  // Append the assistant's terminating visible turn.
  void AppendFinalAssistantMessage(ChatMessageId assistant_id,
                                   const std::string& content);
  // Direct push for the sub-agent continuation injection path. Caller
  // already constructs the ChatMessage with explicit role/status/content.
  void Append(ChatMessage message);

  // Direct view of the underlying vector. Used by request building and
  // by callers that want to scan history (e.g. checking for
  // non-system-only state). Caller must hold the shared history mutex
  // for the duration of the access.
  [[nodiscard]] const std::vector<ChatMessage>& View() const;
  // Mutable reference to the underlying vector. Exposed for legacy
  // callers (the prompt processor still receives the vector by raw
  // reference) and for in-place compaction. Caller must hold the
  // shared history mutex.
  [[nodiscard]] std::vector<ChatMessage>& MutableView();

  // Clear all history. Caller must hold the shared history mutex.
  void Clear();
  // Truncate-mode compaction: keeps the last `keep_last` non-system
  // messages, replacing the dropped slice with a synthetic system
  // note. Returns the number of removed non-system messages (0 when no
  // compaction was needed). Caller must hold the shared history mutex.
  std::size_t Compact(std::size_t keep_last);

  // True when at least one non-system message is present. Caller must
  // hold the shared history mutex.
  [[nodiscard]] bool HasNonSystemMessages() const;
  // Count of non-system messages. Caller must hold the shared history
  // mutex.
  [[nodiscard]] std::size_t CountNonSystem() const;

  // ---- Acquires the shared history mutex internally. ----

  // Returns a defensive copy of history under lock — used by
  // `ChatService::History()` to satisfy callers that hold no lock.
  [[nodiscard]] std::vector<ChatMessage> Snapshot() const;

  // Auto-compact entry point. Acquires the shared history mutex
  // internally for snapshot / replace; releases it during the
  // (possibly long-running) summarization side-call to keep the worker
  // thread unblocked. Emits a `ConversationCompactedEvent` (Auto) via
  // `emit_event` when a compaction was performed. Returns the
  // outcome so callers can surface failures.
  internal::CompactionOutcome MaybeAutoCompact(
      const ChatConfig& config, provider::LanguageModelProvider& provider,
      const std::function<void(ChatEvent)>& emit_event,
      std::stop_token stop_token);

  // ---- Pure helpers (no shared state). ----

  // Combines the static excluded-tool set with the per-mode exclusions
  // (`ExcludedToolsForMode`) and erases matching entries from `tools`
  // in place. This is the "agent-mode-aware filtering" previously
  // open-coded in `ChatServicePromptProcessor::BuildRoundRequest`.
  static void FilterToolsForAgentMode(
      std::vector<ToolDefinition>& tools,
      const std::set<std::string>& static_excluded,
      const std::set<std::string>& mode_excluded);

 private:
  std::mutex* history_mutex_;
  std::vector<ChatMessage> history_;
};

}  // namespace yac::chat
