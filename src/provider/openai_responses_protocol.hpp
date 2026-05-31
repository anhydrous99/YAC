#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_json.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace yac::provider::openai_responses_protocol {

using Json = ProviderJson;

struct StreamState {
  struct PendingToolCall {
    std::string item_id;
    std::string call_id;
    std::string name;
    std::string arguments_json;
  };

  std::string buffer;
  ChatEventSink* sink = nullptr;
  std::map<int, PendingToolCall> pending_tool_calls;
  std::optional<chat::TokenUsage> pending_usage;
};

[[nodiscard]] std::string ResponsesPath();
[[nodiscard]] Json BuildResponsesPayload(const chat::ChatRequest& request,
                                         const chat::ProviderConfig& config);
[[nodiscard]] chat::ChatEvent ParseStreamData(const std::string& data);
[[nodiscard]] std::optional<chat::TokenUsage> ParseUsageJson(
    const std::string& data);
void FlushPendingToolCalls(StreamState& state, ChatEventSink& sink);
void ConsumeSseChunk(std::string_view chunk, StreamState& state);

}  // namespace yac::provider::openai_responses_protocol
