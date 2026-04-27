#include "chat/chat_service_prompt_processor.hpp"

#include "chat/chat_service_history.hpp"
#include "chat/chat_service_mcp.hpp"
#include "mcp/tool_naming.hpp"
#include "tool_call/executor_arguments.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace yac::chat::internal {

namespace {

std::string ToolRejectedJson() {
  return R"({"error":"User rejected tool execution."})";
}

::yac::tool_call::PreparedToolCall MakeFallbackPreparedToolCall(
    const ::yac::chat::ToolCallRequest& request) {
  if (const auto parsed = ::yac::mcp::SplitMcpToolName(request.name);
      parsed.has_value()) {
    return ::yac::tool_call::PreparedToolCall{
        .request = request,
        .preview = ::yac::tool_call::McpToolCall{
            .server_id = parsed->first,
            .tool_name = request.name,
            .original_tool_name = parsed->second,
            .arguments_json = request.arguments_json,
        }};
  }
  return ::yac::tool_call::PreparedToolCall{
      .request = request,
      .preview = ::yac::tool_call::BashCall{.command = request.name,
                                            .is_error = true}};
}

::yac::tool_call::ToolExecutionResult MakeErrorToolResult(
    ::yac::tool_call::ToolCallBlock block, std::string message) {
  std::visit(
      [&message](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = message;
        } else if constexpr (requires { call.is_error; }) {
          call.is_error = true;
        }
      },
      block);
  return ::yac::tool_call::ToolExecutionResult{
      .block = std::move(block),
      .result_json =
          ::yac::tool_call::Json{{"error", std::move(message)}}.dump(),
      .is_error = true,
  };
}

}  // namespace

