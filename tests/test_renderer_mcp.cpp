#include "core_types/typed_ids.hpp"
#include "presentation/tool_call/renderer.hpp"
#include "tool_call/types.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::tool_call;
using namespace yac::tool_call;
using yac::McpServerId;

namespace {

std::string StripAnsi(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\x1b' && i + 1 < in.size() && in[i + 1] == '[') {
      i += 2;
      while (i < in.size() && in[i] != 'm') {
        ++i;
      }
      continue;
    }
    out.push_back(in[i]);
  }
  return out;
}

std::string RenderToString(const ToolCallBlock& block, int width = 80,
                           int height = 24) {
  auto elem = ToolCallRenderer::Render(block);
  REQUIRE(elem != nullptr);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return StripAnsi(screen.ToString());
}

}  // namespace

TEST_CASE("mcp_tool_call_text_and_link") {
  McpToolCall call{
      .server_id = McpServerId{"github"},
      .tool_name = "search_repos",
      .original_tool_name = "search_repos",
      .arguments_json = R"({"query": "yac"})",
      .result_blocks = {{.kind = McpResultBlockKind::Text,
                         .text = "Found 3 repos matching **yac**",
                         .mime_type = "",
                         .uri = "",
                         .name = "",
                         .bytes = 0},
                        {.kind = McpResultBlockKind::ResourceLink,
                         .text = "",
                         .mime_type = "",
                         .uri = "https://api.github.com/repos/example/yac",
                         .name = "example/yac",
                         .bytes = 0}},
      .is_error = false,
      .error = "",
      .is_truncated = false,
      .result_bytes = 256,
  };

  auto output = RenderToString(call, 80, 12);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[MCP: github]"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("search_repos"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Found 3 repos"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("yac"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("example/yac"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "https://api.github.com/repos/example/yac"));
}

TEST_CASE("mcp_image_placeholder") {
  // A large base64 string that should NOT appear in the output.
  std::string fake_base64(500, 'A');

  McpToolCall call{
      .server_id = McpServerId{"images"},
      .tool_name = "generate",
      .original_tool_name = "generate",
      .arguments_json = "{}",
      .result_blocks = {{.kind = McpResultBlockKind::Image,
                         .text = fake_base64,
                         .mime_type = "image/png",
                         .uri = "",
                         .name = "",
                         .bytes = 12345}},
      .is_error = false,
      .error = "",
      .is_truncated = false,
      .result_bytes = 12345,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "[image: image/png, 12345 bytes]"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[MCP: images]"));
  // The raw base64 payload must NOT leak into the visible output.
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring(fake_base64));
}

TEST_CASE("mcp_audio_placeholder") {
  McpToolCall call{
      .server_id = McpServerId{"tts"},
      .tool_name = "speak",
      .original_tool_name = "speak",
      .arguments_json = "{}",
      .result_blocks = {{.kind = McpResultBlockKind::Audio,
                         .text = "",
                         .mime_type = "audio/wav",
                         .uri = "",
                         .name = "",
                         .bytes = 4096}},
      .is_error = false,
      .error = "",
      .is_truncated = false,
      .result_bytes = 4096,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "[audio: audio/wav, 4096 bytes]"));
}

TEST_CASE("mcp_embedded_resource_text") {
  McpToolCall call{
      .server_id = McpServerId{"fs"},
      .tool_name = "read_file",
      .original_tool_name = "read_file",
      .arguments_json = "{}",
      .result_blocks = {{.kind = McpResultBlockKind::EmbeddedResource,
                         .text = "Hello **world**",
                         .mime_type = "text/plain",
                         .uri = "file:///tmp/hello.txt",
                         .name = "hello.txt",
                         .bytes = 15}},
      .is_error = false,
      .error = "",
      .is_truncated = false,
      .result_bytes = 15,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Hello"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("world"));
}

TEST_CASE("mcp_embedded_resource_binary") {
  McpToolCall call{
      .server_id = McpServerId{"fs"},
      .tool_name = "read_binary",
      .original_tool_name = "read_binary",
      .arguments_json = "{}",
      .result_blocks = {{.kind = McpResultBlockKind::EmbeddedResource,
                         .text = "",
                         .mime_type = "application/octet-stream",
                         .uri = "file:///tmp/data.bin",
                         .name = "data.bin",
                         .bytes = 8192}},
      .is_error = false,
      .error = "",
      .is_truncated = false,
      .result_bytes = 8192,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "[application/octet-stream, 8192 bytes]"));
}

TEST_CASE("mcp_truncated_indicator") {
  McpToolCall call{
      .server_id = McpServerId{"large"},
      .tool_name = "big_query",
      .original_tool_name = "big_query",
      .arguments_json = "{}",
      .result_blocks = {},
      .is_error = false,
      .error = "",
      .is_truncated = true,
      .result_bytes = 1048576,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("(truncated)"));
}

TEST_CASE("mcp_error_display") {
  McpToolCall call{
      .server_id = McpServerId{"bad"},
      .tool_name = "fail_tool",
      .original_tool_name = "fail_tool",
      .arguments_json = "{}",
      .result_blocks = {},
      .is_error = true,
      .error = "server unreachable",
      .is_truncated = false,
      .result_bytes = 0,
  };

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Error: server unreachable"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[MCP: bad]"));
}
