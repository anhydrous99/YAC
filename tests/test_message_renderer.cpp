#include "presentation/message.hpp"
#include "presentation/message_renderer.hpp"

#include <regex>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

std::string RenderMessageToString(const Message& msg, int width = 80,
                                  int height = 24) {
  auto elem = MessageRenderer::Render(msg);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

std::string StripAnsi(const std::string& s) {
  static const std::regex ansi("\x1b\\[[^A-Za-z]*[A-Za-z]");
  return std::regex_replace(s, ansi, "");
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
  auto elem = MessageRenderer::RenderAll(messages);
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
  auto elem = MessageRenderer::RenderAll(messages);
  REQUIRE(elem != nullptr);
}

TEST_CASE("RenderAll with empty list") {
  std::vector<Message> messages;
  auto elem = MessageRenderer::RenderAll(messages);
  REQUIRE(elem != nullptr);
}

TEST_CASE("Render user message with custom label") {
  Message msg{Sender::User, "content", "CustomName", ""};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("CustomName"));
}

TEST_CASE("Render agent message uses cached_blocks when available") {
  Message msg{Sender::Agent, "# Cached"};
  msg.cached_blocks = markdown::MarkdownParser::Parse(msg.content);
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Cached"));
}

TEST_CASE("Render agent message falls back to parsing without cache") {
  Message msg{Sender::Agent, "# Fallback"};
  REQUIRE_FALSE(msg.cached_blocks.has_value());
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Fallback"));
}

TEST_CASE("RenderHeader shows bracket-initial role indicator for user") {
  Message msg{Sender::User, "test"};
  auto output = StripAnsi(RenderMessageToString(msg));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[Y]"));
}

TEST_CASE("RenderHeader shows bracket-initial role indicator for agent") {
  Message msg{Sender::Agent, "test"};
  auto output = StripAnsi(RenderMessageToString(msg));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[A]"));
}

TEST_CASE("RenderHeader shows relative timestamp") {
  Message msg{Sender::User, "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("just now"));
}

TEST_CASE("RenderHeader shows middle dot separator before timestamp") {
  Message msg{Sender::User, "test"};
  auto output = RenderMessageToString(msg);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("·"));
}

TEST_CASE("RenderHeader hides timestamp for zero time_point") {
  Message msg{Sender::User, "test"};
  msg.created_at = std::chrono::system_clock::time_point{};
  auto output = StripAnsi(RenderMessageToString(msg, 120, 10));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[Y]"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("ago"));
}
