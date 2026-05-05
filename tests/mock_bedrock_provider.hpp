#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace yac::provider {

struct BedrockToolUse {
  std::string id;
  std::string name;
  std::string input_json;
};

struct MockBedrockScriptEntry {
  std::string on_user_prompt_contains;
  std::string text;
  std::vector<BedrockToolUse> tool_uses;
  int input_tokens = 0;
  int output_tokens = 0;
  bool has_usage = false;
  std::string inline_error_type;
  std::string inline_error_message;
  bool has_inline_error = false;
  std::string stop_reason;
  int delay_ms = 0;
};

// MockBedrockProvider reads a JSONL script file at construction time and emits
// Bedrock-shaped ChatEvents without making real AWS API calls.
//
// JSONL schema (one JSON object per non-empty, non-comment line):
//   {
//     "on_user_prompt_contains": "<substr>",
//     "text":         "<canned text response>",
//     "tool_use":     { "id": "<id>", "name": "<name>", "input": {...} },
//     "usage":        { "input_tokens": N, "output_tokens": M },
//     "inline_error": { "type": "<type>", "message": "<msg>" },
//     "stop_reason":  "<reason>",
//     "delay_ms":     N
//   }
//
// First entry whose `on_user_prompt_contains` substring is found in the last
// user message wins. Use `""` as a catch-all.
//
// Optional `request_log_path`: each ChatRequest received by CompleteStream is
// appended as a single JSON line to that file (for later assertion).
class MockBedrockProvider : public LanguageModelProvider {
 public:
  explicit MockBedrockProvider(std::string script_path,
                               std::string request_log_path = "");

  [[nodiscard]] std::string Id() const override { return "bedrock"; }

  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;

 private:
  std::vector<MockBedrockScriptEntry> entries_;
  std::string request_log_path_;
};

}  // namespace yac::provider
