#include "chat/chat_service_prompt_processor.hpp"

#include "chat/chat_service_history.hpp"

#include <utility>

namespace yac::chat::internal {

namespace {

constexpr int kMaxToolRounds = 8;

std::string ToolRejectedJson() {
  return R"({"error":"User rejected tool execution."})";
}

}  // namespace

ChatServicePromptProcessor::ChatServicePromptProcessor(
    provider::ProviderRegistry& registry,
    ::yac::tool_call::ToolExecutor& tool_executor,
    ChatServiceToolApproval& tool_approval, std::mutex& history_mutex,
    std::vector<ChatMessage>& history, EmitEventFn emit_event,
    NextMessageIdFn next_message_id, ConfigSnapshotFn config_snapshot,
    GenerationValueFn generation_value, std::set<std::string> excluded_tools,
    std::mutex* approval_gate)
    : registry_(&registry),
      tool_executor_(&tool_executor),
      tool_approval_(&tool_approval),
      history_mutex_(&history_mutex),
      history_(&history),
      emit_event_(std::move(emit_event)),
      next_message_id_(std::move(next_message_id)),
      config_snapshot_(std::move(config_snapshot)),
      generation_value_(std::move(generation_value)),
      excluded_tools_(std::move(excluded_tools)),
      approval_gate_(approval_gate) {}

void ChatServicePromptProcessor::ProcessPrompt(
    ChatMessageId prompt_id, const std::string& prompt_content,
    uint64_t generation, std::stop_token stop_token) {
  const auto assistant_id = next_message_id_();
  const ChatServiceRequestBuilder request_builder(config_snapshot_());
  auto provider = registry_->Resolve(request_builder.Config().provider_id);
  if (provider == nullptr) {
    emit_event_(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                          .message_id = prompt_id,
                          .role = ChatRole::User,
                          .status = ChatMessageStatus::Complete});
    emit_event_(ChatEvent{.type = ChatEventType::Error,
                          .message_id = assistant_id,
                          .role = ChatRole::Assistant,
                          .text = "No provider registered for '" +
                                  request_builder.Config().provider_id + "'.",
                          .status = ChatMessageStatus::Error});
    emit_event_(
        ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
    return;
  }

  {
    std::lock_guard lock(*history_mutex_);
    ChatServiceHistory(*history_).AppendActiveUserMessage(prompt_id,
                                                          prompt_content);
  }

  emit_event_(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                        .message_id = prompt_id,
                        .role = ChatRole::User,
                        .status = ChatMessageStatus::Complete});
  emit_event_(ChatEvent{.type = ChatEventType::Started,
                        .message_id = assistant_id,
                        .role = ChatRole::Assistant,
                        .provider_id = request_builder.Config().provider_id,
                        .model = request_builder.Config().model,
                        .status = ChatMessageStatus::Active});

  std::string visible_assistant_text;
  bool assistant_error = false;
  for (int round = 0; round <= kMaxToolRounds; ++round) {
    std::string round_text;
    std::vector<ToolCallRequest> requested_tools;
    const ChatRequest request = BuildRoundRequest(request_builder);

    auto sink = [this, &round_text, assistant_id, generation, &assistant_error,
                 &requested_tools](ChatEvent event) mutable {
      if (generation_value_() != generation) {
        return;
      }
      if (event.type == ChatEventType::ToolCallRequested) {
        requested_tools = std::move(event.tool_calls);
        return;
      }
      event.message_id = assistant_id;
      event.role = ChatRole::Assistant;
      if (event.type == ChatEventType::TextDelta) {
        if (event.text.empty()) {
          return;
        }
        round_text += event.text;
      } else if (event.type == ChatEventType::Error) {
        assistant_error = true;
        event.status = ChatMessageStatus::Error;
      }
      emit_event_(std::move(event));
    };

    provider->CompleteStream(request, std::move(sink), stop_token);

    if (generation_value_() != generation) {
      emit_event_(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                            .message_id = assistant_id,
                            .role = ChatRole::Assistant,
                            .status = ChatMessageStatus::Cancelled});
      emit_event_(ChatEvent{.type = ChatEventType::Finished,
                            .message_id = assistant_id});
      return;
    }

    if (assistant_error) {
      emit_event_(ChatEvent{.type = ChatEventType::Finished,
                            .message_id = assistant_id});
      return;
    }

    visible_assistant_text += round_text;

    if (requested_tools.empty()) {
      break;
    }

