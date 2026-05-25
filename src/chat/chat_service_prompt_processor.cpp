#include "chat/chat_service_prompt_processor.hpp"

#include "chat/chat_history_store.hpp"
#include "chat/chat_service_compactor.hpp"
#include "chat/chat_service_history.hpp"
#include "chat/chat_service_mcp.hpp"
#include "chat/chat_service_request_builder.hpp"
#include "chat/tool_round_runner.hpp"
#include "provider/model_context_windows.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace yac::chat::internal {

ChatServicePromptProcessor::ChatServicePromptProcessor(PromptRunContext context)
    : registry_(context.registry),
      tool_approval_(context.tool_approval),
      chat_service_mcp_(context.chat_service_mcp),
      history_mutex_(context.history_mutex),
      history_(context.history),
      emit_event_(std::move(context.emit_event)),
      next_message_id_(std::move(context.next_message_id)),
      config_snapshot_(std::move(context.config_snapshot)),
      generation_value_(std::move(context.generation_value)),
      excluded_tools_(std::move(context.excluded_tools)),
      approval_gate_(context.approval_gate),
      mode_excluded_tools_(std::move(context.mode_excluded_tools)),
      prepare_built_in_tool_call_(
          context.prepare_built_in_tool_call
              ? std::move(context.prepare_built_in_tool_call)
              : PrepareBuiltInToolCallFn{[](const ToolCallRequest& request) {
                  return ::yac::tool_call::ToolExecutor::Prepare(request);
                }}),
      execute_built_in_tool_call_(
          context.execute_built_in_tool_call
              ? std::move(context.execute_built_in_tool_call)
              : ExecuteBuiltInToolCallFn{[tool_executor_ptr =
                                              context.tool_executor](
                                             const ::yac::tool_call::
                                                 PreparedToolCall& prepared,
                                             std::stop_token stop_token) {
                  return tool_executor_ptr->Execute(prepared, stop_token);
                }}),
      on_usage_reported_(std::move(context.on_usage_reported)),
      last_usage_(std::move(context.last_usage)),
      on_build_switch_reminder_used_(
          std::move(context.on_build_switch_reminder_used)),
      on_plan_exit_approved_(std::move(context.on_plan_exit_approved)) {}

ChatServicePromptProcessor::RoundOutcome
ChatServicePromptProcessor::RunOneRound(
    provider::LanguageModelProvider& provider, ChatMessageId assistant_id,
    uint64_t generation, std::stop_token stop_token) {
  RoundOutcome outcome;
  bool round_aborted = false;
  const ChatRequest request = BuildRoundRequest(generation, round_aborted);
  if (round_aborted) {
    outcome.stop = RoundOutcome::Stop::Aborted;
    return outcome;
  }

  auto sink = [this, &outcome, assistant_id,
               generation](ChatEvent event) mutable {
    if (generation_value_() != generation) {
      return;
    }
    if (auto* tool_requested = event.As<ToolCallRequestedEvent>()) {
      outcome.requested_tools = std::move(tool_requested->tool_calls);
      return;
    }
    if (auto* delta = event.As<TextDeltaEvent>()) {
      if (delta->text.empty()) {
        return;
      }
      outcome.round_text += delta->text;
      emit_event_(
          ChatEvent{TextDeltaEvent{.message_id = assistant_id,
                                   .role = ChatRole::Assistant,
                                   .text = std::move(delta->text),
                                   .provider_id = std::move(delta->provider_id),
                                   .model = std::move(delta->model)}});
      return;
    }
    if (auto* arg_delta = event.As<ToolCallArgumentDeltaEvent>()) {
      auto [it, inserted] = outcome.streaming_card_ids.try_emplace(
          arg_delta->tool_call_id, ChatMessageId{0});
      if (inserted) {
        it->second = next_message_id_();
      }
      arg_delta->message_id = assistant_id;
      arg_delta->card_message_id = it->second;
      emit_event_(std::move(event));
      return;
    }
    if (auto* error = event.As<ErrorEvent>()) {
      outcome.stop = RoundOutcome::Stop::StreamError;
      emit_event_(
          ChatEvent{ErrorEvent{.message_id = assistant_id,
                               .role = ChatRole::Assistant,
                               .text = std::move(error->text),
                               .provider_id = std::move(error->provider_id),
                               .model = std::move(error->model),
                               .status = ChatMessageStatus::Error}});
      return;
    }
    // FinishedEvent from the provider signals stream end; ProcessPrompt emits
    // its own authoritative FinishedEvent (with message_id) after all rounds.
    if (event.As<FinishedEvent>()) {
      return;
    }
    if (auto* usage = event.As<UsageReportedEvent>();
        usage != nullptr && on_usage_reported_) {
      on_usage_reported_(usage->usage);
    }
    emit_event_(std::move(event));
  };

  provider.CompleteStream(request, std::move(sink), stop_token);

  if (generation_value_() != generation) {
    outcome.stop = RoundOutcome::Stop::Aborted;
    return outcome;
  }
  if (outcome.stop == RoundOutcome::Stop::StreamError) {
    return outcome;
  }
  outcome.stop = outcome.requested_tools.empty()
                     ? RoundOutcome::Stop::ModelDone
                     : RoundOutcome::Stop::CallTools;
  return outcome;
}

