#include "chat/tool_round_runner.hpp"

#include "chat/agent_mode.hpp"
#include "chat/chat_service_history.hpp"
#include "chat/chat_service_mcp.hpp"
#include "chat/tool_call_argument_parser.hpp"
#include "mcp/tool_naming.hpp"
#include "tool_call/tool_validation_error.hpp"

#include <stdexcept>
#include <utility>

namespace yac::chat::internal {

namespace {

std::string ToolRejectedJson() {
  return R"({"error":"User rejected tool execution."})";
}

std::string PlanModeDeniedMessage(const std::string& tool_name) {
  return "Tool '" + tool_name + "' is not allowed in Plan mode.";
}

std::optional<std::string> PlanModePermissionError(
    const ChatConfig& config, const ToolCallRequest& request) {
  if (config.agent_mode != AgentMode::Plan) {
    return std::nullopt;
  }
  if (!IsToolAllowedForMode(AgentMode::Plan, request.name)) {
    return PlanModeDeniedMessage(request.name);
  }
  return std::nullopt;
}

std::string PartialField(const std::string& arguments_json,
                         const std::string& field) {
  const auto value =
      ::yac::chat::ExtractStringFieldPartial(arguments_json, field);
  return value.has_value() ? *value : std::string{};
}

::yac::tool_call::ToolCallBlock MakeBuiltinFallbackBlock(
    const ::yac::chat::ToolCallRequest& request) {
  namespace tc = ::yac::tool_call;
  const auto& name = request.name;
  const auto& args = request.arguments_json;
  if (name == tc::kFileEditToolName) {
    return tc::FileEditCall{.filepath = PartialField(args, "filepath")};
  }
  if (name == tc::kFileReadToolName) {
    return tc::FileReadCall{.filepath = PartialField(args, "filepath")};
  }
  if (name == tc::kFileWriteToolName) {
    return tc::FileWriteCall{.filepath = PartialField(args, "filepath")};
  }
  if (name == tc::kListDirToolName) {
    return tc::ListDirCall{.path = PartialField(args, "path")};
  }
  if (name == tc::kGrepToolName) {
    return tc::GrepCall{.pattern = PartialField(args, "pattern")};
  }
  if (name == tc::kGlobToolName) {
    return tc::GlobCall{.pattern = PartialField(args, "pattern")};
  }
  if (name == tc::kLspDiagnosticsToolName) {
    return tc::LspDiagnosticsCall{.file_path = PartialField(args, "file_path")};
  }
  if (name == tc::kLspReferencesToolName) {
    return tc::LspReferencesCall{.file_path = PartialField(args, "file_path")};
  }
  if (name == tc::kLspGotoDefinitionToolName) {
    return tc::LspGotoDefinitionCall{.file_path =
                                         PartialField(args, "file_path")};
  }
  if (name == tc::kLspRenameToolName) {
    return tc::LspRenameCall{.file_path = PartialField(args, "file_path"),
                             .new_name = PartialField(args, "new_name")};
  }
  if (name == tc::kLspSymbolsToolName) {
    return tc::LspSymbolsCall{.file_path = PartialField(args, "file_path")};
  }
  if (name == tc::kSubAgentToolName) {
    return tc::SubAgentCall{.task = PartialField(args, "task")};
  }
  if (name == tc::kTodoWriteToolName) {
    return tc::TodoWriteCall{};
  }
  if (name == tc::kAskUserToolName) {
    return tc::AskUserCall{.question = PartialField(args, "question")};
  }
  if (name == tc::kPlanExitToolName) {
    return tc::PlanExitCall{.plan = PartialField(args, "plan")};
  }
  if (name == tc::kBashToolName) {
    return tc::BashCall{.command = PartialField(args, "command"),
                        .is_error = true};
  }
  return tc::BashCall{.command = name, .is_error = true};
}

::yac::tool_call::PreparedToolCall MakeFallbackPreparedToolCall(
    const ::yac::chat::ToolCallRequest& request) {
  if (const auto parsed = ::yac::mcp::SplitMcpToolName(request.name);
      parsed.has_value()) {
    return ::yac::tool_call::PreparedToolCall{
        .request = request,
        .preview = ::yac::tool_call::McpToolCall{
            .server_id = ::yac::McpServerId{parsed->first},
            .tool_name = request.name,
            .original_tool_name = parsed->second,
            .arguments_json = request.arguments_json,
        }};
  }
  return ::yac::tool_call::PreparedToolCall{
      .request = request, .preview = MakeBuiltinFallbackBlock(request)};
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
          ::yac::tool_call::ValidationJson{{"error", std::move(message)}}
              .dump(),
      .is_error = true,
  };
}

}  // namespace

