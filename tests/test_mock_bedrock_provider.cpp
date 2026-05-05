#include "chat/types.hpp"
#include "mock_bedrock_provider.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

class TempFile {
 public:
  explicit TempFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {}
  ~TempFile() { std::filesystem::remove(path_); }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

  void Write(std::string_view content) {
    std::ofstream f(path_, std::ios::trunc);
    f << content;
  }

  [[nodiscard]] std::string Read() const {
    std::ifstream f(path_);
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
  }

 private:
  std::filesystem::path path_;
};

struct CollectedEvents {
  std::vector<std::string> text_deltas;
  std::vector<std::string> errors;
  std::vector<std::string> tool_call_ids_started;
  std::vector<std::string> tool_call_ids_done;
  std::vector<std::string> argument_deltas;
  bool has_usage = false;
  int prompt_tokens = 0;
  int completion_tokens = 0;
  bool finished = false;
  bool cancelled = false;
};

CollectedEvents RunStream(MockBedrockProvider& provider,
                          const std::string& user_message,
                          bool request_stop = false) {
  ChatRequest req;
  req.provider_id = "bedrock";
  req.model = "anthropic.claude-3-5-haiku-20241022-v1:0";
  req.messages.push_back(
      ChatMessage{.role = ChatRole::User, .content = user_message});

  CollectedEvents out;
  std::stop_source stop_src;
  if (request_stop) {
    stop_src.request_stop();
  }

  provider.CompleteStream(
      req,
      [&](ChatEvent ev) {
        if (auto* td = ev.As<TextDeltaEvent>()) {
          out.text_deltas.push_back(td->text);
        } else if (auto* err = ev.As<ErrorEvent>()) {
          out.errors.push_back(err->text);
        } else if (auto* started = ev.As<ToolCallStartedEvent>()) {
          out.tool_call_ids_started.push_back(started->tool_call_id);
        } else if (auto* done = ev.As<ToolCallDoneEvent>()) {
          out.tool_call_ids_done.push_back(done->tool_call_id);
        } else if (auto* arg = ev.As<ToolCallArgumentDeltaEvent>()) {
          out.argument_deltas.push_back(arg->arguments_json);
        } else if (auto* usage = ev.As<UsageReportedEvent>()) {
          out.has_usage = true;
          out.prompt_tokens = usage->usage.prompt_tokens;
          out.completion_tokens = usage->usage.completion_tokens;
        } else if (ev.As<FinishedEvent>()) {
          out.finished = true;
        } else if (ev.As<CancelledEvent>()) {
          out.cancelled = true;
        }
      },
      stop_src.get_token());
  return out;
}

}  // namespace

TEST_CASE("MockBedrockProvider: id returns bedrock") {
  TempFile script("bedrock_test_id.jsonl");
  script.Write(R"({"on_user_prompt_contains":"","text":"hi"})");

  MockBedrockProvider provider(script.Path().string());
  REQUIRE(provider.Id() == "bedrock");
}

TEST_CASE("MockBedrockProvider: emits TextDeltaEvent on text match") {
  TempFile script("bedrock_test_text.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"hello","text":"Hello from Bedrock!"}
{"on_user_prompt_contains":"","text":"Catch-all."})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "say hello");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  REQUIRE(ev.text_deltas[0] == "Hello from Bedrock!");
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: catch-all matches") {
  TempFile script("bedrock_test_catchall.jsonl");
  script.Write(R"({"on_user_prompt_contains":"","text":"Default response."})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "anything goes here");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  REQUIRE(ev.text_deltas[0] == "Default response.");
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: error when no entry matches") {
  TempFile script("bedrock_test_nomatch.jsonl");
  script.Write(R"({"on_user_prompt_contains":"hello","text":"Hi!"})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "something unrelated");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  REQUIRE(!ev.errors[0].empty());
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: throws on missing script file") {
  REQUIRE_THROWS_AS(MockBedrockProvider("/nonexistent/bedrock_script.jsonl"),
                    std::runtime_error);
}

TEST_CASE("MockBedrockProvider: emits tool call events") {
  TempFile script("bedrock_test_tool.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"list","tool_use":{"id":"tu_001","name":"list_dir","input":{"path":"."}}})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "please list the files");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.tool_call_ids_started.size() == 1);
  REQUIRE(ev.tool_call_ids_started[0] == "tu_001");
  REQUIRE(ev.argument_deltas.size() == 1);
  REQUIRE(ev.argument_deltas[0].find("path") != std::string::npos);
  REQUIRE(ev.tool_call_ids_done.size() == 1);
  REQUIRE(ev.tool_call_ids_done[0] == "tu_001");
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: emits UsageReportedEvent") {
  TempFile script("bedrock_test_usage.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"","text":"ok","usage":{"input_tokens":10,"output_tokens":5}})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "any prompt");

  REQUIRE(ev.has_usage);
  REQUIRE(ev.prompt_tokens == 10);
  REQUIRE(ev.completion_tokens == 5);
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: inline_error emits ErrorEvent") {
  TempFile script("bedrock_test_inline_err.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"","inline_error":{"type":"modelStreamErrorException","message":"stream broke"}})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "trigger error");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  REQUIRE(ev.errors[0].find("modelStreamErrorException") != std::string::npos);
  REQUIRE(ev.errors[0].find("stream broke") != std::string::npos);
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: guardrail stop_reason emits ErrorEvent") {
  TempFile script("bedrock_test_guardrail.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"","stop_reason":"guardrail_intervened"})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "blocked prompt");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  REQUIRE(ev.errors[0].find("guardrail_intervened") != std::string::npos);
  REQUIRE(ev.finished);
}

TEST_CASE(
    "MockBedrockProvider: content_filtered stop_reason emits ErrorEvent") {
  TempFile script("bedrock_test_filtered.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"","stop_reason":"content_filtered"})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "filtered prompt");

  REQUIRE(ev.errors.size() == 1);
  REQUIRE(ev.errors[0].find("content_filtered") != std::string::npos);
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: normal stop_reason does not emit ErrorEvent") {
  TempFile script("bedrock_test_end_turn.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"","text":"done","stop_reason":"end_turn"})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "any");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.finished);
}

TEST_CASE("MockBedrockProvider: stop token emits CancelledEvent") {
  TempFile script("bedrock_test_cancel.jsonl");
  script.Write(R"({"on_user_prompt_contains":"","text":"never"})");

  MockBedrockProvider provider(script.Path().string());
  auto ev = RunStream(provider, "any", true);

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.cancelled);
  REQUIRE(!ev.finished);
}

TEST_CASE("MockBedrockProvider: request log is written") {
  TempFile script("bedrock_test_log_script.jsonl");
  script.Write(R"({"on_user_prompt_contains":"","text":"ok"})");

  TempFile log_file("bedrock_test_request_log.jsonl");

  MockBedrockProvider provider(script.Path().string(),
                               log_file.Path().string());
  RunStream(provider, "first message");
  RunStream(provider, "second message");

  std::string log_content = log_file.Read();
  REQUIRE(!log_content.empty());
  REQUIRE(log_content.find("\"role\"") != std::string::npos);
  REQUIRE(log_content.find("\"user\"") != std::string::npos);

  size_t line_count = 0;
  for (char c : log_content) {
    if (c == '\n') {
      ++line_count;
    }
  }
  REQUIRE(line_count == 2);
}
