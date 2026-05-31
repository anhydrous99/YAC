#include "chat/config.hpp"
#include "chat/types.hpp"
#include "config_env_test_helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::testing::ScopedEnvClear;

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    const char* previous = std::getenv(name_.c_str());
    if (previous != nullptr) {
      has_previous_ = true;
      previous_ = previous;
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (has_previous_) {
      setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

 private:
  std::string name_;
  std::string previous_;
  bool has_previous_ = false;
};

class TempFile {
 public:
  explicit TempFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
  }
  ~TempFile() { std::filesystem::remove_all(path_); }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

  void Write(std::string_view content) const {
    std::filesystem::create_directories(path_.parent_path());
    std::ofstream stream(path_, std::ios::trunc);
    stream << content;
  }

 private:
  std::filesystem::path path_;
};

bool HasIssue(const std::vector<yac::chat::ConfigIssue>& issues,
              yac::chat::ConfigIssueSeverity severity,
              std::string_view message) {
  return std::ranges::any_of(issues, [&](const yac::chat::ConfigIssue& issue) {
    return issue.severity == severity && issue.message == message;
  });
}

}  // namespace

TEST_CASE("web_search config defaults are deterministic and disabled") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_defaults.toml");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE_FALSE(result.config.web_search.enabled);
  REQUIRE(result.config.web_search.provider == "exa");
  REQUIRE(result.config.web_search.endpoint == "https://api.exa.ai/search");
  REQUIRE(result.config.web_search.api_key_env == "YAC_EXA_API_KEY");
  REQUIRE(result.config.web_search.api_key.empty());
  REQUIRE(result.config.web_search.timeout_seconds == 25);
  REQUIRE(result.config.web_search.result_limit == 5);
  REQUIRE(result.config.web_search.context_limit == 4096);
  REQUIRE_FALSE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                         "web_search provider is not configured"));
}

TEST_CASE("web_search settings load from TOML and secret env") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_toml.toml");
  file.Write(
      "[web_search]\n"
      "enabled = true\n"
      "provider = \"exa\"\n"
      "endpoint = \"https://search.example.test/exa\"\n"
      "timeout_seconds = 30\n"
      "result_limit = 7\n"
      "context_limit = 9000\n");
  ScopedEnvVar api_key("YAC_EXA_API_KEY", "fake-exa-secret");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.enabled);
  REQUIRE(result.config.web_search.provider == "exa");
  REQUIRE(result.config.web_search.endpoint ==
          "https://search.example.test/exa");
  REQUIRE(result.config.web_search.api_key == "fake-exa-secret");
  REQUIRE(result.config.web_search.timeout_seconds == 30);
  REQUIRE(result.config.web_search.result_limit == 7);
  REQUIRE(result.config.web_search.context_limit == 9000);
  REQUIRE_FALSE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                         "web_search provider is not configured"));
}

TEST_CASE("web_search env overrides enable Exa with endpoint and timeout") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_env.toml");
  file.Write(
      "[web_search]\n"
      "enabled = false\n"
      "endpoint = \"https://toml.example.test/search\"\n"
      "timeout_seconds = 10\n");
  ScopedEnvVar enabled("YAC_WEB_SEARCH_ENABLED", "1");
  ScopedEnvVar provider("YAC_WEB_SEARCH_PROVIDER", "exa");
  ScopedEnvVar endpoint("YAC_EXA_ENDPOINT", "https://env.example.test/search");
  ScopedEnvVar timeout("YAC_WEB_SEARCH_TIMEOUT_SECONDS", "45");
  ScopedEnvVar api_key("YAC_EXA_API_KEY", "fake-exa-secret");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.enabled);
  REQUIRE(result.config.web_search.provider == "exa");
  REQUIRE(result.config.web_search.endpoint ==
          "https://env.example.test/search");
  REQUIRE(result.config.web_search.timeout_seconds == 45);
  REQUIRE(result.config.web_search.api_key == "fake-exa-secret");
}

TEST_CASE("enabled web_search without API key reports deterministic issue") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_missing_key.toml");
  file.Write(
      "[web_search]\n"
      "enabled = true\n");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.enabled);
  REQUIRE(result.config.web_search.api_key.empty());
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "web_search provider is not configured"));
  REQUIRE(std::ranges::none_of(
      result.issues, [](const yac::chat::ConfigIssue& issue) {
        return issue.message.find("fake-exa-secret") != std::string::npos ||
               issue.detail.find("fake-exa-secret") != std::string::npos;
      }));
}

TEST_CASE("web_search provider is fixed to Exa for the MVP") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_invalid_provider.toml");
  file.Write(
      "[web_search]\n"
      "enabled = true\n"
      "provider = \"parallel\"\n");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.provider == "exa");
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "Invalid web_search.provider in settings.toml"));
}

TEST_CASE("web_search result and context limits reject plan schema overflow") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_limit_overflow.toml");
  file.Write(
      "[web_search]\n"
      "result_limit = 11\n"
      "context_limit = 12001\n");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.result_limit == 5);
  REQUIRE(result.config.web_search.context_limit == 4096);
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "Invalid web_search.result_limit in settings.toml"));
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "Invalid web_search.context_limit in settings.toml"));
}

TEST_CASE("web_search env provider and timeout validation are safe") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_web_search_invalid_env.toml");
  ScopedEnvVar provider("YAC_WEB_SEARCH_PROVIDER", "parallel");
  ScopedEnvVar timeout("YAC_WEB_SEARCH_TIMEOUT_SECONDS", "0");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.web_search.provider == "exa");
  REQUIRE(result.config.web_search.timeout_seconds == 25);
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "Invalid YAC_WEB_SEARCH_PROVIDER"));
  REQUIRE(HasIssue(result.issues, yac::chat::ConfigIssueSeverity::Error,
                   "Invalid YAC_WEB_SEARCH_TIMEOUT_SECONDS"));
}
