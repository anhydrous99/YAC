#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/ContentBlock.h>
#include <aws/bedrock-runtime/model/ConversationRole.h>
#include <aws/bedrock-runtime/model/ConverseStreamRequest.h>
#include <aws/bedrock-runtime/model/InferenceConfiguration.h>
#include <aws/bedrock-runtime/model/Message.h>
#include <aws/bedrock-runtime/model/SystemContentBlock.h>
#include <aws/bedrock-runtime/model/Tool.h>
#include <aws/bedrock-runtime/model/ToolConfiguration.h>
#include <aws/bedrock-runtime/model/ToolInputSchema.h>
#include <aws/bedrock-runtime/model/ToolResultBlock.h>
#include <aws/bedrock-runtime/model/ToolResultContentBlock.h>
#include <aws/bedrock-runtime/model/ToolSpecification.h>
#include <aws/core/utils/Document.h>
#include <stdexcept>

namespace yac::provider {

struct ConverseStreamRequestData {
  Aws::BedrockRuntime::Model::ConverseStreamRequest request;
};

struct BedrockMessageData {
  std::string role;
};

struct ToolConfigData {
  Aws::BedrockRuntime::Model::ToolConfiguration config;
};

struct ToolResultData {
  Aws::BedrockRuntime::Model::ToolResultBlock block;
};

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

  for (const auto& msg : request.messages) {
    if (msg.role == chat::ChatRole::System ||
        msg.role == chat::ChatRole::Tool) {
      continue;
    }

    using Aws::BedrockRuntime::Model::ConversationRole;
    ConversationRole role = msg.role == chat::ChatRole::User
                                ? ConversationRole::user
                                : ConversationRole::assistant;

    Aws::BedrockRuntime::Model::ContentBlock content_block;
    content_block.SetText(msg.content);

    Aws::BedrockRuntime::Model::Message bedrock_msg;
    bedrock_msg.SetRole(role);
    bedrock_msg.AddContent(std::move(content_block));

    data.request.AddMessages(std::move(bedrock_msg));
  }

  Aws::BedrockRuntime::Model::InferenceConfiguration inf_config;
  int max_tokens = 4096;
  if (config.options.count("max_tokens") != 0) {
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
  (void)messages;
  return {};
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

ToolConfigData TranslateToolDefinitions(
    const std::vector<chat::ToolDefinition>& tools) {
  ToolConfigData data;
  for (const auto& def : tools) {
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

}  // namespace yac::provider