    {
      std::lock_guard lock(*history_mutex_);
      ChatServiceHistory(*history_).AppendAssistantToolRound(
          assistant_id, round_text, requested_tools);
    }

    RunToolRound(requested_tools, stop_token);

    if (round == kMaxToolRounds) {
      emit_event_(ChatEvent{.type = ChatEventType::Error,
                            .message_id = assistant_id,
                            .role = ChatRole::Assistant,
                            .text = "Tool round limit reached.",
                            .status = ChatMessageStatus::Error});
      emit_event_(ChatEvent{.type = ChatEventType::Finished,
                            .message_id = assistant_id});
      return;
    }
  }

  if (!visible_assistant_text.empty()) {
    std::lock_guard lock(*history_mutex_);
    ChatServiceHistory(*history_).AppendFinalAssistantMessage(
        assistant_id, visible_assistant_text);
  }
  emit_event_(ChatEvent{.type = ChatEventType::AssistantMessageDone,
                        .message_id = assistant_id,
                        .role = ChatRole::Assistant,
                        .status = ChatMessageStatus::Complete});
  emit_event_(
      ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
}

ChatRequest ChatServicePromptProcessor::BuildRoundRequest(
    const ChatServiceRequestBuilder& request_builder) const {
  std::lock_guard lock(*history_mutex_);
  auto tools = ::yac::tool_call::ToolExecutor::Definitions();
  std::erase_if(tools, [this](const auto& t) {
    return excluded_tools_.contains(t.name);
  });
  return request_builder.BuildRequest(*history_, tools);
}

void ChatServicePromptProcessor::RunToolRound(
    const std::vector<ToolCallRequest>& requested_tools,
    std::stop_token stop_token) {
  for (const auto& tool_request : requested_tools) {
    auto tool_message_id = next_message_id_();
    auto prepared = ::yac::tool_call::ToolExecutor::Prepare(tool_request);
    emit_event_(ChatEvent{.type = ChatEventType::ToolCallStarted,
                          .message_id = tool_message_id,
                          .role = ChatRole::Tool,
                          .tool_call_id = tool_request.id,
                          .tool_name = tool_request.name,
                          .tool_call = prepared.preview,
                          .status = ChatMessageStatus::Active});

    bool approved = true;
    if (prepared.requires_approval) {
      std::unique_lock<std::mutex> gate_lock;
      if (approval_gate_ != nullptr) {
        gate_lock = std::unique_lock<std::mutex>(*approval_gate_);
        if (stop_token.stop_requested()) {
          return;
        }
      }
      auto approval_id = tool_approval_->BeginPendingApproval();
      emit_event_(ChatEvent{.type = ChatEventType::ToolApprovalRequested,
                            .message_id = tool_message_id,
                            .role = ChatRole::Tool,
                            .text = prepared.approval_prompt,
                            .tool_call_id = tool_request.id,
                            .tool_name = tool_request.name,
                            .approval_id = approval_id,
                            .tool_call = prepared.preview,
                            .status = ChatMessageStatus::Queued});
      approved = tool_approval_->WaitForResolution(approval_id, stop_token);
    }

    ::yac::tool_call::ToolExecutionResult result =
        approved ? tool_executor_->Execute(prepared, stop_token)
                 : MakeRejectedToolResult(prepared);

    emit_event_(ChatEvent{.type = ChatEventType::ToolCallDone,
                          .message_id = tool_message_id,
                          .role = ChatRole::Tool,
                          .tool_call_id = tool_request.id,
                          .tool_name = tool_request.name,
                          .tool_call = result.block,
                          .status = result.is_error
                                        ? ChatMessageStatus::Error
                                        : ChatMessageStatus::Complete});
    {
      std::lock_guard lock(*history_mutex_);
      ChatServiceHistory(*history_).AppendToolResult(tool_message_id,
                                                     tool_request, result);
    }

    if (stop_token.stop_requested()) {
      return;
    }
  }
}

::yac::tool_call::ToolExecutionResult
ChatServicePromptProcessor::MakeRejectedToolResult(
    const ::yac::tool_call::PreparedToolCall& prepared) {
  ::yac::tool_call::ToolExecutionResult result{
      .block = prepared.preview,
      .result_json = ToolRejectedJson(),
      .is_error = true,
  };
  std::visit(
      [](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = "User rejected tool execution.";
        }
      },
      result.block);
  return result;
}

}  // namespace yac::chat::internal