void ChatServicePromptProcessor::ProcessPrompt(
    ChatMessageId prompt_id, const std::string& prompt_content,
    uint64_t generation, std::stop_token stop_token) {
  const auto assistant_id = next_message_id_();
  const ChatServiceRequestBuilder request_builder(config_snapshot_());
  auto provider = registry_->Resolve(request_builder.Config().provider_id);
  if (provider == nullptr) {
    emit_event_(ChatEvent{
        MessageStatusChangedEvent{.message_id = prompt_id,
                                  .role = ChatRole::User,
                                  .status = ChatMessageStatus::Complete}});
    emit_event_(ChatEvent{
        ErrorEvent{.message_id = assistant_id,
                   .role = ChatRole::Assistant,
                   .text = "No provider registered for '" +
                           request_builder.Config().provider_id.value + "'.",
                   .status = ChatMessageStatus::Error}});
    emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
    return;
  }

  if (generation_value_() != generation) {
    return;
  }

  // Auto-compact BEFORE appending the new user message so the new prompt is
  // the freshest message and isn't itself a candidate for being summarized.
  // The trigger relies on `prompt_tokens` from the previous round's
  // UsageReportedEvent — stale by one turn but the threshold (default 0.8)
  // leaves headroom.
  const auto& cfg = request_builder.Config();
  if (cfg.auto_compact_enabled && last_usage_) {
    const auto prior_usage = last_usage_();
    if (prior_usage && prior_usage->prompt_tokens > 0) {
      const int window = ::yac::provider::ResolveContextWindow(provider.get(),
                                                               cfg.model.value);
      if (window > 0) {
        const double pct = static_cast<double>(prior_usage->prompt_tokens) /
                           static_cast<double>(window);
        if (pct >= cfg.auto_compact_threshold) {
          MaybeAutoCompactHistory(*history_, *history_mutex_, cfg, *provider,
                                  emit_event_, stop_token);
        }
      }
    }
  }

  {
    std::scoped_lock lock(*history_mutex_);
    if (ShouldAbortLocked(generation)) {
      return;
    }
    ChatServiceHistory(*history_).AppendActiveUserMessage(prompt_id,
                                                          prompt_content);
  }

  emit_event_(ChatEvent{
      MessageStatusChangedEvent{.message_id = prompt_id,
                                .role = ChatRole::User,
                                .status = ChatMessageStatus::Complete}});
  emit_event_(ChatEvent{
      StartedEvent{.message_id = assistant_id,
                   .role = ChatRole::Assistant,
                   .provider_id = request_builder.Config().provider_id,
                   .model = request_builder.Config().model,
                   .status = ChatMessageStatus::Active}});

  std::string visible_assistant_text;
  while (true) {
    auto outcome = RunOneRound(*provider, assistant_id, generation, stop_token);
    visible_assistant_text += outcome.round_text;
    if (outcome.stop == RoundOutcome::Stop::Aborted) {
      EmitCancellation(assistant_id);
      return;
    }
    if (outcome.stop == RoundOutcome::Stop::StreamError) {
      emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
      return;
    }
    if (outcome.stop == RoundOutcome::Stop::ModelDone) {
      break;
    }

    // Stop::CallTools — append the assistant turn under the lock, then run
    // the tool round outside it (emit_event_ re-enters history_mutex_).
    bool tool_round_aborted = false;
    {
      std::scoped_lock lock(*history_mutex_);
      if (ShouldAbortLocked(generation)) {
        tool_round_aborted = true;
      } else {
        ChatServiceHistory(*history_).AppendAssistantToolRound(
            assistant_id, outcome.round_text, outcome.requested_tools);
      }
    }
    if (tool_round_aborted) {
      EmitCancellation(assistant_id);
      return;
    }
    RunToolRound(outcome.requested_tools, outcome.streaming_card_ids,
                 generation, stop_token);
    if (stop_token.stop_requested() || generation_value_() != generation) {
      EmitCancellation(assistant_id);
      return;
    }
  }

  if (!visible_assistant_text.empty()) {
    bool final_message_aborted = false;
    {
      std::scoped_lock lock(*history_mutex_);
      if (ShouldAbortLocked(generation)) {
        final_message_aborted = true;
      } else {
        ChatServiceHistory(*history_).AppendFinalAssistantMessage(
            assistant_id, visible_assistant_text);
      }
    }
    // See note above: emit_event_ re-enters history_mutex_, so the
    // cancellation events must be emitted after the scoped_lock releases.
    if (final_message_aborted) {
      EmitCancellation(assistant_id);
      return;
    }
  }
  emit_event_(ChatEvent{
      AssistantMessageDoneEvent{.message_id = assistant_id,
                                .role = ChatRole::Assistant,
                                .status = ChatMessageStatus::Complete}});
  emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
}

