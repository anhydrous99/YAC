#include "chat/chat_service_mcp.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_tool_approval.hpp"
#include "lambda_mock_provider.hpp"
#include "mock_mcp_manager.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/todo_state.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <openai.hpp>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::chat::internal;
using namespace yac::provider;
using namespace yac::tool_call;
using yac::test::MockMcpManager;
using yac::testing::LambdaMockProvider;
using Json = openai::_detail::Json;

namespace {

// Provider that emits one malformed tool_call in round 1 and captures the
// tool-result message in round 2 so the test can assert on the JSON payload
// fed back to the model.
struct CapturedToolResult {
  bool seen = false;
  std::string content;
  std::string tool_name;
};

std::shared_ptr<LambdaMockProvider> MakeMalformedToolProvider(
    std::string tool_name, std::string arguments_json,
    std::shared_ptr<CapturedToolResult> captured,
    std::shared_ptr<int> request_count) {
  return std::make_shared<LambdaMockProvider>(
      "test-provider",
      [tool_name = std::move(tool_name),
       arguments_json = std::move(arguments_json), captured,
       request_count](const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) {
        REQUIRE_FALSE(stop_token.stop_requested());
        ++(*request_count);
        if (*request_count == 1) {
          sink(ChatEvent{
              ToolCallRequestedEvent{.tool_calls = {ToolCallRequest{
                                         .id = "call-1",
                                         .name = tool_name,
                                         .arguments_json = arguments_json,
                                     }}}});
          return;
        }
        for (const auto& msg : request.messages) {
          if (msg.role == ChatRole::Tool && msg.tool_call_id == "call-1") {
            captured->seen = true;
            captured->content = msg.content;
            captured->tool_name = msg.tool_name;
            break;
          }
        }
        sink(ChatEvent{TextDeltaEvent{.text = "ack"}});
      });
}

struct Harness {
  explicit Harness(std::shared_ptr<LanguageModelProvider> provider,
                   MockMcpManager* mcp_manager = nullptr)
      : workspace_root(std::filesystem::temp_directory_path() /
                       "yac_prepare_validation_tests"),
        tool_executor(workspace_root, nullptr, todo_state),
        mcp_helper(mcp_manager),
        processor(
            registry, tool_executor, tool_approval,
            mcp_manager != nullptr ? &mcp_helper : nullptr, history_mutex,
            history,
            [this](ChatEvent event) { events.push_back(std::move(event)); },
            [this] { return next_message_id++; }, [this] { return config; },
            [] { return uint64_t{1}; }) {
    std::filesystem::create_directories(workspace_root);
    config.provider_id = provider->Id();
    config.model = "test-model";
    config.workspace_root = workspace_root.string();
    registry.Register(std::move(provider));
  }

  ~Harness() { std::filesystem::remove_all(workspace_root); }
  Harness(const Harness&) = delete;
  Harness& operator=(const Harness&) = delete;
  Harness(Harness&&) = delete;
  Harness& operator=(Harness&&) = delete;

  std::filesystem::path workspace_root;
  ProviderRegistry registry;
  TodoState todo_state;
  ToolExecutor tool_executor;
  ChatServiceToolApproval tool_approval;
  ChatServiceMcp mcp_helper;
  std::mutex history_mutex;
  std::vector<ChatMessage> history;
  std::vector<ChatEvent> events;
  ChatConfig config;
  ChatMessageId next_message_id = 100;
  ChatServicePromptProcessor processor;
};

}  // namespace

TEST_CASE("malformed JSON args yields structured error to model") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider =
      MakeMalformedToolProvider("file_edit", "{not json", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "do an edit", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  REQUIRE(captured->tool_name == "file_edit");
  const auto json = Json::parse(captured->content);
  REQUIRE(json.contains("error"));
  REQUIRE(json["tool_name"] == "file_edit");
  REQUIRE(json["received_arguments"] == "{not json");
  REQUIRE(json.contains("expected_schema"));
  REQUIRE(json["expected_schema"].contains("properties"));
  REQUIRE(json["expected_schema"]["properties"].contains("filepath"));
}

TEST_CASE("missing required field yields structured error") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider = MakeMalformedToolProvider(
      "file_edit", R"({"old_string":"x"})", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "do an edit", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  const auto json = Json::parse(captured->content);
  REQUIRE(json.contains("error"));
  REQUIRE(json["tool_name"] == "file_edit");
  REQUIRE(json["received_arguments"] == R"({"old_string":"x"})");
  REQUIRE(json.contains("expected_schema"));
}

TEST_CASE("wrong type for required field yields structured error") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider = MakeMalformedToolProvider(
      "file_edit", R"({"filepath":42,"old_string":"x","new_string":"y"})",
      captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "do an edit", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  const auto json = Json::parse(captured->content);
  REQUIRE(std::string(json["error"]).find("filepath") != std::string::npos);
  REQUIRE(json["tool_name"] == "file_edit");
  REQUIRE(json.contains("expected_schema"));
}

TEST_CASE("todo_write with malformed args reaches the model with payload") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider =
      MakeMalformedToolProvider("todo_write", "{not json", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "track todos", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  const auto json = Json::parse(captured->content);
  REQUIRE(json["tool_name"] == "todo_write");
  REQUIRE(json["received_arguments"] == "{not json");
  REQUIRE(json.contains("expected_schema"));
  // Critically: no bad_variant_access leaked through.
  REQUIRE(std::string(json["error"]).find("bad variant access") ==
          std::string::npos);
}

TEST_CASE("ask_user with malformed args reaches the model with payload") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider =
      MakeMalformedToolProvider("ask_user", "{not json", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "ask the user", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  const auto json = Json::parse(captured->content);
  REQUIRE(json["tool_name"] == "ask_user");
  REQUIRE(json.contains("expected_schema"));
  REQUIRE(std::string(json["error"]).find("bad variant access") ==
          std::string::npos);
}

TEST_CASE("ToolCallStarted event carries tool-typed preview, not BashCall") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider =
      MakeMalformedToolProvider("file_edit", "{not json", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "edit", 1, std::stop_source{}.get_token());

  bool found = false;
  for (const auto& evt : harness.events) {
    if (const auto* started = evt.As<ToolCallStartedEvent>();
        started != nullptr && started->tool_name == "file_edit") {
      REQUIRE(std::holds_alternative<FileEditCall>(started->tool_call));
      found = true;
      break;
    }
  }
  REQUIRE(found);
}

TEST_CASE("unknown builtin tool yields structured error without crash") {
  auto captured = std::make_shared<CapturedToolResult>();
  auto count = std::make_shared<int>(0);
  auto provider =
      MakeMalformedToolProvider("not_a_real_tool", "{}", captured, count);
  Harness harness(provider);

  harness.processor.ProcessPrompt(1, "call ghost", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(captured->seen);
  const auto json = Json::parse(captured->content);
  REQUIRE(json["tool_name"] == "not_a_real_tool");
  REQUIRE(std::string(json["error"]).find("Unknown tool") != std::string::npos);
  REQUIRE_FALSE(json.contains("expected_schema"));
}
