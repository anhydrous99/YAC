#pragma once

#include "chat/chat_service_request_builder.hpp"
#include "chat/tool_approval_manager.hpp"
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

  ChatServicePromptProcessor(
      provider::ProviderRegistry& registry,
      ::yac::tool_call::ToolExecutor& tool_executor,
      ToolApprovalManager& tool_approval, ChatServiceMcp* chat_service_mcp,
      std::mutex& history_mutex, std::vector<ChatMessage>& history,
      EmitEventFn emit_event, NextMessageIdFn next_message_id,
      ConfigSnapshotFn config_snapshot, GenerationValueFn generation_value,
      std::set<std::string> excluded_tools = {},
      std::mutex* approval_gate = nullptr,
      ModeExcludedToolsFn mode_excluded_tools = {},
      PrepareBuiltInToolCallFn prepare_built_in_tool_call = {},
      ExecuteBuiltInToolCallFn execute_built_in_tool_call = {},
      OnUsageReportedFn on_usage_reported = {}, LastUsageFn last_usage = {});

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
      provider::LanguageModelProvider& provider,
      const ChatServiceRequestBuilder& request_builder,
      ChatMessageId assistant_id, uint64_t generation,
      std::stop_token stop_token);

  // Builds the per-round request snapshot under the history lock.
  // `aborted` is set to true when the captured `generation` is stale —
  // the caller must observe it and bail before sending the (empty)
  // request.
  [[nodiscard]] ChatRequest BuildRoundRequest(
      const ChatServiceRequestBuilder& request_builder, uint64_t generation,
      bool& aborted) const;
  void RunToolRound(
      const std::vector<ToolCallRequest>& requested_tools,
      const std::unordered_map<ToolCallId, ChatMessageId>& streaming_card_ids,
      uint64_t generation, std::stop_token stop_token);

  // Holds a successful Prepare result alongside any preparation failure that
  // surfaced — fallback previews are always populated even on failure so the
  // tool card can render a meaningful error.
  struct ToolPrepFailure {
    std::string error;
    std::string result_json;
  };
  struct ToolPrep {
    ::yac::tool_call::PreparedToolCall prepared;
    std::optional<ToolPrepFailure> failure;
  };

  [[nodiscard]] ToolPrep PrepareOneToolCall(const ToolCallRequest& request,
                                            bool is_mcp_tool) const;
  // Awaits user approval (or auto-approves ask_user). Returns true when
  // the call should proceed. Sets *gate_aborted when stop fires while the
  // approval gate is held; the caller must observe and bail without
  // appending history.
  [[nodiscard]] bool MaybeAwaitApproval(
      ::yac::tool_call::PreparedToolCall& prepared,
      const ToolCallRequest& tool_request, ChatMessageId tool_message_id,
      std::stop_token stop_token, bool& gate_aborted);
  [[nodiscard]] ::yac::tool_call::ToolExecutionResult ExecuteOneToolCall(
      const ::yac::tool_call::PreparedToolCall& prepared,
      const std::optional<ToolPrepFailure>& failure, bool approved,
      bool is_mcp_tool, std::stop_token stop_token);
  // Returns true when ResetConversation or CancelActiveResponse has
  // bumped generation past `generation`. Caller must hold
  // *history_mutex_; this is shorthand for the recurring re-check
  // pattern at every history-mutating site.
  [[nodiscard]] bool ShouldAbortLocked(uint64_t generation) const;
  // Emits the standard "assistant cancelled" status pair used at every
  // generation-mismatch / abort exit point. Must be called outside the
  // history lock — see the lock-then-emit comment in ProcessPrompt.
  void EmitCancellation(ChatMessageId assistant_id) const;
  [[nodiscard]] static ::yac::tool_call::ToolExecutionResult
  MakeRejectedToolResult(const ::yac::tool_call::PreparedToolCall& prepared);

  provider::ProviderRegistry* registry_;
  ::yac::tool_call::ToolExecutor* tool_executor_;
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
};

}  // namespace yac::chat::internal
