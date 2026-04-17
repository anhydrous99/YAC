#include "presentation/markdown/parser.hpp"
#include "presentation/message.hpp"
#include "presentation/message_renderer.hpp"

#include <algorithm>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

std::string RenderMessageToString(const Message& msg, int width = 80,
                                  int height = 24, int thinking_frame = 0) {
  auto elem = MessageRenderer::Render(
      msg,
      RenderContext{.terminal_width = width, .thinking_frame = thinking_frame});
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

std::string RenderMessageToString(const Message& msg, MessageRenderCache& cache,
                                  int width = 80, int height = 24,
                                  int thinking_frame = 0) {
  auto elem = MessageRenderer::Render(
      msg, cache,
      RenderContext{.terminal_width = width, .thinking_frame = thinking_frame});
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

std::string StripAnsi(const std::string& s) {
  static const std::regex ansi("\x1b\\[[^A-Za-z]*[A-Za-z]");
  return std::regex_replace(s, ansi, "");
}

std::vector<std::string> Lines(const std::string& s) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= s.size()) {
    auto pos = s.find('\n', start);
    if (pos == std::string::npos) {
      pos = s.size();
    }
    auto line = s.substr(start, pos - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(std::move(line));
    if (pos == s.size()) {
      break;
    }
    start = pos + 1;
  }
  return lines;
}

std::pair<size_t, size_t> ContentColumns(const std::string& rendered) {
  auto lines = Lines(StripAnsi(rendered));
  auto left_edge = std::string::npos;
  size_t right_edge = 0;

  for (const auto& line : lines) {
    auto left = line.find_first_not_of(' ');
    if (left == std::string::npos) {
      continue;
    }
    auto right = line.find_last_not_of(' ');
    left_edge = std::min(left_edge, left);
    right_edge = std::max(right_edge, right);
  }

  REQUIRE(left_edge != std::string::npos);
  return {left_edge, right_edge};
}

size_t ContentWidth(const std::string& rendered) {
  auto [left_edge, right_edge] = ContentColumns(rendered);
  return right_edge - left_edge + 1;
}

void RequireNoBoxGlyphs(const std::string& rendered) {
  auto output = StripAnsi(rendered);
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╭"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╮"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╰"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╯"));
}

}  // namespace

TEST_CASE("Render user message contains content") {
  Message msg{Sender::User, "Hello world"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Hello"));
}

TEST_CASE("Render user message contains label") {
  Message msg{Sender::User, "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("You"));
}

TEST_CASE("Render agent message contains content") {
  Message msg{Sender::Agent, "Hi there"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Hi"));
}

TEST_CASE("Render agent message contains label") {
  Message msg{Sender::Agent, "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Assistant"));
}

TEST_CASE("Render agent message surface hugs short content") {
  Message msg{Sender::Agent, "short"};
  msg.created_at = std::chrono::system_clock::time_point{};

  auto [left_edge, right_edge] =
      ContentColumns(RenderMessageToString(msg, 60, 8));
  REQUIRE(left_edge == 2);
  REQUIRE(right_edge < 59);
}

TEST_CASE("Render message surface max width follows terminal width") {
  std::string long_text;
  for (int i = 0; i < 80; ++i) {
    long_text += "word ";
  }

  Message msg{Sender::Agent, long_text};
  SECTION("agent message") {
    msg.sender = Sender::Agent;
  }
  SECTION("user message") {
    msg.sender = Sender::User;
  }
  msg.created_at = std::chrono::system_clock::time_point{};

  auto narrow_width = ContentWidth(RenderMessageToString(msg, 60, 10));
  auto wide_width = ContentWidth(RenderMessageToString(msg, 120, 10));

  REQUIRE(wide_width > narrow_width);
  REQUIRE(wide_width > 80);
}

TEST_CASE("Render messages avoid decorative box glyphs") {
  Message msg{Sender::Agent, "borderless"};
  msg.created_at = std::chrono::system_clock::time_point{};

  RequireNoBoxGlyphs(RenderMessageToString(msg, 80, 8));
}

TEST_CASE("Render agent message wraps in a narrow terminal") {
  Message msg{Sender::Agent,
              "one two three four five six seven eight nine ten"};
  msg.created_at = std::chrono::system_clock::time_point{};

  auto output = StripAnsi(RenderMessageToString(msg, 32, 12));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("nine ten"));
}

TEST_CASE("Render user message wraps in a narrow terminal") {
  Message msg{Sender::User, "one two three four five six seven eight nine ten"};
  msg.created_at = std::chrono::system_clock::time_point{};

  auto output = StripAnsi(RenderMessageToString(msg, 32, 12));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("nine ten"));
}

