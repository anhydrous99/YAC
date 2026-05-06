#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/BedrockRuntimeErrors.h>
#include <aws/bedrock-runtime/model/ContentBlock.h>
#include <aws/bedrock-runtime/model/ConversationRole.h>
#include <aws/bedrock-runtime/model/ConverseStreamHandler.h>
#include <aws/bedrock-runtime/model/ConverseStreamRequest.h>
#include <aws/bedrock-runtime/model/InferenceConfiguration.h>
#include <aws/bedrock-runtime/model/Message.h>
#include <aws/bedrock-runtime/model/StopReason.h>
#include <aws/bedrock-runtime/model/SystemContentBlock.h>
#include <aws/bedrock-runtime/model/Tool.h>
#include <aws/bedrock-runtime/model/ToolConfiguration.h>
#include <aws/bedrock-runtime/model/ToolInputSchema.h>
#include <aws/bedrock-runtime/model/ToolResultBlock.h>
#include <aws/bedrock-runtime/model/ToolResultContentBlock.h>
#include <aws/bedrock-runtime/model/ToolSpecification.h>
#include <aws/core/client/AWSError.h>
#include <aws/core/utils/Document.h>
#include <regex>
#include <stdexcept>
#include <utility>

namespace yac::provider {

ConverseStreamRequestData BuildConverseStreamRequest(
    const chat::ChatRequest& request, const chat::ProviderConfig& config) {
  ConverseStreamRequestData data;
  data.request.SetModelId(request.model);

  std::string system_text;
  for (const auto& msg : request.messages) {
    if (msg.role == chat::ChatRole::System) {
      if (!system_text.empty()) {
        system_text += '\n';
      }
      system_text += msg.content;
    }
  }
  if (!system_text.empty()) {
    Aws::BedrockRuntime::Model::SystemContentBlock sys_block;
    sys_block.SetText(system_text);
    data.request.AddSystem(std::move(sys_block));
  }

  // Coalesce tool messages into Bedrock-shaped user/assistant messages so
  // multi-turn tool calling works. The system prompt was already extracted
  // above; CoalesceToolResults additionally skips ChatRole::System entries.
  std::vector<BedrockMessageData> coalesced =
      CoalesceToolResults(request.messages);
  for (auto& bmd : coalesced) {
    data.request.AddMessages(std::move(bmd.message));
  }

  Aws::BedrockRuntime::Model::InferenceConfiguration inf_config;
  int max_tokens = 4096;
  if (config.options.contains("max_tokens")) {
    max_tokens = std::stoi(config.options.at("max_tokens"));
  }
  inf_config.SetMaxTokens(max_tokens);
  if (request.temperature > 0.0) {
    inf_config.SetTemperature(request.temperature);
  }
  data.request.SetInferenceConfig(std::move(inf_config));

  if (!request.tools.empty()) {
    ToolConfigData tool_config = TranslateToolDefinitions(request.tools);
    data.request.SetToolConfig(std::move(tool_config.config));
  }

  return data;
}

std::vector<BedrockMessageData> CoalesceToolResults(
    const std::vector<chat::ChatMessage>& messages) {
  using Aws::BedrockRuntime::Model::ContentBlock;
  using Aws::BedrockRuntime::Model::ConversationRole;
  using Aws::BedrockRuntime::Model::ToolResultContentBlock;
  using Aws::BedrockRuntime::Model::ToolUseBlock;

  std::vector<BedrockMessageData> out;
  // Index of the in-flight coalesced user-role tool-result message in `out`.
  // -1 means we are not currently coalescing tool results.
  std::ptrdiff_t pending_tool_result_idx = -1;

  for (const auto& msg : messages) {
    // System messages are routed via SystemContentBlock by
    // BuildConverseStreamRequest; do not emit them here.
    if (msg.role == chat::ChatRole::System) {
      pending_tool_result_idx = -1;
      continue;
    }

    if (msg.role == chat::ChatRole::Tool) {
      if (pending_tool_result_idx < 0) {
        BedrockMessageData wrapper;
        wrapper.message.SetRole(ConversationRole::user);
        out.push_back(std::move(wrapper));
        pending_tool_result_idx = static_cast<std::ptrdiff_t>(out.size()) - 1;
      }

      ToolResultContentBlock trc;
      trc.SetText(msg.content);

      Aws::BedrockRuntime::Model::ToolResultBlock trb;
      trb.SetToolUseId(msg.tool_call_id);
      trb.AddContent(std::move(trc));

      ContentBlock cb;
      cb.SetToolResult(std::move(trb));

      out[static_cast<std::size_t>(pending_tool_result_idx)].message.AddContent(
          std::move(cb));
      continue;
    }

    // Non-tool message: any pending tool-result coalescing run ends here.
    pending_tool_result_idx = -1;

    BedrockMessageData data;
    if (msg.role == chat::ChatRole::User) {
      data.message.SetRole(ConversationRole::user);
      ContentBlock cb;
      cb.SetText(msg.content);
      data.message.AddContent(std::move(cb));
    } else {
      data.message.SetRole(ConversationRole::assistant);
      if (!msg.content.empty()) {
        ContentBlock cb;
        cb.SetText(msg.content);
        data.message.AddContent(std::move(cb));
      }
      for (const auto& tc : msg.tool_calls) {
        ToolUseBlock tub;
        tub.SetToolUseId(tc.id);
        tub.SetName(tc.name);

        const std::string args_json =
            tc.arguments_json.empty() ? "{}" : tc.arguments_json;
        Aws::Utils::Document input_doc(args_json);
        if (!input_doc.WasParseSuccessful()) {
          throw std::runtime_error(
              "[bedrock] Failed to parse tool_use input JSON for '" + tc.name +
              "': " + std::string(input_doc.GetErrorMessage()));
        }
        tub.SetInput(std::move(input_doc));

        ContentBlock cb;
        cb.SetToolUse(std::move(tub));
        data.message.AddContent(std::move(cb));
      }
      // Bedrock rejects messages with an empty content array. If an assistant
      // message had no text and no tool_calls, emit a single empty text block
      // to keep the array non-empty.
      if (msg.content.empty() && msg.tool_calls.empty()) {
        ContentBlock cb;
        cb.SetText("");
        data.message.AddContent(std::move(cb));
      }
    }

    out.push_back(std::move(data));
  }

  return out;
}

chat::ErrorEvent MapBedrockSyncError(const std::string& error_type,
                                     const std::string& message) {
  std::string prefix;

  if (error_type == "AccessDeniedException") {
    prefix =
        "[bedrock-access-denied] Check AWS credentials and IAM permissions: ";
  } else if (error_type == "ModelErrorException") {
    prefix = "[bedrock-model-error] Model returned an error: ";
  } else if (error_type == "ModelNotReadyException") {
    prefix = "[bedrock-model-not-ready] Model is not ready, retry later: ";
  } else if (error_type == "ModelTimeoutException") {
    prefix = "[bedrock-model-timeout] Model request timed out: ";
  } else if (error_type == "ResourceNotFoundException") {
    prefix = "[bedrock-not-found] Model or resource not found: ";
  } else if (error_type == "ServiceUnavailableException") {
    prefix = "[bedrock-unavailable] Bedrock service unavailable: ";
  } else if (error_type == "ThrottlingException") {
    prefix = "[bedrock-throttle] Rate limit exceeded, retry after backoff: ";
  } else if (error_type == "ValidationException") {
    prefix = "[bedrock-validation] Request validation failed: ";
  } else if (error_type == "InternalServerException") {
    prefix = "[bedrock-internal] Bedrock internal error: ";
  } else if (error_type == "ExpiredTokenException") {
    prefix = "[bedrock-expired-token] AWS credentials have expired: ";
  } else if (error_type == "InvalidSignatureException") {
    prefix =
        "[bedrock-invalid-signature] AWS request signature is invalid "
        "(credentials may be expired): ";
  } else if (error_type == "UnauthorizedException") {
    prefix = "[bedrock-unauthorized] AWS request unauthorized: ";
  } else {
    prefix = "[bedrock-error] " + error_type + ": ";
  }

  return {.text = prefix + message};
}

chat::ErrorEvent MapBedrockStreamError(const std::string& error_type,
                                       const std::string& message) {
  std::string prefix;

  if (error_type == "internalServerException") {
    prefix = "[bedrock-stream-internal] Stream internal error: ";
  } else if (error_type == "modelStreamErrorException") {
    prefix = "[bedrock-stream-model-error] Model stream error: ";
  } else if (error_type == "serviceUnavailableException") {
    prefix = "[bedrock-stream-unavailable] Stream service unavailable: ";
  } else if (error_type == "throttlingException") {
    prefix = "[bedrock-stream-throttle] Stream rate limited: ";
  } else if (error_type == "validationException") {
    prefix = "[bedrock-stream-validation] Stream validation error: ";
  } else {
    prefix = "[bedrock-stream-error] " + error_type + ": ";
  }

  return {.text = prefix + message};
}

bool IsErrorStopReason(const std::string& stop_reason) {
  if (stop_reason == "end_turn" || stop_reason == "tool_use" ||
      stop_reason == "max_tokens" || stop_reason == "stop_sequence") {
    return false;
  }

  if (stop_reason == "guardrail_intervened" ||
      stop_reason == "content_filtered") {
    return true;
  }

  return false;
}

bool IsCredentialError(const std::string& exception_name) {
  return exception_name == "ExpiredTokenException" ||
         exception_name == "InvalidSignatureException" ||
         exception_name == "UnauthorizedException" ||
         exception_name == "AccessDeniedException";
}

ToolConfigData TranslateToolDefinitions(
    const std::vector<chat::ToolDefinition>& tools) {
  // Bedrock requires tool names to match ^[a-zA-Z0-9_-]+$ and be ≤64 chars.
  // OpenAI accepts a broader set (including '.'), so this check is scoped to
  // the Bedrock translator rather than the cross-provider tool catalog.
  static const std::regex bedrock_tool_name_regex("^[a-zA-Z0-9_-]+$");

  ToolConfigData data;
  for (const auto& def : tools) {
    if (def.name.size() > 64 ||
        !std::regex_match(def.name, bedrock_tool_name_regex)) {
      throw std::runtime_error(
          "[bedrock-validation] Tool name '" + def.name +
          "' violates Bedrock compliance: must match ^[a-zA-Z0-9_-]+$ and be "
          "<=64 chars");
    }

    Aws::Utils::Document schema_doc(def.parameters_schema_json);
    if (!schema_doc.WasParseSuccessful()) {
      throw std::runtime_error(
          "[bedrock] Failed to parse tool schema JSON for '" + def.name +
          "': " + std::string(schema_doc.GetErrorMessage()));
    }

    Aws::BedrockRuntime::Model::ToolInputSchema input_schema;
    input_schema.SetJson(schema_doc);

    Aws::BedrockRuntime::Model::ToolSpecification tool_spec;
    tool_spec.SetName(def.name);
    tool_spec.SetDescription(def.description);
    tool_spec.SetInputSchema(std::move(input_schema));

    Aws::BedrockRuntime::Model::Tool tool;
    tool.SetToolSpec(std::move(tool_spec));

    data.config.AddTools(std::move(tool));
  }
  return data;
}

chat::ToolCallRequest TranslateToolUseToYac(const std::string& tooluse_id,
                                            const std::string& name,
                                            const std::string& input_json) {
  return {.id = tooluse_id, .name = name, .arguments_json = input_json};
}

ToolResultData TranslateYacToolResultToBedrock(
    const chat::ChatMessage& tool_msg) {
  Aws::BedrockRuntime::Model::ToolResultContentBlock content_block;
  content_block.SetText(tool_msg.content);

  Aws::BedrockRuntime::Model::ToolResultBlock result_block;
  result_block.SetToolUseId(tool_msg.tool_call_id);
  result_block.AddContent(std::move(content_block));

  return {.block = std::move(result_block)};
}

namespace {

class BedrockStreamHandler
    : public Aws::BedrockRuntime::Model::ConverseStreamHandler {
 public:
  BedrockStreamHandler(ChatEventSink sink, std::string provider_id,
                       std::string model)
      : sink_(std::move(sink)),
        provider_id_(std::move(provider_id)),
        model_(std::move(model)) {
    SetMessageStartEventCallback(
        [this](const Aws::BedrockRuntime::Model::MessageStartEvent& evt) {
          OnMessageStart(evt);
        });
    SetContentBlockStartEventCallback(
        [this](const Aws::BedrockRuntime::Model::ContentBlockStartEvent& evt) {
          OnContentBlockStart(evt);
        });
    SetContentBlockDeltaEventCallback(
        [this](const Aws::BedrockRuntime::Model::ContentBlockDeltaEvent& evt) {
          OnContentBlockDelta(evt);
        });
    SetContentBlockStopEventCallback(
        [this](const Aws::BedrockRuntime::Model::ContentBlockStopEvent& evt) {
          OnContentBlockStop(evt);
        });
    SetMessageStopEventCallback(
        [this](const Aws::BedrockRuntime::Model::MessageStopEvent& evt) {
          OnMessageStop(evt);
        });
    SetConverseStreamMetadataEventCallback(
        [this](const Aws::BedrockRuntime::Model::ConverseStreamMetadataEvent&
                   evt) { OnMetadata(evt); });
    SetOnErrorCallback(
        [this](const Aws::Client::AWSError<
               Aws::BedrockRuntime::BedrockRuntimeErrors>& error) {
          OnError(error);
        });
  }

  BedrockStreamHandler(const BedrockStreamHandler&) = delete;
  BedrockStreamHandler& operator=(const BedrockStreamHandler&) = delete;
  BedrockStreamHandler(BedrockStreamHandler&&) = delete;
  BedrockStreamHandler& operator=(BedrockStreamHandler&&) = delete;
  ~BedrockStreamHandler() override = default;

 private:
  void OnMessageStart(
      const Aws::BedrockRuntime::Model::MessageStartEvent& /*evt*/) {}

  void OnContentBlockStart(
      const Aws::BedrockRuntime::Model::ContentBlockStartEvent& evt) {
    const auto& start = evt.GetStart();
    if (!start.ToolUseHasBeenSet()) {
      return;
    }
    const auto& tool_use = start.GetToolUse();
    current_tool_use_id_ = tool_use.GetToolUseId();
    current_tool_name_ = tool_use.GetName();
    accumulated_tool_input_.clear();
    in_tool_block_ = true;

    sink_(chat::ChatEvent{chat::ToolCallStartedEvent{
        .tool_call_id = current_tool_use_id_,
        .tool_name = current_tool_name_,
    }});
  }

  void OnContentBlockDelta(
      const Aws::BedrockRuntime::Model::ContentBlockDeltaEvent& evt) {
    const auto& delta = evt.GetDelta();
    if (delta.TextHasBeenSet()) {
      const auto& text = delta.GetText();
      if (text.empty()) {
        return;
      }
      sink_(chat::ChatEvent{chat::TextDeltaEvent{
          .text = std::string(text.c_str(), text.size()),
          .provider_id = provider_id_,
          .model = model_,
      }});
      return;
    }
    if (delta.ToolUseHasBeenSet() && in_tool_block_) {
      const auto& tool_use_delta = delta.GetToolUse();
      if (!tool_use_delta.InputHasBeenSet()) {
        return;
      }
      const auto& fragment = tool_use_delta.GetInput();
      accumulated_tool_input_.append(fragment.c_str(), fragment.size());
      sink_(chat::ChatEvent{chat::ToolCallArgumentDeltaEvent{
          .tool_call_id = current_tool_use_id_,
          .tool_name = current_tool_name_,
          .arguments_json = accumulated_tool_input_,
      }});
    }
  }

  void OnContentBlockStop(
      const Aws::BedrockRuntime::Model::ContentBlockStopEvent& /*evt*/) {
    if (!in_tool_block_) {
      return;
    }
    sink_(chat::ChatEvent{chat::ToolCallDoneEvent{
        .tool_call_id = current_tool_use_id_,
        .tool_name = current_tool_name_,
    }});
    in_tool_block_ = false;
    current_tool_use_id_.clear();
    current_tool_name_.clear();
    accumulated_tool_input_.clear();
  }

  void OnMessageStop(const Aws::BedrockRuntime::Model::MessageStopEvent& evt) {
    using Aws::BedrockRuntime::Model::StopReasonMapper::GetNameForStopReason;
    const Aws::String reason_aws = GetNameForStopReason(evt.GetStopReason());
    const std::string reason(reason_aws.c_str(), reason_aws.size());
    if (IsErrorStopReason(reason)) {
      auto error = MapBedrockStreamError("stopReason", reason);
      error.provider_id = provider_id_;
      error.model = model_;
      sink_(chat::ChatEvent{std::move(error)});
    }
  }

  void OnMetadata(
      const Aws::BedrockRuntime::Model::ConverseStreamMetadataEvent& evt) {
    if (!evt.UsageHasBeenSet()) {
      return;
    }
    const auto& usage = evt.GetUsage();
    chat::TokenUsage token_usage;
    token_usage.prompt_tokens = usage.GetInputTokens();
    token_usage.completion_tokens = usage.GetOutputTokens();
    token_usage.total_tokens = usage.GetTotalTokens();
    if (token_usage.total_tokens == 0) {
      token_usage.total_tokens =
          token_usage.prompt_tokens + token_usage.completion_tokens;
    }
    sink_(chat::ChatEvent{chat::UsageReportedEvent{
        .provider_id = provider_id_,
        .model = model_,
        .usage = token_usage,
    }});
  }

  void OnError(
      const Aws::Client::AWSError<Aws::BedrockRuntime::BedrockRuntimeErrors>&
          error) {
    const auto& name = error.GetExceptionName();
    const auto& message = error.GetMessage();
    auto evt =
        MapBedrockStreamError(std::string(name.c_str(), name.size()),
                              std::string(message.c_str(), message.size()));
    evt.provider_id = provider_id_;
    evt.model = model_;
    sink_(chat::ChatEvent{std::move(evt)});
  }

  ChatEventSink sink_;
  std::string provider_id_;
  std::string model_;
  std::string current_tool_use_id_;
  std::string current_tool_name_;
  std::string accumulated_tool_input_;
  bool in_tool_block_ = false;
};

}  // namespace

struct BedrockStreamHandlerData {
  BedrockStreamHandler handler;
};

void DestroyBedrockStreamHandler(BedrockStreamHandlerData* data) noexcept {
  delete data;  // NOLINT(cppcoreguidelines-owning-memory)
}

BedrockStreamHandlerHandle MakeStreamHandler(const ChatEventSink& sink,
                                             const std::string& provider_id,
                                             const std::string& model) {
  return BedrockStreamHandlerHandle(new BedrockStreamHandlerData{
      .handler = BedrockStreamHandler(sink, provider_id, model)});
}

Aws::BedrockRuntime::Model::ConverseStreamHandler& GetSdkHandler(
    BedrockStreamHandlerHandle& handle) {
  return handle->handler;
}

}  // namespace yac::provider
