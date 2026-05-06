#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/ContentBlock.h>
#include <aws/bedrock-runtime/model/ConversationRole.h>
#include <aws/bedrock-runtime/model/ToolResultBlock.h>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

static ChatRequest MakeRequest(const std::string& model = "test-model") {
  ChatRequest req;
  req.model = model;
  return req;
}

TEST_CASE("IsErrorStopReason returns false for normal stop reasons") {
  SECTION("end_turn") {
    REQUIRE_FALSE(IsErrorStopReason("end_turn"));
  }
  SECTION("tool_use") {
    REQUIRE_FALSE(IsErrorStopReason("tool_use"));
  }
  SECTION("max_tokens") {
    REQUIRE_FALSE(IsErrorStopReason("max_tokens"));
  }
  SECTION("stop_sequence") {
    REQUIRE_FALSE(IsErrorStopReason("stop_sequence"));
  }
}

TEST_CASE("IsErrorStopReason returns true for error stop reasons") {
  SECTION("guardrail_intervened") {
    REQUIRE(IsErrorStopReason("guardrail_intervened"));
  }
  SECTION("content_filtered") {
    REQUIRE(IsErrorStopReason("content_filtered"));
  }
}

TEST_CASE("IsErrorStopReason returns false for unknown or empty stop reason") {
  REQUIRE_FALSE(IsErrorStopReason("some_future_stop_reason"));
  REQUIRE_FALSE(IsErrorStopReason(""));
}

TEST_CASE(
    "MapBedrockSyncError produces ErrorEvent with category-specific prefix") {
  SECTION("ThrottlingException") {
    const auto err =
        MapBedrockSyncError("ThrottlingException", "rate limit hit");
    REQUIRE_FALSE(err.text.empty());
    REQUIRE(err.text.find("throttle") != std::string::npos);
  }
  SECTION("AccessDeniedException") {
    const auto err = MapBedrockSyncError("AccessDeniedException", "denied");
    REQUIRE(err.text.find("access-denied") != std::string::npos);
  }
  SECTION("ValidationException") {
    const auto err = MapBedrockSyncError("ValidationException", "bad input");
    REQUIRE(err.text.find("validation") != std::string::npos);
  }
  SECTION("ModelErrorException") {
    const auto err = MapBedrockSyncError("ModelErrorException", "model error");
    REQUIRE(err.text.find("model-error") != std::string::npos);
  }
  SECTION("ModelNotReadyException") {
    const auto err = MapBedrockSyncError("ModelNotReadyException", "not ready");
    REQUIRE(err.text.find("not-ready") != std::string::npos);
  }
  SECTION("ModelTimeoutException") {
    const auto err = MapBedrockSyncError("ModelTimeoutException", "timed out");
    REQUIRE(err.text.find("timeout") != std::string::npos);
  }
  SECTION("ResourceNotFoundException") {
    const auto err =
        MapBedrockSyncError("ResourceNotFoundException", "missing");
    REQUIRE(err.text.find("not-found") != std::string::npos);
  }
  SECTION("ServiceUnavailableException") {
    const auto err = MapBedrockSyncError("ServiceUnavailableException", "down");
    REQUIRE(err.text.find("unavailable") != std::string::npos);
  }
  SECTION("InternalServerException") {
    const auto err =
        MapBedrockSyncError("InternalServerException", "server error");
    REQUIRE(err.text.find("internal") != std::string::npos);
  }
}

TEST_CASE(
    "MapBedrockSyncError uses generic bedrock-error prefix for unknown types") {
  const auto err = MapBedrockSyncError("SomeUnknownException", "weird error");
  REQUIRE(err.text.find("SomeUnknownException") != std::string::npos);
  REQUIRE(err.text.find("weird error") != std::string::npos);
}

TEST_CASE("MapBedrockSyncError appends original message to error text") {
  const auto err =
      MapBedrockSyncError("ThrottlingException", "please slow down");
  REQUIRE(err.text.find("please slow down") != std::string::npos);
}

TEST_CASE(
    "MapBedrockSyncError produces credential-specific prefixes for new "
    "exception types") {
  SECTION("ExpiredTokenException") {
    const auto err =
        MapBedrockSyncError("ExpiredTokenException", "token expired");
    REQUIRE(err.text.find("expired-token") != std::string::npos);
    REQUIRE(err.text.find("token expired") != std::string::npos);
  }
  SECTION("InvalidSignatureException") {
    const auto err =
        MapBedrockSyncError("InvalidSignatureException", "bad sig");
    REQUIRE(err.text.find("invalid-signature") != std::string::npos);
    REQUIRE(err.text.find("bad sig") != std::string::npos);
  }
  SECTION("UnauthorizedException") {
    const auto err =
        MapBedrockSyncError("UnauthorizedException", "not authorized");
    REQUIRE(err.text.find("unauthorized") != std::string::npos);
    REQUIRE(err.text.find("not authorized") != std::string::npos);
  }
}