ChatServicePromptProcessor::ChatServicePromptProcessor(
    provider::ProviderRegistry& registry,
    ::yac::tool_call::ToolExecutor& tool_executor,
    ChatServiceToolApproval& tool_approval, ChatServiceMcp* chat_service_mcp,
    std::mutex& history_mutex, std::vector<ChatMessage>& history,
    EmitEventFn emit_event, NextMessageIdFn next_message_id,
    ConfigSnapshotFn config_snapshot, GenerationValueFn generation_value,
    std::set<std::string> excluded_tools, std::mutex* approval_gate,
    ModeExcludedToolsFn mode_excluded_tools,
    PrepareBuiltInToolCallFn prepare_built_in_tool_call,
    ExecuteBuiltInToolCallFn execute_built_in_tool_call)
    : registry_(&registry),
      tool_executor_(&tool_executor),
      tool_approval_(&tool_approval),
      chat_service_mcp_(chat_service_mcp),
      history_mutex_(&history_mutex),
      history_(&history),
      emit_event_(std::move(emit_event)),
      next_message_id_(std::move(next_message_id)),
      config_snapshot_(std::move(config_snapshot)),
      generation_value_(std::move(generation_value)),
      excluded_tools_(std::move(excluded_tools)),
      approval_gate_(approval_gate),
      mode_excluded_tools_(std::move(mode_excluded_tools)),
      prepare_built_in_tool_call_(
          prepare_built_in_tool_call
              ? std::move(prepare_built_in_tool_call)
              : PrepareBuiltInToolCallFn{[](const ToolCallRequest& request) {
                  return ::yac::tool_call::ToolExecutor::Prepare(request);
                }}),
      execute_built_in_tool_call_(
          execute_built_in_tool_call
              ? std::move(execute_built_in_tool_call)
              : ExecuteBuiltInToolCallFn{
                    [tool_executor_ptr = &tool_executor](
                        const ::yac::tool_call::PreparedToolCall& prepared,
                        std::stop_token stop_token) {
                      return tool_executor_ptr->Execute(prepared, stop_token);
                    }}) {}

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
                           request_builder.Config().provider_id + "'.",
                   .status = ChatMessageStatus::Error}});
    emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
    return;
  }

  if (generation_value_() != generation) {
    return;
  }

  {
    std::lock_guard lock(*history_mutex_);
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
  bool assistant_error = false;
  // Hoisted so the post-loop check can inspect the final iteration's value:
  // non-empty after the loop exits means the model still wanted to call tools
  // when we hit the configured tool-round cap.
  std::vector<ToolCallRequest> requested_tools;
  const int max_tool_rounds =
      std::max(kMinToolRoundLimit, request_builder.Config().max_tool_rounds);
  for (int round = 0; round < max_tool_rounds; ++round) {
    std::string round_text;
    requested_tools.clear();
    std::unordered_map<std::string, ChatMessageId> streaming_card_ids;
    const ChatRequest request = BuildRoundRequest(request_builder);

    auto sink = [this, &round_text, assistant_id, generation, &assistant_error,
                 &requested_tools,
                 &streaming_card_ids](ChatEvent event) mutable {
      if (generation_value_() != generation) {
        return;
      }
      if (auto* tool_requested = event.As<ToolCallRequestedEvent>()) {
        requested_tools = std::move(tool_requested->tool_calls);
        return;
      }
      if (auto* delta = event.As<TextDeltaEvent>()) {
        if (delta->text.empty()) {
          return;
        }
        round_text += delta->text;
        emit_event_(ChatEvent{
            TextDeltaEvent{.message_id = assistant_id,
                           .role = ChatRole::Assistant,
                           .text = std::move(delta->text),
                           .provider_id = std::move(delta->provider_id),
                           .model = std::move(delta->model)}});
        return;
      }
      if (auto* arg_delta = event.As<ToolCallArgumentDeltaEvent>()) {
        auto [it, inserted] = streaming_card_ids.try_emplace(
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
        assistant_error = true;
        emit_event_(
            ChatEvent{ErrorEvent{.message_id = assistant_id,
                                 .role = ChatRole::Assistant,
                                 .text = std::move(error->text),
                                 .provider_id = std::move(error->provider_id),
                                 .model = std::move(error->model),
                                 .status = ChatMessageStatus::Error}});
        return;
      }
      emit_event_(std::move(event));
    };

    provider->CompleteStream(request, std::move(sink), stop_token);

    if (generation_value_() != generation) {
      emit_event_(ChatEvent{
          MessageStatusChangedEvent{.message_id = assistant_id,
                                    .role = ChatRole::Assistant,
                                    .status = ChatMessageStatus::Cancelled}});
      emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
      return;
    }

    if (assistant_error) {
      emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
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

    RunToolRound(requested_tools, streaming_card_ids, stop_token);
  }

  if (!requested_tools.empty()) {
    emit_event_(ChatEvent{
        ErrorEvent{.message_id = assistant_id,
                   .role = ChatRole::Assistant,
                   .text = "Tool round limit reached after " +
                           std::to_string(max_tool_rounds) +
                           (max_tool_rounds == 1 ? " round." : " rounds."),
                   .status = ChatMessageStatus::Error}});
    emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
    return;
  }

  if (!visible_assistant_text.empty()) {
    std::lock_guard lock(*history_mutex_);
    ChatServiceHistory(*history_).AppendFinalAssistantMessage(
        assistant_id, visible_assistant_text);
  }
  emit_event_(ChatEvent{
      AssistantMessageDoneEvent{.message_id = assistant_id,
                                .role = ChatRole::Assistant,
                                .status = ChatMessageStatus::Complete}});
  emit_event_(ChatEvent{FinishedEvent{.message_id = assistant_id}});
}

ChatRequest ChatServicePromptProcessor::BuildRoundRequest(
    const ChatServiceRequestBuilder& request_builder) const {
  std::lock_guard lock(*history_mutex_);
  auto tools = ::yac::tool_call::ToolExecutor::Definitions();
  if (chat_service_mcp_ != nullptr) {
    tools = chat_service_mcp_->MergeBuiltInsAndMcp(
        tools, chat_service_mcp_->BuildToolCatalogSnapshot());
  }
  auto mode_excluded =
      mode_excluded_tools_ ? mode_excluded_tools_() : std::set<std::string>{};
  std::erase_if(tools, [this, &mode_excluded](const auto& t) {
    return excluded_tools_.contains(t.name) || mode_excluded.contains(t.name);
  });
  return request_builder.BuildRequest(*history_, tools);
}

void ChatServicePromptProcessor::RunToolRound(
    const std::vector<ToolCallRequest>& requested_tools,
    const std::unordered_map<std::string, ChatMessageId>& streaming_card_ids,
    std::stop_token stop_token) {
  for (const auto& tool_request : requested_tools) {
    ChatMessageId tool_message_id = 0;
    if (auto it = streaming_card_ids.find(tool_request.id);
        it != streaming_card_ids.end()) {
      tool_message_id = it->second;
    } else {
      tool_message_id = next_message_id_();
    }
    const bool is_mcp_tool = ::yac::mcp::IsMcpToolName(tool_request.name);
    auto prepared = MakeFallbackPreparedToolCall(tool_request);
    std::string preparation_error;
    try {
      if (is_mcp_tool) {
        if (chat_service_mcp_ == nullptr) {
          throw std::invalid_argument(
              "MCP tool requested but MCP is unavailable: " +
              tool_request.name);
        }
        prepared = chat_service_mcp_->PrepareMcpToolCall(tool_request);
      } else {
        prepared = prepare_built_in_tool_call_(tool_request);
      }
    } catch (const std::exception& error) {
      preparation_error = error.what();
    }
    prepared.card_message_id = tool_message_id;
    emit_event_(
        ChatEvent{ToolCallStartedEvent{.message_id = tool_message_id,
                                       .role = ChatRole::Tool,
                                       .tool_call_id = tool_request.id,
                                       .tool_name = tool_request.name,
                                       .tool_call = prepared.preview,
                                       .status = ChatMessageStatus::Active}});

    bool approved = true;
    if (preparation_error.empty() && prepared.requires_approval) {
      std::unique_lock<std::mutex> gate_lock;
      if (approval_gate_ != nullptr) {
        gate_lock = std::unique_lock<std::mutex>(*approval_gate_);
        if (stop_token.stop_requested()) {
          return;
        }
      }
      auto approval_id = tool_approval_->BeginPendingApproval();
      prepared.approval_id = approval_id;
      std::string question;
      std::vector<std::string> options;
      if (const auto* ask_user =
              std::get_if<::yac::tool_call::AskUserCall>(&prepared.preview);
          ask_user != nullptr) {
        question = ask_user->question;
        options = ask_user->options;
      }
      emit_event_(ChatEvent{
          ToolApprovalRequestedEvent{.message_id = tool_message_id,
                                     .role = ChatRole::Tool,
                                     .text = prepared.approval_prompt,
                                     .tool_call_id = tool_request.id,
                                     .tool_name = tool_request.name,
                                     .approval_id = approval_id,
                                     .tool_call = prepared.preview,
                                     .status = ChatMessageStatus::Queued,
                                     .question = std::move(question),
                                     .options = std::move(options)}});
      if (tool_request.name != ::yac::tool_call::kAskUserToolName) {
        auto resolution =
            tool_approval_->WaitForResolution(approval_id, stop_token);
        approved = resolution.approved;
      }
    }

    ::yac::tool_call::ToolExecutionResult result;
    if (!preparation_error.empty()) {
      result = MakeErrorToolResult(prepared.preview, preparation_error);
    } else if (!approved) {
      result = MakeRejectedToolResult(prepared);
    } else if (is_mcp_tool) {
      try {
        result = chat_service_mcp_->ExecuteMcpToolCall(prepared, stop_token);
      } catch (const std::exception& error) {
        result = MakeErrorToolResult(prepared.preview, error.what());
      }
    } else {
      result = execute_built_in_tool_call_(prepared, stop_token);
    }

    // For a background sub_agent call, the tool itself has "completed" (the
    // spawn succeeded) but the sub-agent session is still running. Keep the
    // card Active so the spinner stays until SubAgentCompleted fires.
    ChatMessageStatus done_status = result.is_error
                                        ? ChatMessageStatus::Error
                                        : ChatMessageStatus::Complete;
    if (const auto* sub =
            std::get_if<::yac::tool_call::SubAgentCall>(&result.block);
        sub != nullptr && !result.is_error &&
        sub->status == ::yac::tool_call::SubAgentStatus::Running) {
      done_status = ChatMessageStatus::Active;
    }

    emit_event_(ChatEvent{ToolCallDoneEvent{.message_id = tool_message_id,
                                            .role = ChatRole::Tool,
                                            .tool_call_id = tool_request.id,
                                            .tool_name = tool_request.name,
                                            .tool_call = result.block,
                                            .status = done_status}});
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
