#include "chat/types.hpp"
#include "mock_bedrock_provider.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

// BEDROCK_SCRIPTS_DIR is a compile-time path injected via CMake
// target_compile_definitions — required, not present at runtime.
#ifndef BEDROCK_SCRIPTS_DIR
#error \
    "BEDROCK_SCRIPTS_DIR must be defined via CMake target_compile_definitions"
#endif

namespace {

struct CollectedEvents {
  std::vector<std::string> text_deltas;
  std::vector<std::string> errors;
  std::vector<std::string> tool_ids_started;
  std::vector<std::string> tool_names_started;
  std::vector<std::string> tool_ids_done;
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
          out.tool_ids_started.push_back(started->tool_call_id);
          out.tool_names_started.push_back(started->tool_name);
        } else if (auto* done = ev.As<ToolCallDoneEvent>()) {
          out.tool_ids_done.push_back(done->tool_call_id);
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

std::string ScriptPath(const char* filename) {
  return std::string(BEDROCK_SCRIPTS_DIR) + "/" + filename;
}

}  // namespace

TEST_CASE("bedrock e2e: text-only response") {
  MockBedrockProvider provider(ScriptPath("converse_text_only.jsonl"));
  auto ev = RunStream(provider, "say hi to me");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  CHECK(ev.text_deltas[0] == "Hello! How can I help you today?");
  REQUIRE(ev.finished);
  CHECK_FALSE(ev.cancelled);
}

TEST_CASE("bedrock e2e: single tool use with argument delta and text") {
  MockBedrockProvider provider(ScriptPath("converse_tool_use_single.jsonl"));
  auto ev = RunStream(provider, "please call tool now");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  CHECK(ev.text_deltas[0] == "Tool called successfully.");
  REQUIRE(ev.tool_ids_started.size() == 1);
  CHECK(ev.tool_ids_started[0] == "tu_single_001");
  REQUIRE(ev.tool_names_started.size() == 1);
  CHECK(ev.tool_names_started[0] == "file_read");
  REQUIRE(ev.argument_deltas.size() == 1);
  CHECK(ev.argument_deltas[0].find("README.md") != std::string::npos);
  REQUIRE(ev.tool_ids_done.size() == 1);
  CHECK(ev.tool_ids_done[0] == "tu_single_001");
  REQUIRE(ev.finished);
}

TEST_CASE("bedrock e2e: multi-tool across 3 turns") {
  MockBedrockProvider provider(ScriptPath("converse_multi_tool.jsonl"));

  auto ev1 = RunStream(provider, "trigger first tool here");
  REQUIRE(ev1.errors.empty());
  REQUIRE(ev1.tool_ids_started.size() == 1);
  CHECK(ev1.tool_ids_started[0] == "tu_001");
  CHECK(ev1.tool_names_started[0] == "file_read");
  REQUIRE(ev1.argument_deltas.size() == 1);
  CHECK(ev1.argument_deltas[0].find("a.txt") != std::string::npos);
  REQUIRE(ev1.tool_ids_done.size() == 1);
  REQUIRE(ev1.finished);

  auto ev2 = RunStream(provider, "trigger second tool now");
  REQUIRE(ev2.errors.empty());
  REQUIRE(ev2.tool_ids_started.size() == 1);
  CHECK(ev2.tool_ids_started[0] == "tu_002");
  CHECK(ev2.argument_deltas[0].find("b.txt") != std::string::npos);
  REQUIRE(ev2.finished);

  auto ev3 = RunStream(provider, "trigger third tool please");
  REQUIRE(ev3.errors.empty());
  REQUIRE(ev3.tool_ids_started.size() == 1);
  CHECK(ev3.tool_ids_started[0] == "tu_003");
  CHECK(ev3.argument_deltas[0].find("c.txt") != std::string::npos);
  REQUIRE(ev3.finished);
}

TEST_CASE("bedrock e2e: throttlingException inline error") {
  MockBedrockProvider provider(ScriptPath("converse_throttle_inline.jsonl"));
  auto ev = RunStream(provider, "any prompt triggers throttle");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  CHECK(ev.errors[0].find("throttlingException") != std::string::npos);
  CHECK(ev.errors[0].find("Rate limit exceeded") != std::string::npos);
  REQUIRE(ev.finished);
  CHECK_FALSE(ev.cancelled);
}

TEST_CASE("bedrock e2e: validationException inline error") {
  MockBedrockProvider provider(ScriptPath("converse_validation_inline.jsonl"));
  auto ev = RunStream(provider, "any prompt triggers validation error");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  CHECK(ev.errors[0].find("validationException") != std::string::npos);
  CHECK(ev.errors[0].find("Invalid request") != std::string::npos);
  REQUIRE(ev.finished);
  CHECK_FALSE(ev.cancelled);
}

TEST_CASE("bedrock e2e: usage metadata reported correctly") {
  MockBedrockProvider provider(ScriptPath("converse_metadata_usage.jsonl"));
  auto ev = RunStream(provider, "any prompt");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  CHECK(ev.text_deltas[0] == "response");
  REQUIRE(ev.has_usage);
  CHECK(ev.prompt_tokens == 42);
  CHECK(ev.completion_tokens == 17);
  REQUIRE(ev.finished);
}

TEST_CASE("bedrock e2e: max_tokens stop_reason does not emit error") {
  MockBedrockProvider provider(
      ScriptPath("converse_stop_reason_max_tokens.jsonl"));
  auto ev = RunStream(provider, "any prompt");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  CHECK(ev.text_deltas[0] == "truncated response");
  REQUIRE(ev.finished);
  CHECK_FALSE(ev.cancelled);
}

TEST_CASE("bedrock e2e: guardrail_intervened stop_reason emits ErrorEvent") {
  MockBedrockProvider provider(
      ScriptPath("converse_stop_reason_guardrail.jsonl"));
  auto ev = RunStream(provider, "any prompt");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  CHECK(ev.errors[0].find("guardrail_intervened") != std::string::npos);
  REQUIRE(ev.finished);
  CHECK_FALSE(ev.cancelled);
}

TEST_CASE("bedrock e2e: stop token pre-requested emits CancelledEvent") {
  MockBedrockProvider provider(ScriptPath("converse_cancel.jsonl"));
  auto ev = RunStream(provider, "any prompt", true);

  CHECK(ev.text_deltas.empty());
  REQUIRE(ev.cancelled);
  CHECK_FALSE(ev.finished);
}