ToolRoundRunner::ToolRoundRunner(PromptRunContext context)
    : tool_approval_(context.tool_approval),
      chat_service_mcp_(context.chat_service_mcp),
      history_mutex_(context.history_mutex),
      history_(context.history),
      emit_event_(std::move(context.emit_event)),
      next_message_id_(std::move(context.next_message_id)),
      config_snapshot_(std::move(context.config_snapshot)),
      generation_value_(std::move(context.generation_value)),
      approval_gate_(context.approval_gate),
      prepare_built_in_tool_call_(
          context.prepare_built_in_tool_call
              ? std::move(context.prepare_built_in_tool_call)
              : ChatServicePromptProcessor::
                    PrepareBuiltInToolCallFn{[](const ToolCallRequest&
                                                    request) {
                      return ::yac::tool_call::ToolExecutor::Prepare(request);
                    }}),
      execute_built_in_tool_call_(
          context.execute_built_in_tool_call
              ? std::move(context.execute_built_in_tool_call)
              : ChatServicePromptProcessor::
                    ExecuteBuiltInToolCallFn{[tool_executor_ptr =
                                                  context.tool_executor](
                                                 const ::yac::tool_call::
                                                     PreparedToolCall& prepared,
                                                 std::stop_token stop_token) {
                      return tool_executor_ptr->Execute(prepared, stop_token);
                    }}),
      on_plan_exit_approved_(std::move(context.on_plan_exit_approved)) {}

void ToolRoundRunner::Run(
    const std::vector<ToolCallRequest>& requested_tools,
    const std::unordered_map<ToolCallId, ChatMessageId>& streaming_card_ids,
    uint64_t generation, std::stop_token stop_token) {
  for (const auto& tool_request : requested_tools) {
    ChatMessageId tool_message_id = 0;
    if (auto it = streaming_card_ids.find(ToolCallId{tool_request.id});
        it != streaming_card_ids.end()) {
      tool_message_id = it->second;
    } else {
      tool_message_id = next_message_id_();
    }
    const bool is_mcp_tool = ::yac::mcp::IsMcpToolName(tool_request.name);
    auto prep = PrepareOneToolCall(tool_request, is_mcp_tool);
    prep.prepared.card_message_id = tool_message_id;
    emit_event_(ChatEvent{
        ToolCallStartedEvent{.message_id = tool_message_id,
                             .role = ChatRole::Tool,
                             .tool_call_id = ToolCallId{tool_request.id},
                             .tool_name = tool_request.name,
                             .tool_call = prep.prepared.preview,
                             .status = ChatMessageStatus::Active}});

    bool approved = true;
    if (!prep.failure.has_value() && prep.prepared.requires_approval) {
      bool gate_aborted = false;
      approved = MaybeAwaitApproval(prep.prepared, tool_request,
                                    tool_message_id, stop_token, gate_aborted);
      if (gate_aborted) {
        return;
      }
    }

    auto result = ExecuteOneToolCall(prep.prepared, prep.failure, approved,
                                     is_mcp_tool, stop_token);

    ChatMessageStatus done_status = result.is_error
                                        ? ChatMessageStatus::Error
                                        : ChatMessageStatus::Complete;
    if (const auto* sub =
            std::get_if<::yac::tool_call::SubAgentCall>(&result.block);
        sub != nullptr && !result.is_error &&
        sub->status == ::yac::tool_call::SubAgentStatus::Running) {
      done_status = ChatMessageStatus::Active;
    }

    emit_event_(
        ChatEvent{ToolCallDoneEvent{.message_id = tool_message_id,
                                    .role = ChatRole::Tool,
                                    .tool_call_id = ToolCallId{tool_request.id},
                                    .tool_name = tool_request.name,
                                    .tool_call = result.block,
                                    .status = done_status}});
    {
      std::scoped_lock lock(*history_mutex_);
      if (ShouldAbortLocked(generation)) {
        return;
      }
      ChatServiceHistory(*history_).AppendToolResult(tool_message_id,
                                                     tool_request, result);
    }

    if (stop_token.stop_requested()) {
      return;
    }
  }
}

