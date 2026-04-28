#include "mock_response_provider.hpp"

#include "chat/types.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

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

MockResponseProvider::MockResponseProvider(std::string script_path,
                                           std::string request_log_path)
    : request_log_path_(std::move(request_log_path)) {
  std::ifstream file(script_path);
  if (!file.is_open()) {
    throw std::runtime_error("MockResponseProvider: cannot open script file: " +
                             script_path);
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    auto j = nlohmann::json::parse(line);
    MockScriptEntry entry;
    entry.on_user_prompt_contains = j.value("on_user_prompt_contains", "");
    entry.emit_text = j.value("emit_text", "");
    entry.finish_reason = j.value("finish_reason", "stop");
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
      for (const auto& tc : j["tool_calls"]) {
        chat::ToolCallRequest req;
        req.id = tc.value("id", "");
        req.name = tc.value("name", "");
        if (tc.contains("arguments")) {
          if (tc["arguments"].is_string()) {
            req.arguments_json = tc["arguments"].get<std::string>();
          } else {
            req.arguments_json = tc["arguments"].dump();
          }
        } else {
          req.arguments_json = "{}";
        }
        entry.tool_calls.push_back(std::move(req));
      }
    }
    entries_.push_back(std::move(entry));
  }
}

void MockResponseProvider::CompleteStream(const chat::ChatRequest& request,
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
    return;
  }

  std::string user_content = FindLastUserMessage(request);

  for (const auto& entry : entries_) {
    if (user_content.find(entry.on_user_prompt_contains) != std::string::npos) {
      if (!entry.emit_text.empty()) {
        sink(chat::ChatEvent{chat::TextDeltaEvent{.text = entry.emit_text}});
      }
      if (entry.finish_reason == "tool_calls" && !entry.tool_calls.empty()) {
        sink(chat::ChatEvent{
            chat::ToolCallRequestedEvent{.tool_calls = entry.tool_calls}});
      }
      return;
    }
  }

  sink(chat::ChatEvent{chat::ErrorEvent{
      .text = "no mock script entry matched user prompt: " + user_content}});
}

}  // namespace yac::provider
