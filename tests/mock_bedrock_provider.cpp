#include "mock_bedrock_provider.hpp"

#include "chat/types.hpp"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

namespace yac::provider {

namespace {

std::string FindLastUserMessage(const chat::ChatRequest& request) {
  for (const auto& message : std::ranges::reverse_view(request.messages)) {
    if (message.role == chat::ChatRole::User) {
      return message.content;
    }
  }
  return "";
}

std::string RoleToString(chat::ChatRole role) {
  switch (role) {
    case chat::ChatRole::System:
      return "system";
    case chat::ChatRole::User:
      return "user";
    case chat::ChatRole::Assistant:
      return "assistant";
    case chat::ChatRole::Tool:
      return "tool";
  }
  return "unknown";
}

}  // namespace

MockBedrockProvider::MockBedrockProvider(std::string script_path,
                                         std::string request_log_path)
    : request_log_path_(std::move(request_log_path)) {
  std::ifstream file(script_path);
  if (!file.is_open()) {
    throw std::runtime_error(
        "MockBedrockProvider: cannot open script file: " + script_path);
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    auto j = nlohmann::json::parse(line);
    MockBedrockScriptEntry entry;
    entry.on_user_prompt_contains = j.value("on_user_prompt_contains", "");
    entry.text = j.value("text", "");
    entry.delay_ms = j.value("delay_ms", 0);
    entry.stop_reason = j.value("stop_reason", "");

    if (j.contains("tool_use") && j["tool_use"].is_object()) {
      const auto& tu = j["tool_use"];
      BedrockToolUse tool_use;
      tool_use.id = tu.value("id", "");
      tool_use.name = tu.value("name", "");
      if (tu.contains("input")) {
        tool_use.input_json = tu["input"].is_string()
                                  ? tu["input"].get<std::string>()
                                  : tu["input"].dump();
      } else {
        tool_use.input_json = "{}";
      }
      entry.tool_uses.push_back(std::move(tool_use));
    }

    if (j.contains("usage") && j["usage"].is_object()) {
      entry.input_tokens = j["usage"].value("input_tokens", 0);
      entry.output_tokens = j["usage"].value("output_tokens", 0);
      entry.has_usage = true;
    }

    if (j.contains("inline_error") && j["inline_error"].is_object()) {
      entry.inline_error_type = j["inline_error"].value("type", "");
      entry.inline_error_message = j["inline_error"].value("message", "");
      entry.has_inline_error = true;
    }

    entries_.push_back(std::move(entry));
  }
}

void MockBedrockProvider::CompleteStream(const chat::ChatRequest& request,
                                         ChatEventSink sink,
                                         std::stop_token stop_token) {
  if (!request_log_path_.empty()) {
    nlohmann::json j;
    j["provider_id"] = request.provider_id;
    j["model"] = request.model;
    j["temperature"] = request.temperature;
    auto messages = nlohmann::json::array();
    for (const auto& msg : request.messages) {
      nlohmann::json m;
      m["role"] = RoleToString(msg.role);
      m["content"] = msg.content;
      messages.push_back(std::move(m));
    }
    j["messages"] = std::move(messages);
    auto tools = nlohmann::json::array();
    for (const auto& tool : request.tools) {
      tools.push_back(nlohmann::json{{"name", tool.name}});
    }
    j["tools"] = std::move(tools);
    std::ofstream log(request_log_path_, std::ios::app);
    log << j.dump() << '\n';
  }

  if (stop_token.stop_requested()) {
    sink(chat::ChatEvent{chat::CancelledEvent{}});
    return;
  }

  const std::string user_content = FindLastUserMessage(request);

  for (const auto& entry : entries_) {
    if (user_content.find(entry.on_user_prompt_contains) == std::string::npos) {
      continue;
    }

    if (entry.delay_ms > 0) {
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(entry.delay_ms);
      while (std::chrono::steady_clock::now() < deadline) {
        if (stop_token.stop_requested()) {
          sink(chat::ChatEvent{chat::CancelledEvent{}});
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    if (stop_token.stop_requested()) {
      sink(chat::ChatEvent{chat::CancelledEvent{}});
      return;
    }

    if (!entry.text.empty()) {
      sink(chat::ChatEvent{chat::TextDeltaEvent{
          .text = entry.text,
          .provider_id = request.provider_id,
          .model = request.model,
      }});
    }

    for (const auto& tu : entry.tool_uses) {
      sink(chat::ChatEvent{chat::ToolCallStartedEvent{
          .tool_call_id = tu.id,
          .tool_name = tu.name,
      }});
      sink(chat::ChatEvent{chat::ToolCallArgumentDeltaEvent{
          .tool_call_id = tu.id,
          .tool_name = tu.name,
          .arguments_json = tu.input_json,
      }});
      sink(chat::ChatEvent{chat::ToolCallDoneEvent{
          .tool_call_id = tu.id,
          .tool_name = tu.name,
      }});
    }

    if (entry.has_inline_error) {
      sink(chat::ChatEvent{chat::ErrorEvent{
          .text = "[bedrock-stream-error] " + entry.inline_error_type + ": " +
                  entry.inline_error_message,
          .provider_id = request.provider_id,
          .model = request.model,
      }});
      sink(chat::ChatEvent{chat::FinishedEvent{}});
      return;
    }

    if (!entry.stop_reason.empty()) {
      const bool is_error = entry.stop_reason == "guardrail_intervened" ||
                            entry.stop_reason == "content_filtered";
      if (is_error) {
        sink(chat::ChatEvent{chat::ErrorEvent{
            .text = "[bedrock-stream-error] stopReason: " + entry.stop_reason,
            .provider_id = request.provider_id,
            .model = request.model,
        }});
        sink(chat::ChatEvent{chat::FinishedEvent{}});
        return;
      }
    }

    if (entry.has_usage) {
      chat::TokenUsage usage;
      usage.prompt_tokens = entry.input_tokens;
      usage.completion_tokens = entry.output_tokens;
      usage.total_tokens = entry.input_tokens + entry.output_tokens;
      sink(chat::ChatEvent{chat::UsageReportedEvent{
          .provider_id = request.provider_id,
          .model = request.model,
          .usage = usage,
      }});
    }

    if (stop_token.stop_requested()) {
      sink(chat::ChatEvent{chat::CancelledEvent{}});
    } else {
      sink(chat::ChatEvent{chat::FinishedEvent{}});
    }
    return;
  }

  sink(chat::ChatEvent{chat::ErrorEvent{
      .text = "MockBedrockProvider: no script entry matched user prompt: " +
              user_content,
      .provider_id = request.provider_id,
      .model = request.model,
  }});
  sink(chat::ChatEvent{chat::FinishedEvent{}});
}

}  // namespace yac::provider
