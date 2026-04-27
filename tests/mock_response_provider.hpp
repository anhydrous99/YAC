#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace yac::provider {

struct MockScriptEntry {
  std::string on_user_prompt_contains;
  std::string emit_text;
  std::string finish_reason;
};

// MockResponseProvider reads a JSONL script file at construction time.
// Each line is a JSON object with the schema:
//   { "on_user_prompt_contains": "<substr>",
//     "emit_text": "<canned response>",
//     "finish_reason": "stop" }
// On CompleteStream it scans the request's last user message for a matching
// entry and emits the canned text deltas. If no entry matches an ErrorEvent
// is emitted so the headless runner can surface a clear failure.
//
// Optional `request_log_path`: each ChatRequest received by CompleteStream
// is appended as a single JSON line to that file (for later assertion).
class MockResponseProvider : public LanguageModelProvider {
 public:
  explicit MockResponseProvider(std::string script_path,
                                std::string request_log_path = "");

  [[nodiscard]] std::string Id() const override { return "mock"; }

  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;

 private:
  std::vector<MockScriptEntry> entries_;
  std::string request_log_path_;
};

}  // namespace yac::provider
