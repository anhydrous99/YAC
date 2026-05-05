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
  return {.text = "[bedrock-" + error_type + "] " + message};
}

chat::ErrorEvent MapBedrockStreamError(const std::string& error_type,
                                       const std::string& message) {
  return {.text = "[bedrock-stream-" + error_type + "] " + message};
}

}  // namespace yac::provider
