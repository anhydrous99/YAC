#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/ConverseStreamRequest.h>

namespace yac::provider {

struct ConverseStreamRequestData {
  Aws::BedrockRuntime::Model::ConverseStreamRequest request;
};

struct BedrockMessageData {
  std::string role;
};

ConverseStreamRequestData BuildConverseStreamRequest(
    const chat::ChatRequest& request, const chat::ProviderConfig& config) {
  (void)request;
  (void)config;
  return {};
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

}  // namespace yac::provider
