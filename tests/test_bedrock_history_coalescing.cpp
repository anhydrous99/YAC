#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/ContentBlock.h>
#include <aws/bedrock-runtime/model/ConversationRole.h>
#include <aws/bedrock-runtime/model/ToolResultBlock.h>
#include <aws/bedrock-runtime/model/ToolResultContentBlock.h>
#include <aws/bedrock-runtime/model/ToolUseBlock.h>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using Aws::BedrockRuntime::Model::ConversationRole;

namespace {

std::string AwsStr(const Aws::String& s) {
  return std::string(s.c_str(), s.size());
}

}  // namespace

TEST_CASE("CoalesceToolResults: empty input yields empty output") {
  const auto result = CoalesceToolResults({});
  REQUIRE(result.empty());
}

TEST_CASE("CoalesceToolResults: single user message") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "hello world"},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
  const auto& content = result[0].message.GetContent();
  REQUIRE(content.size() == 1);
  REQUIRE(content[0].TextHasBeenSet());
  REQUIRE(AwsStr(content[0].GetText()) == "hello world");
}

TEST_CASE(
    "CoalesceToolResults: user + assistant without tools passes through") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "ping"},
      {.role = ChatRole::Assistant, .content = "pong"},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 2);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
  REQUIRE(result[1].message.GetRole() == ConversationRole::assistant);

  const auto& asst_content = result[1].message.GetContent();
  REQUIRE(asst_content.size() == 1);
  REQUIRE(asst_content[0].TextHasBeenSet());
  REQUIRE(AwsStr(asst_content[0].GetText()) == "pong");
}

TEST_CASE(
    "CoalesceToolResults: single tool result coalesces into user message") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "please run a tool"},
      {.role = ChatRole::Assistant,
       .content = "",
       .tool_calls = {ToolCallRequest{.id = "call-1",
                                      .name = "bash",
                                      .arguments_json = R"({"cmd":"ls"})"}}},
      {.role = ChatRole::Tool,
       .content = "file.txt",
       .tool_call_id = yac::ToolCallId{"call-1"}},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 3);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
  REQUIRE(result[1].message.GetRole() == ConversationRole::assistant);
  REQUIRE(result[2].message.GetRole() == ConversationRole::user);

  const auto& asst_content = result[1].message.GetContent();
  REQUIRE(asst_content.size() == 1);
  REQUIRE(asst_content[0].ToolUseHasBeenSet());
  REQUIRE(AwsStr(asst_content[0].GetToolUse().GetToolUseId()) == "call-1");
  REQUIRE(AwsStr(asst_content[0].GetToolUse().GetName()) == "bash");

  const auto& tool_content = result[2].message.GetContent();
  REQUIRE(tool_content.size() == 1);
  REQUIRE(tool_content[0].ToolResultHasBeenSet());
  REQUIRE(AwsStr(tool_content[0].GetToolResult().GetToolUseId()) == "call-1");
}

TEST_CASE(
    "CoalesceToolResults: multiple tool results coalesced into one message") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "run three tools"},
      {.role = ChatRole::Assistant,
       .content = "",
       .tool_calls =
           {
               ToolCallRequest{.id = "c-a", .name = "bash"},
               ToolCallRequest{.id = "c-b", .name = "bash"},
               ToolCallRequest{.id = "c-c", .name = "bash"},
           }},
      {.role = ChatRole::Tool,
       .content = "out-a",
       .tool_call_id = yac::ToolCallId{"c-a"}},
      {.role = ChatRole::Tool,
       .content = "out-b",
       .tool_call_id = yac::ToolCallId{"c-b"}},
      {.role = ChatRole::Tool,
       .content = "out-c",
       .tool_call_id = yac::ToolCallId{"c-c"}},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 3);
  REQUIRE(result[2].message.GetRole() == ConversationRole::user);

  const auto& content = result[2].message.GetContent();
  REQUIRE(content.size() == 3);
  REQUIRE(content[0].ToolResultHasBeenSet());
  REQUIRE(content[1].ToolResultHasBeenSet());
  REQUIRE(content[2].ToolResultHasBeenSet());
  REQUIRE(AwsStr(content[0].GetToolResult().GetToolUseId()) == "c-a");
  REQUIRE(AwsStr(content[1].GetToolResult().GetToolUseId()) == "c-b");
  REQUIRE(AwsStr(content[2].GetToolResult().GetToolUseId()) == "c-c");
}