ToolRoundRunner::ToolPrep ToolRoundRunner::PrepareOneToolCall(
    const ToolCallRequest& request, bool is_mcp_tool) const {
  ToolPrep prep{.prepared = MakeFallbackPreparedToolCall(request)};
  try {
    if (auto error = PlanModePermissionError(config_snapshot_(), request);
        error.has_value()) {
      prep.failure = ToolPrepFailure{
          .error = *error,
          .result_json =
              ::yac::tool_call::ValidationJson{{"error", *error}}.dump(),
      };
      return prep;
    }
    if (is_mcp_tool) {
      if (chat_service_mcp_ == nullptr) {
        throw std::invalid_argument(
            "MCP tool requested but MCP is unavailable: " + request.name);
      }
      prep.prepared = chat_service_mcp_->PrepareMcpToolCall(request);
    } else {
      prep.prepared = prepare_built_in_tool_call_(request);
    }
  } catch (const ::yac::tool_call::ToolValidationError& error) {
    auto definitions = ::yac::tool_call::ToolExecutor::Definitions();
    if (chat_service_mcp_ != nullptr) {
      definitions = ChatServiceMcp::MergeBuiltInsAndMcp(
          definitions, chat_service_mcp_->BuildToolCatalogSnapshot());
    }
    prep.failure = ToolPrepFailure{
        .error = error.what(),
        .result_json =
            ::yac::tool_call::BuildValidationErrorJson(error, definitions),
    };
  } catch (const std::exception& error) {
    prep.failure = ToolPrepFailure{
        .error = error.what(),
        .result_json = ::yac::tool_call::BuildValidationErrorJson(
            error.what(), request.name, request.arguments_json),
    };
  }
  return prep;
}

bool ToolRoundRunner::MaybeAwaitApproval(
    ::yac::tool_call::PreparedToolCall& prepared,
    const ToolCallRequest& tool_request, ChatMessageId tool_message_id,
    std::stop_token stop_token, bool& gate_aborted) {
  std::unique_lock<std::mutex> gate_lock;
  if (approval_gate_ != nullptr) {
    gate_lock = std::unique_lock<std::mutex>(*approval_gate_);
    if (stop_token.stop_requested()) {
      gate_aborted = true;
      return false;
    }
  }
  auto approval_id = tool_approval_->RequestApproval(
      ToolCallId{tool_request.id}, tool_request.name,
      tool_request.arguments_json);
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
                                 .tool_call_id = ToolCallId{tool_request.id},
                                 .tool_name = tool_request.name,
                                 .approval_id = approval_id,
                                 .tool_call = prepared.preview,
                                 .status = ChatMessageStatus::Queued,
                                 .question = std::move(question),
                                 .options = std::move(options)}});
  if (tool_request.name == ::yac::tool_call::kAskUserToolName) {
    return true;
  }
  return tool_approval_->WaitForResolution(approval_id, stop_token).approved;
}

::yac::tool_call::ToolExecutionResult ToolRoundRunner::ExecuteOneToolCall(
    const ::yac::tool_call::PreparedToolCall& prepared,
    const std::optional<ToolPrepFailure>& failure, bool approved,
    bool is_mcp_tool, std::stop_token stop_token) {
  if (failure.has_value()) {
    auto result = MakeErrorToolResult(prepared.preview, failure->error);
    if (!failure->result_json.empty()) {
      result.result_json = failure->result_json;
    }
    return result;
  }
  if (!approved) {
    return MakeRejectedToolResult(prepared);
  }
  if (auto error =
          PlanModePermissionError(config_snapshot_(), prepared.request);
      error.has_value()) {
    return MakeErrorToolResult(prepared.preview, *error);
  }
  if (prepared.request.name == ::yac::tool_call::kPlanExitToolName &&
      on_plan_exit_approved_) {
    return on_plan_exit_approved_(prepared);
  }
  if (is_mcp_tool) {
    try {
      return chat_service_mcp_->ExecuteMcpToolCall(prepared, stop_token);
    } catch (const std::exception& error) {
      return MakeErrorToolResult(prepared.preview, error.what());
    }
  }
  return execute_built_in_tool_call_(prepared, stop_token);
}

bool ToolRoundRunner::ShouldAbortLocked(uint64_t generation) const {
  return generation_value_() != generation;
}

::yac::tool_call::ToolExecutionResult ToolRoundRunner::MakeRejectedToolResult(
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
