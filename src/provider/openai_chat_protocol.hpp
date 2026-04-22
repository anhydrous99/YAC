#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <map>
#include <openai.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::provider::openai_protocol {

using Json = openai::_detail::Json;

struct StreamState {
  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;
  };

  std::string buffer;
  ChatEventSink* sink = nullptr;
  std::map<int, PendingToolCall> pending_tool_calls;
  std::optional<chat::TokenUsage> pending_usage;
};

[[nodiscard]] std::string RoleToOpenAi(chat::ChatRole role);
[[nodiscard]] Json BuildChatPayload(const chat::ChatRequest& request,
                                    bool stream,
                                    const chat::ProviderConfig& config);
[[nodiscard]] std::vector<chat::ModelInfo> ParseModelsData(
    const std::string& data);
[[nodiscard]] chat::ChatEvent ParseStreamData(const std::string& data);
[[nodiscard]] std::optional<chat::TokenUsage> ParseUsageJson(
    const std::string& data);
[[nodiscard]] std::string ExtractBufferedText(const Json& response);
[[nodiscard]] std::optional<chat::TokenUsage> ExtractBufferedUsage(
    const Json& response);
[[nodiscard]] std::vector<chat::ToolCallRequest> ExtractBufferedToolCalls(
    const Json& response);
void ConsumeSseChunk(std::string_view chunk, StreamState& state);

}  // namespace yac::provider::openai_protocol