TEST_CASE("IsCredentialError returns true for credential-failure exceptions") {
  REQUIRE(IsCredentialError("ExpiredTokenException"));
  REQUIRE(IsCredentialError("InvalidSignatureException"));
  REQUIRE(IsCredentialError("UnauthorizedException"));
  REQUIRE(IsCredentialError("AccessDeniedException"));
}

TEST_CASE("IsCredentialError returns false for non-credential exceptions") {
  REQUIRE_FALSE(IsCredentialError("ThrottlingException"));
  REQUIRE_FALSE(IsCredentialError("ValidationException"));
  REQUIRE_FALSE(IsCredentialError("InternalServerException"));
  REQUIRE_FALSE(IsCredentialError("ResourceNotFoundException"));
  REQUIRE_FALSE(IsCredentialError("ModelErrorException"));
  REQUIRE_FALSE(IsCredentialError(""));
  REQUIRE_FALSE(IsCredentialError("SomeUnknownException"));
}

TEST_CASE(
    "MapBedrockStreamError produces ErrorEvent with stream-specific prefix") {
  SECTION("throttlingException") {
    const auto err = MapBedrockStreamError("throttlingException", "too fast");
    REQUIRE(err.text.find("throttle") != std::string::npos);
  }
  SECTION("internalServerException") {
    const auto err =
        MapBedrockStreamError("internalServerException", "internal err");
    REQUIRE(err.text.find("internal") != std::string::npos);
  }
  SECTION("modelStreamErrorException") {
    const auto err =
        MapBedrockStreamError("modelStreamErrorException", "model err");
    REQUIRE(err.text.find("model-error") != std::string::npos);
  }
  SECTION("serviceUnavailableException") {
    const auto err =
        MapBedrockStreamError("serviceUnavailableException", "down");
    REQUIRE(err.text.find("unavailable") != std::string::npos);
  }
  SECTION("validationException") {
    const auto err = MapBedrockStreamError("validationException", "bad input");
    REQUIRE(err.text.find("validation") != std::string::npos);
  }
}

TEST_CASE(
    "MapBedrockStreamError uses generic prefix for unknown stream error "
    "types") {
  const auto err = MapBedrockStreamError("unknownStreamErr", "some detail");
  REQUIRE(err.text.find("unknownStreamErr") != std::string::npos);
  REQUIRE(err.text.find("some detail") != std::string::npos);
}

TEST_CASE(
    "MapBedrockStreamError appends original message to stream error text") {
  const auto err = MapBedrockStreamError("throttlingException", "back off 5s");
  REQUIRE(err.text.find("back off 5s") != std::string::npos);
}

TEST_CASE("TranslateToolUseToYac maps tooluse_id, name, and input_json") {
  const auto call = TranslateToolUseToYac("tu-abc", "file_read",
                                          R"({"path":"src/main.cpp"})");
  REQUIRE(call.id == "tu-abc");
  REQUIRE(call.name == "file_read");
  REQUIRE(call.arguments_json == R"({"path":"src/main.cpp"})");
}

TEST_CASE("TranslateToolUseToYac preserves empty input_json as-is") {
  const auto call = TranslateToolUseToYac("tu-xyz", "list_dir", "");
  REQUIRE(call.id == "tu-xyz");
  REQUIRE(call.name == "list_dir");
  REQUIRE(call.arguments_json.empty());
}

TEST_CASE("BuildConverseStreamRequest sets model id from ChatRequest") {
  auto req = MakeRequest("anthropic.claude-3-5-haiku-20241022-v1:0");
  const ProviderConfig config;
  const auto data = BuildConverseStreamRequest(req, config);
  REQUIRE(std::string(data.request.GetModelId().c_str()) ==
          "anthropic.claude-3-5-haiku-20241022-v1:0");
}

TEST_CASE(
    "BuildConverseStreamRequest with empty messages has no system block and "
    "no converse messages") {
  const auto data = BuildConverseStreamRequest(MakeRequest(), ProviderConfig{});
  REQUIRE(data.request.GetSystem().empty());
  REQUIRE(data.request.GetMessages().empty());
}

TEST_CASE(
    "BuildConverseStreamRequest extracts system message into the system "
    "block") {
  ChatRequest req = MakeRequest();
  req.messages = {
      ChatMessage{.role = ChatRole::System, .content = "You are helpful"},
      ChatMessage{.role = ChatRole::User, .content = "Hello"},
  };
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});
  REQUIRE(data.request.GetSystem().size() == 1);
  REQUIRE(std::string(data.request.GetSystem()[0].GetText().c_str()) ==
          "You are helpful");
  REQUIRE(data.request.GetMessages().size() == 1);
}