TEST_CASE("CoalesceToolResults: mixed messages - only tool run coalesces") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "first"},
      {.role = ChatRole::Assistant, .content = "text response"},
      {.role = ChatRole::User, .content = "now use a tool"},
      {.role = ChatRole::Assistant,
       .content = "",
       .tool_calls = {ToolCallRequest{.id = "t1", .name = "bash"}}},
      {.role = ChatRole::Tool,
       .content = "tool output",
       .tool_call_id = yac::ToolCallId{"t1"}},
      {.role = ChatRole::User, .content = "final"},
      {.role = ChatRole::Assistant, .content = "done"},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 7);
  REQUIRE(result[1].message.GetRole() == ConversationRole::assistant);
  REQUIRE(result[3].message.GetRole() == ConversationRole::assistant);
  REQUIRE(result[4].message.GetRole() == ConversationRole::user);
  const auto& content = result[4].message.GetContent();
  REQUIRE(content.size() == 1);
  REQUIRE(content[0].ToolResultHasBeenSet());
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
  REQUIRE(result[2].message.GetRole() == ConversationRole::user);
  REQUIRE(result[5].message.GetRole() == ConversationRole::user);
}

TEST_CASE("CoalesceToolResults: tool_call_id preserved as toolUseId") {
  const std::string k_id = "very-unique-tool-id-abc123";
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "go"},
      {.role = ChatRole::Assistant,
       .content = "",
       .tool_calls = {ToolCallRequest{.id = k_id, .name = "bash"}}},
      {.role = ChatRole::Tool,
       .content = "my output",
       .tool_call_id = yac::ToolCallId{k_id}},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 3);

  const auto& content = result[2].message.GetContent();
  REQUIRE(content.size() == 1);
  REQUIRE(content[0].ToolResultHasBeenSet());

  const auto& tr = content[0].GetToolResult();
  REQUIRE(AwsStr(tr.GetToolUseId()) == k_id);
  REQUIRE(tr.GetContent().size() == 1);
  REQUIRE(AwsStr(tr.GetContent()[0].GetText()) == "my output");
}

TEST_CASE(
    "CoalesceToolResults: orphan tool result coalesces into user message") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::Tool,
       .content = "orphan data",
       .tool_call_id = yac::ToolCallId{"orphan-id"}},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);

  const auto& content = result[0].message.GetContent();
  REQUIRE(content.size() == 1);
  REQUIRE(content[0].ToolResultHasBeenSet());
  REQUIRE(AwsStr(content[0].GetToolResult().GetToolUseId()) == "orphan-id");
}

TEST_CASE("CoalesceToolResults: system messages are excluded from output") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::System, .content = "You are helpful."},
      {.role = ChatRole::User, .content = "hey"},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 1);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
}

TEST_CASE(
    "CoalesceToolResults: system message between tool results resets "
    "coalescing") {
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::Tool,
       .content = "r1",
       .tool_call_id = yac::ToolCallId{"id1"}},
      {.role = ChatRole::System, .content = "injected system"},
      {.role = ChatRole::Tool,
       .content = "r2",
       .tool_call_id = yac::ToolCallId{"id2"}},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 2);
  REQUIRE(result[0].message.GetRole() == ConversationRole::user);
  REQUIRE(result[1].message.GetRole() == ConversationRole::user);
  REQUIRE(
      AwsStr(
          result[0].message.GetContent()[0].GetToolResult().GetToolUseId()) ==
      "id1");
  REQUIRE(
      AwsStr(
          result[1].message.GetContent()[0].GetToolResult().GetToolUseId()) ==
      "id2");
}

TEST_CASE(
    "CoalesceToolResults: empty assistant message gets empty text block") {
  // Bedrock rejects messages with an empty content array; the implementation
  // inserts a single empty-text ContentBlock to satisfy the API constraint.
  std::vector<ChatMessage> msgs = {
      {.role = ChatRole::User, .content = "hi"},
      {.role = ChatRole::Assistant, .content = ""},
  };
  const auto result = CoalesceToolResults(msgs);
  REQUIRE(result.size() == 2);
  REQUIRE(result[1].message.GetRole() == ConversationRole::assistant);

  const auto& content = result[1].message.GetContent();
  REQUIRE(content.size() == 1);
  REQUIRE(content[0].TextHasBeenSet());
}
