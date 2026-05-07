#include "chat/types.hpp"
#include "mock_response_provider.hpp"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

constexpr std::string_view kSampleJsonl =
    R"({"on_user_prompt_contains":"hello","emit_text":"Hi there!","finish_reason":"stop"}
{"on_user_prompt_contains":"bye","emit_text":"Goodbye!","finish_reason":"stop"}
{"on_user_prompt_contains":"","emit_text":"Catch-all response.","finish_reason":"stop"}
)";

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
};

CollectedEvents RunStream(MockResponseProvider& provider,
                          const std::string& user_message) {
  ChatRequest req;
  req.provider_id = ::yac::ProviderId{"mock"};
  req.model = ::yac::ModelId{"mock-model"};
  req.messages.push_back(
      ChatMessage{.role = ChatRole::User, .content = user_message});

  CollectedEvents out;
  std::stop_source stop_src;
  provider.CompleteStream(
      req,
      [&](ChatEvent ev) {
        if (auto* td = ev.As<TextDeltaEvent>()) {
          out.text_deltas.push_back(td->text);
        } else if (auto* err = ev.As<ErrorEvent>()) {
          out.errors.push_back(err->text);
        }
      },
      stop_src.get_token());
  return out;
}

}  // namespace

TEST_CASE("MockResponseProvider: matches hello entry") {
  TempFile script("mock_test_hello.jsonl");
  script.Write(kSampleJsonl);

  MockResponseProvider provider(script.Path().string());
  auto ev = RunStream(provider, "say hello world");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  REQUIRE(ev.text_deltas[0] == "Hi there!");
}

TEST_CASE("MockResponseProvider: matches bye entry") {
  TempFile script("mock_test_bye.jsonl");
  script.Write(kSampleJsonl);

  MockResponseProvider provider(script.Path().string());
  auto ev = RunStream(provider, "say bye now");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  REQUIRE(ev.text_deltas[0] == "Goodbye!");
}

TEST_CASE("MockResponseProvider: catch-all matches when no specific entry") {
  TempFile script("mock_test_catchall.jsonl");
  script.Write(kSampleJsonl);

  MockResponseProvider provider(script.Path().string());
  auto ev = RunStream(provider, "something else entirely");

  REQUIRE(ev.errors.empty());
  REQUIRE(ev.text_deltas.size() == 1);
  REQUIRE(ev.text_deltas[0] == "Catch-all response.");
}

TEST_CASE(
    "MockResponseProvider: error when no entry matches and no catch-all") {
  TempFile script("mock_test_nomatch.jsonl");
  script.Write(
      R"({"on_user_prompt_contains":"hello","emit_text":"Hi!","finish_reason":"stop"}
)");

  MockResponseProvider provider(script.Path().string());
  auto ev = RunStream(provider, "something unrelated");

  REQUIRE(ev.text_deltas.empty());
  REQUIRE(ev.errors.size() == 1);
  REQUIRE(!ev.errors[0].empty());
}

TEST_CASE("MockResponseProvider: throws on missing script file") {
  REQUIRE_THROWS_AS(MockResponseProvider("/nonexistent/path/to/script.jsonl"),
                    std::runtime_error);
}

TEST_CASE("MockResponseProvider: request log is written") {
  TempFile script("mock_test_log_script.jsonl");
  script.Write(kSampleJsonl);

  TempFile log_file("mock_test_request_log.jsonl");

  MockResponseProvider provider(script.Path().string(),
                                log_file.Path().string());
  RunStream(provider, "hello there");
  RunStream(provider, "bye now");

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

TEST_CASE("MockResponseProvider: stop token prevents emission") {
  TempFile script("mock_test_stop.jsonl");
  script.Write(kSampleJsonl);

  MockResponseProvider provider(script.Path().string());

  ChatRequest req;
  req.provider_id = ::yac::ProviderId{"mock"};
  req.model = ::yac::ModelId{"mock-model"};
  req.messages.push_back(
      ChatMessage{.role = ChatRole::User, .content = "hello"});

  std::vector<std::string> deltas;
  std::stop_source stop_src;
  stop_src.request_stop();

  provider.CompleteStream(
      req,
      [&](ChatEvent ev) {
        if (auto* td = ev.As<TextDeltaEvent>()) {
          deltas.push_back(td->text);
        }
      },
      stop_src.get_token());

  REQUIRE(deltas.empty());
}

TEST_CASE("MockResponseProvider: id returns mock") {
  TempFile script("mock_test_id.jsonl");
  script.Write(kSampleJsonl);

  MockResponseProvider provider(script.Path().string());
  REQUIRE(provider.Id() == "mock");
}
