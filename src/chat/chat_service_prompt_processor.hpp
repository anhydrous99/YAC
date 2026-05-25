#pragma once

#include "chat/tool_approval_manager.hpp"
#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace yac::chat::internal {

class ChatServiceMcp;

class ChatServicePromptProcessor {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;
  using NextMessageIdFn = std::function<ChatMessageId()>;
  using ConfigSnapshotFn = std::function<ChatConfig()>;
  using GenerationValueFn = std::function<uint64_t()>;
  using ModeExcludedToolsFn = std::function<std::set<std::string>()>;
  using PrepareBuiltInToolCallFn =
      std::function<::yac::tool_call::PreparedToolCall(const ToolCallRequest&)>;
  using ExecuteBuiltInToolCallFn =
      std::function<::yac::tool_call::ToolExecutionResult(
          const ::yac::tool_call::PreparedToolCall&, std::stop_token)>;
  // Optional side-channel hooks for auto-compaction. `OnUsageReportedFn` is
  // invoked alongside the forwarded `UsageReportedEvent` so the host can
  // cache the latest usage; `LastUsageFn` returns it back for the trigger.
  // Both default to no-ops, so existing callers are unaffected.
  using OnUsageReportedFn = std::function<void(const TokenUsage&)>;
  using LastUsageFn = std::function<std::optional<TokenUsage>()>;
  using OnBuildSwitchReminderUsedFn = std::function<void(std::string)>;
  using OnPlanExitApprovedFn =
      std::function<::yac::tool_call::ToolExecutionResult(
          const ::yac::tool_call::PreparedToolCall&)>;

  // Non-owning dependency bundle for one prompt-processing host. References and
  // pointers must outlive the processor. Callbacks may capture host state, but
  // must not transfer ownership of UI, providers, transports, or config parsers
  // into prompt processing.
  struct PromptRunContext {
    provider::ProviderRegistry* registry = nullptr;
    ::yac::tool_call::ToolExecutor* tool_executor = nullptr;
    ToolApprovalManager* tool_approval = nullptr;
    ChatServiceMcp* chat_service_mcp = nullptr;
    std::mutex* history_mutex = nullptr;
    std::vector<ChatMessage>* history = nullptr;
    EmitEventFn emit_event;
    NextMessageIdFn next_message_id;
    ConfigSnapshotFn config_snapshot;
    GenerationValueFn generation_value;
    std::set<std::string> excluded_tools;
    std::mutex* approval_gate = nullptr;
    ModeExcludedToolsFn mode_excluded_tools;
    PrepareBuiltInToolCallFn prepare_built_in_tool_call;
    ExecuteBuiltInToolCallFn execute_built_in_tool_call;
    OnUsageReportedFn on_usage_reported;
    LastUsageFn last_usage;
    OnBuildSwitchReminderUsedFn on_build_switch_reminder_used;
    OnPlanExitApprovedFn on_plan_exit_approved;
  };

  explicit ChatServicePromptProcessor(PromptRunContext context);

  void ProcessPrompt(ChatMessageId prompt_id, const std::string& prompt_content,
                     uint64_t generation, std::stop_token stop_token);

 private:
  // Outcome of a single provider round. Holds the streaming-derived state
  // (text, requested tools, streaming card ids) so ProcessPrompt's loop body
  // can read it like a struct return rather than scraping shared state.
  struct RoundOutcome {
    enum class Stop {
      // Stream finished cleanly with no further tool calls requested.
      ModelDone,
      // Model wants more tool calls; caller must run them and continue.
      CallTools,
      // Generation moved past the captured value while running.
      Aborted,
      // Provider emitted an ErrorEvent during the stream.
      StreamError,
    };
    Stop stop = Stop::ModelDone;
    std::string round_text;
    std::vector<ToolCallRequest> requested_tools;
    std::unordered_map<ToolCallId, ChatMessageId> streaming_card_ids;
  };

  // Drives one provider stream: builds the request under the history lock,
  // runs the streaming sink, and reports back via RoundOutcome. Cancellation
  // events are NOT emitted from here — the caller observes Stop::Aborted /
  // Stop::StreamError and decides what to surface.
  [[nodiscard]] RoundOutcome RunOneRound(
      provider::LanguageModelProvider& provider, ChatMessageId assistant_id,
      uint64_t generation, std::stop_token stop_token);

  // Builds the per-round request snapshot under the history lock.
  // `aborted` is set to true when the captured `generation` is stale —
  // the caller must observe it and bail before sending the (empty)
  // request.
  [[nodiscard]] ChatRequest BuildRoundRequest(uint64_t generation,
                                              bool& aborted) const;
  void RunToolRound(
      const std::vector<ToolCallRequest>& requested_tools,
      const std::unordered_map<ToolCallId, ChatMessageId>& streaming_card_ids,
      uint64_t generation, std::stop_token stop_token) const;
  // Returns true when ResetConversation or CancelActiveResponse has
  // bumped generation past `generation`. Caller must hold
  // *history_mutex_; this is shorthand for the recurring re-check
  // pattern at every history-mutating site.
  [[nodiscard]] bool ShouldAbortLocked(uint64_t generation) const;
  // Emits the standard "assistant cancelled" status pair used at every
  // generation-mismatch / abort exit point. Must be called outside the
  // history lock — see the lock-then-emit comment in ProcessPrompt.
  void EmitCancellation(ChatMessageId assistant_id) const;
  provider::ProviderRegistry* registry_;
  ToolApprovalManager* tool_approval_;
  ChatServiceMcp* chat_service_mcp_;
  std::mutex* history_mutex_;
  std::vector<ChatMessage>* history_;
  EmitEventFn emit_event_;
  NextMessageIdFn next_message_id_;
  ConfigSnapshotFn config_snapshot_;
  GenerationValueFn generation_value_;
  std::set<std::string> excluded_tools_;
  std::mutex* approval_gate_;
  ModeExcludedToolsFn mode_excluded_tools_;
  PrepareBuiltInToolCallFn prepare_built_in_tool_call_;
  ExecuteBuiltInToolCallFn execute_built_in_tool_call_;
  OnUsageReportedFn on_usage_reported_;
  LastUsageFn last_usage_;
  OnBuildSwitchReminderUsedFn on_build_switch_reminder_used_;
  OnPlanExitApprovedFn on_plan_exit_approved_;
};

}  // namespace yac::chat::internal
