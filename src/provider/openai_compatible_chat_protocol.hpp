#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <map>
#include <openai.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::provider::openai_compatible_protocol {

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
// Emits a ToolCallRequestedEvent for any fully-formed pending tool calls
// (entries with both id and name populated) and clears the pending buffer.
// Safe to call when nothing is pending — emits nothing in that case. Called
// both from the SSE dispatcher (on any terminating finish_reason) and from
// the provider after the stream closes (backstop for servers that close the
// SSE socket without a finish_reason chunk).
void FlushPendingToolCalls(StreamState& state, ChatEventSink& sink);
void ConsumeSseChunk(std::string_view chunk, StreamState& state);

}  // namespace yac::provider::openai_compatible_protocol