TEST_CASE(
    "BuildConverseStreamRequest concatenates multiple system messages with "
    "newline") {
  ChatRequest req = MakeRequest();
  req.messages = {
      ChatMessage{.role = ChatRole::System, .content = "Part one"},
      ChatMessage{.role = ChatRole::System, .content = "Part two"},
  };
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});
  REQUIRE(data.request.GetSystem().size() == 1);
  const std::string sys_text(data.request.GetSystem()[0].GetText().c_str());
  REQUIRE(sys_text.find("Part one") != std::string::npos);
  REQUIRE(sys_text.find("Part two") != std::string::npos);
  REQUIRE(data.request.GetMessages().empty());
}

TEST_CASE(
    "BuildConverseStreamRequest extracts system block and coalesces tool "
    "messages into a user toolResult message") {
  ChatRequest req = MakeRequest();
  req.messages = {
      ChatMessage{.role = ChatRole::System, .content = "sys"},
      ChatMessage{.role = ChatRole::User, .content = "user msg"},
      ChatMessage{.role = ChatRole::Assistant,
                  .content = "",
                  .tool_calls = {{.id = "tc-1",
                                  .name = "list_dir",
                                  .arguments_json = R"({"path":"."})"}}},
      ChatMessage{
          .role = ChatRole::Tool, .content = "result", .tool_call_id = "tc-1"},
  };
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});

  REQUIRE(data.request.GetSystem().size() == 1);

  const auto& msgs = data.request.GetMessages();
  REQUIRE(msgs.size() == 3);
  REQUIRE(msgs[0].GetRole() ==
          Aws::BedrockRuntime::Model::ConversationRole::user);
  REQUIRE(msgs[1].GetRole() ==
          Aws::BedrockRuntime::Model::ConversationRole::assistant);
  REQUIRE(msgs[2].GetRole() ==
          Aws::BedrockRuntime::Model::ConversationRole::user);

  const auto& tool_result_content = msgs[2].GetContent();
  REQUIRE(tool_result_content.size() == 1);
  REQUIRE(tool_result_content[0].ToolResultHasBeenSet());
  const auto& tool_use_id =
      tool_result_content[0].GetToolResult().GetToolUseId();
  REQUIRE(std::string(tool_use_id.data(), tool_use_id.size()) == "tc-1");
}

TEST_CASE("BuildConverseStreamRequest uses default max_tokens of 4096") {
  const auto data = BuildConverseStreamRequest(MakeRequest(), ProviderConfig{});
  REQUIRE(data.request.GetInferenceConfig().GetMaxTokens() == 4096);
}

TEST_CASE(
    "BuildConverseStreamRequest respects max_tokens override in "
    "ProviderConfig options") {
  ProviderConfig config;
  config.options["max_tokens"] = "8192";
  const auto data = BuildConverseStreamRequest(MakeRequest(), config);
  REQUIRE(data.request.GetInferenceConfig().GetMaxTokens() == 8192);
}

TEST_CASE(
    "BuildConverseStreamRequest sets temperature when request temperature is "
    "positive") {
  ChatRequest req = MakeRequest();
  req.temperature = 0.7;
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});
  REQUIRE(data.request.GetInferenceConfig().TemperatureHasBeenSet());
}

TEST_CASE(
    "BuildConverseStreamRequest does not set temperature when request "
    "temperature is zero") {
  ChatRequest req = MakeRequest();
  req.temperature = 0.0;
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});
  REQUIRE_FALSE(data.request.GetInferenceConfig().TemperatureHasBeenSet());
}

TEST_CASE(
    "BuildConverseStreamRequest includes tool config when tools list is "
    "non-empty") {
  ChatRequest req = MakeRequest();
  req.tools = {ToolDefinition{
      .name = "list_dir",
      .description = "List directory files",
      .parameters_schema_json =
          R"({"type":"object","properties":{"path":{"type":"string"}}})",
  }};
  const auto data = BuildConverseStreamRequest(req, ProviderConfig{});
  REQUIRE(data.request.ToolConfigHasBeenSet());
}

TEST_CASE(
    "BuildConverseStreamRequest has no tool config when tools list is empty") {
  const auto data = BuildConverseStreamRequest(MakeRequest(), ProviderConfig{});
  REQUIRE_FALSE(data.request.ToolConfigHasBeenSet());
}

TEST_CASE("MakeStreamHandler returns a non-null handle") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent e) {
    events.push_back(std::move(e));
  };
  const auto handle = MakeStreamHandler(sink, "bedrock", "test-model");
  REQUIRE(handle != nullptr);
}

TEST_CASE(
    "GetSdkHandler returns a valid reference to the underlying SDK handler") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent e) {
    events.push_back(std::move(e));
  };
  auto handle = MakeStreamHandler(sink, "bedrock", "test-model");
  auto& sdk_handler = GetSdkHandler(handle);
  (void)sdk_handler;
  REQUIRE(handle != nullptr);
}