ChatRequest ChatServicePromptProcessor::BuildRoundRequest(uint64_t generation,
                                                          bool& aborted) const {
  // Each model round intentionally rebuilds from a fresh config snapshot while
  // provider selection and prompt start metadata keep the ProcessPrompt entry
  // snapshot. This lets mode/tool changes from approved plan_exit apply to the
  // next round without changing the active provider mid-prompt.
  const auto config = config_snapshot_();
  ChatRequest request;
  std::optional<std::string> consumed_build_switch_path;
  {
    std::scoped_lock lock(*history_mutex_);
    // Gate the read on a fresh generation — closes the window where
    // ResetConversation clears history between an outer check and this
    // read. Caller observes `aborted` and bails before issuing the
    // upstream request.
    if (ShouldAbortLocked(generation)) {
      aborted = true;
      return {};
    }
    auto tools = ::yac::tool_call::ToolExecutor::Definitions();
    if (chat_service_mcp_ != nullptr) {
      tools = yac::chat::internal::ChatServiceMcp::MergeBuiltInsAndMcp(
          tools, chat_service_mcp_->BuildToolCatalogSnapshot());
    }
    auto mode_excluded =
        mode_excluded_tools_ ? mode_excluded_tools_() : std::set<std::string>{};
    ChatHistoryStore::FilterToolsForAgentMode(tools, excluded_tools_,
                                              mode_excluded, config.agent_mode);
    const ChatServiceRequestBuilder fresh_request_builder(config);
    request = fresh_request_builder.BuildRequest(*history_, tools);
    if (config.agent_mode == AgentMode::Build &&
        config.build_switch_plan_path.has_value()) {
      consumed_build_switch_path = *config.build_switch_plan_path;
    }
  }
  if (consumed_build_switch_path.has_value() &&
      on_build_switch_reminder_used_) {
    on_build_switch_reminder_used_(*consumed_build_switch_path);
  }
  return request;
}

bool ChatServicePromptProcessor::ShouldAbortLocked(uint64_t generation) const {
  return generation_value_() != generation;
}

void ChatServicePromptProcessor::EmitCancellation(
    ChatMessageId assistant_id) const {
  emit_event_(ChatEvent{
      MessageStatusChangedEvent{.message_id = assistant_id,
                                .role = ChatRole::Assistant,
                                .status = ChatMessageStatus::Cancelled}});
  emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
}

void ChatServicePromptProcessor::RunToolRound(
    const std::vector<ToolCallRequest>& requested_tools,
    const std::unordered_map<ToolCallId, ChatMessageId>& streaming_card_ids,
    uint64_t generation, std::stop_token stop_token) const {
  ToolRoundRunner(PromptRunContext{
                      .tool_executor = nullptr,
                      .tool_approval = tool_approval_,
                      .chat_service_mcp = chat_service_mcp_,
                      .history_mutex = history_mutex_,
                      .history = history_,
                      .emit_event = emit_event_,
                      .next_message_id = next_message_id_,
                      .config_snapshot = config_snapshot_,
                      .generation_value = generation_value_,
                      .approval_gate = approval_gate_,
                      .prepare_built_in_tool_call = prepare_built_in_tool_call_,
                      .execute_built_in_tool_call = execute_built_in_tool_call_,
                      .on_plan_exit_approved = on_plan_exit_approved_,
                  })
      .Run(requested_tools, streaming_card_ids, generation, stop_token);
}

}  // namespace yac::chat::internal
