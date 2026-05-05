#pragma once

#include "chat/types.hpp"

#include <string>
#include <vector>

namespace yac::provider {

struct ConverseStreamRequestData;
struct BedrockMessageData;

ConverseStreamRequestData BuildConverseStreamRequest(
    const chat::ChatRequest& request, const chat::ProviderConfig& config);

std::vector<BedrockMessageData> CoalesceToolResults(
    const std::vector<chat::ChatMessage>& messages);

chat::ErrorEvent MapBedrockSyncError(const std::string& error_type,
                                     const std::string& message);

chat::ErrorEvent MapBedrockStreamError(const std::string& error_type,
                                       const std::string& message);

bool IsErrorStopReason(const std::string& stop_reason);

struct ToolConfigData;
struct ToolResultData;

ToolConfigData TranslateToolDefinitions(
    const std::vector<chat::ToolDefinition>& tools);

chat::ToolCallRequest TranslateToolUseToYac(const std::string& tooluse_id,
                                            const std::string& name,
                                            const std::string& input_json);

ToolResultData TranslateYacToolResultToBedrock(
    const chat::ChatMessage& tool_msg);

}  // namespace yac::provider