TEST_CASE("Render active agent message shows thinking indicator") {
  Message msg{Sender::Agent, ""};
  msg.status = MessageStatus::Active;

  auto output = StripAnsi(RenderMessageToString(msg));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("thinking"));
}

TEST_CASE("Render active agent message animates thinking pulse") {
  Message msg{Sender::Agent, ""};
  msg.status = MessageStatus::Active;
  msg.created_at = std::chrono::system_clock::time_point{};

  auto frame0 = StripAnsi(RenderMessageToString(msg, 80, 24, 0));
  auto frame5 = StripAnsi(RenderMessageToString(msg, 80, 24, 5));

  REQUIRE_THAT(frame0, Catch::Matchers::ContainsSubstring("thinking"));
  REQUIRE(frame0 != frame5);
}

TEST_CASE("Render complete agent message hides thinking indicator") {
  Message msg{Sender::Agent, "done"};
  msg.status = MessageStatus::Complete;

  auto output = StripAnsi(RenderMessageToString(msg));

  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("thinking"));
}

TEST_CASE("Render active agent message with text shows stream cursor") {
  Message msg{Sender::Agent, "partial"};
  msg.status = MessageStatus::Active;

  auto output = StripAnsi(RenderMessageToString(msg));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x96\x8e"));
}

TEST_CASE("Stream cursor stays inline with the last streamed text") {
  Message msg{Sender::Agent, "partial"};
  msg.status = MessageStatus::Active;

  auto lines = Lines(StripAnsi(RenderMessageToString(msg)));

  size_t text_line = std::string::npos;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].find("partial") != std::string::npos) {
      text_line = i;
      break;
    }
  }

  REQUIRE(text_line != std::string::npos);
  REQUIRE_THAT(lines[text_line],
               Catch::Matchers::ContainsSubstring("\xe2\x96\x8e"));
}

TEST_CASE("Render error agent message hides active indicator") {
  Message msg{Sender::Agent, "failed"};
  msg.status = MessageStatus::Error;

  auto output = StripAnsi(RenderMessageToString(msg));

  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("thinking"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("\xe2\x96\x8e"));
}

TEST_CASE("Render agent message with markdown") {
  Message msg{Sender::Agent, "# Title\n\nSome **bold** text"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Title"));
}

TEST_CASE("RenderAll with multiple messages") {
  std::vector<Message> messages = {
      {Sender::User, "first"},
      {Sender::Agent, "second"},
  };
  auto elem = MessageRenderer::RenderAll(messages, 80);
  ftxui::Screen screen(80, 20);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("first"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("second"));
}

TEST_CASE("RenderAll with single message") {
  std::vector<Message> messages = {
      {Sender::User, "only"},
  };
  auto elem = MessageRenderer::RenderAll(messages, 80);
  REQUIRE(elem != nullptr);
}

TEST_CASE("RenderAll with empty list") {
  std::vector<Message> messages;
  auto elem = MessageRenderer::RenderAll(messages, 80);
  REQUIRE(elem != nullptr);
}

TEST_CASE("Render user message with custom label") {
  Message msg{Sender::User, "content", "CustomName", ""};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("CustomName"));
}

TEST_CASE("Render agent message uses cached_blocks when available") {
  Message msg{Sender::Agent, "# Cached"};
  MessageRenderCache cache;
  cache.markdown_blocks = markdown::MarkdownParser::Parse(msg.Text());
  auto output = RenderMessageToString(msg, cache);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Cached"));
}

