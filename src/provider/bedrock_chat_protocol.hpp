#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <aws/bedrock-runtime/model/ConverseStreamHandler.h>
#include <aws/bedrock-runtime/model/ConverseStreamRequest.h>
#include <aws/bedrock-runtime/model/ToolConfiguration.h>
#include <aws/bedrock-runtime/model/ToolResultBlock.h>
#include <memory>
#include <string>
#include <vector>

namespace yac::provider {

struct ConverseStreamRequestData {
  Aws::BedrockRuntime::Model::ConverseStreamRequest request;
};

struct BedrockMessageData {
  Aws::BedrockRuntime::Model::Message message;
};

struct BedrockStreamHandlerData;

void DestroyBedrockStreamHandler(BedrockStreamHandlerData* data) noexcept;

struct BedrockStreamHandlerDeleter {
  void operator()(BedrockStreamHandlerData* data) const noexcept {
    DestroyBedrockStreamHandler(data);
  }
};

using BedrockStreamHandlerHandle =
    std::unique_ptr<BedrockStreamHandlerData, BedrockStreamHandlerDeleter>;

// Creates a per-stream handler that translates Bedrock ConverseStream events
// into ChatEvents and dispatches them to `sink`. The returned handle owns the
// underlying handler; it must outlive any in-flight ConverseStream call that
// references it. Not thread-safe; callers must keep one handle per stream.
BedrockStreamHandlerHandle MakeStreamHandler(const ChatEventSink& sink,
                                             const std::string& provider_id,
                                             const std::string& model);

// Returns a reference to the SDK-side handler bound by `handle`. The reference
// is valid for the lifetime of the handle and is intended for passing to
// ConverseStreamRequest::SetEventStreamHandler at dispatch time.
Aws::BedrockRuntime::Model::ConverseStreamHandler& GetSdkHandler(
    BedrockStreamHandlerHandle& handle);

ConverseStreamRequestData BuildConverseStreamRequest(
    const chat::ChatRequest& request, const chat::ProviderConfig& config);

std::vector<BedrockMessageData> CoalesceToolResults(
    const std::vector<chat::ChatMessage>& messages);

chat::ErrorEvent MapBedrockSyncError(const std::string& error_type,
                                     const std::string& message);

chat::ErrorEvent MapBedrockStreamError(const std::string& error_type,
                                       const std::string& message);

bool IsErrorStopReason(const std::string& stop_reason);

struct ToolConfigData {
  Aws::BedrockRuntime::Model::ToolConfiguration config;
};

struct ToolResultData {
  Aws::BedrockRuntime::Model::ToolResultBlock block;
};

ToolConfigData TranslateToolDefinitions(
    const std::vector<chat::ToolDefinition>& tools);

chat::ToolCallRequest TranslateToolUseToYac(const std::string& tooluse_id,
                                            const std::string& name,
                                            const std::string& input_json);

ToolResultData TranslateYacToolResultToBedrock(
    const chat::ChatMessage& tool_msg);

}  // namespace yac::provider