TEST_CASE("Render agent message falls back to parsing without cache") {
  Message msg{Sender::Agent, "# Fallback"};
  MessageRenderCache cache;
  REQUIRE_FALSE(cache.markdown_blocks.has_value());
  auto output = RenderMessageToString(msg, cache);
  REQUIRE(cache.markdown_blocks.has_value());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Fallback"));
}

TEST_CASE("Render unknown sender message uses fallback text") {
  Message msg{static_cast<Sender>(99), "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Unknown Sender"));
}

TEST_CASE("RenderHeader shows avatar role indicator for user") {
  Message msg{Sender::User, "test"};
  auto output = StripAnsi(RenderMessageToString(msg));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x97\x8f"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("You"));
}

TEST_CASE("RenderHeader shows avatar role indicator for agent") {
  Message msg{Sender::Agent, "test"};
  auto output = StripAnsi(RenderMessageToString(msg));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x97\x86"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Assistant"));
}

TEST_CASE("RenderHeader shows relative timestamp") {
  Message msg{Sender::User, "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("just now"));
}

TEST_CASE("RenderHeader hides timestamp for zero time_point") {
  Message msg{Sender::User, "test"};
  msg.created_at = std::chrono::system_clock::time_point{};
  auto output = StripAnsi(RenderMessageToString(msg, 120, 10));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("You"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("ago"));
}

TEST_CASE("Render populates cached_element after first call") {
  Message msg{Sender::User, "cache test"};
  MessageRenderCache cache;
  REQUIRE_FALSE(cache.element.has_value());
  REQUIRE(cache.terminal_width == -1);
  auto elem =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 80});
  REQUIRE(elem != nullptr);
  REQUIRE(cache.element.has_value());
  REQUIRE(cache.terminal_width == 80);
}

TEST_CASE("Render caches active agent message once text is streaming") {
  Message msg{Sender::Agent, "partial"};
  msg.status = MessageStatus::Active;
  MessageRenderCache cache;

  auto elem =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 80});

  REQUIRE(elem != nullptr);
  REQUIRE(cache.element.has_value());
  REQUIRE(cache.terminal_width == 80);
}

TEST_CASE("Render returns cached element on second call at same width") {
  Message msg{Sender::User, "cache hit test"};
  MessageRenderCache cache;
  auto elem1 =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 80});
  auto elem2 =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 80});
  REQUIRE(elem1 != nullptr);
  REQUIRE(elem2 != nullptr);
  REQUIRE(cache.terminal_width == 80);
  ftxui::Screen screen1(80, 10);
  ftxui::Screen screen2(80, 10);
  ftxui::Render(screen1, elem1);
  ftxui::Render(screen2, elem2);
  REQUIRE(screen1.ToString() == screen2.ToString());
}

TEST_CASE("Render invalidates cache when width changes") {
  Message msg{Sender::User, "resize test"};
  MessageRenderCache cache;
  auto elem1 =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 80});
  REQUIRE(elem1 != nullptr);
  REQUIRE(cache.terminal_width == 80);
  auto elem2 =
      MessageRenderer::Render(msg, cache, RenderContext{.terminal_width = 40});
  REQUIRE(elem2 != nullptr);
  REQUIRE(cache.terminal_width == 40);
  REQUIRE(cache.element.has_value());
}

TEST_CASE("Visual output identical with and without cache") {
  Message msg{Sender::Agent, "# Heading\n\nSome **bold** text"};
  MessageRenderCache parsed_cache;
  parsed_cache.markdown_blocks = markdown::MarkdownParser::Parse(msg.Text());
  auto elem1 = MessageRenderer::Render(msg, parsed_cache,
                                       RenderContext{.terminal_width = 80});
  ftxui::Screen screen1(80, 20);
  ftxui::Render(screen1, elem1);
  MessageRenderCache empty_cache;
  auto elem2 = MessageRenderer::Render(msg, empty_cache,
                                       RenderContext{.terminal_width = 80});
  ftxui::Screen screen2(80, 20);
  ftxui::Render(screen2, elem2);
  REQUIRE(screen1.ToString() == screen2.ToString());
}
