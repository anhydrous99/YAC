#include "chat/config.hpp"
#include "chat/types.hpp"
#include "config_env_test_helpers.hpp"

#include <algorithm>
#include <array>
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

const yac::chat::ConfigIssue* FindIssueByMessage(
    const std::vector<yac::chat::ConfigIssue>& issues,
    std::string_view message) {
  const auto it =
      std::ranges::find_if(issues, [&](const yac::chat::ConfigIssue& issue) {
        return issue.message == message;
      });
  if (it == issues.end()) {
    return nullptr;
  }
  return &*it;
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

TEST_CASE("env validation wording matches settings semantics") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_env_validation_wording.toml");
  ScopedEnvVar temperature("YAC_TEMPERATURE", "3.0");
  ScopedEnvVar context_window("YAC_CONTEXT_WINDOW", "10000001");
  ScopedEnvVar compact_mode("YAC_COMPACT_MODE", "invalid");
  ScopedEnvVar result_max_bytes("YAC_MCP_RESULT_MAX_BYTES", "0");
  ScopedEnvVar web_search_timeout("YAC_WEB_SEARCH_TIMEOUT_SECONDS", "0");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  struct ExpectedIssue {
    std::string_view message;
    std::string_view detail_fragment;
  };
  constexpr std::array expected = {
      ExpectedIssue{"Invalid YAC_TEMPERATURE",
                    "YAC_TEMPERATURE must be between 0 and 2"},
      ExpectedIssue{"Invalid YAC_CONTEXT_WINDOW",
                    "YAC_CONTEXT_WINDOW must be between 1 and 10000000"},
      ExpectedIssue{"Invalid YAC_COMPACT_MODE",
                    "YAC_COMPACT_MODE must be 'summarize' or 'truncate'"},
      ExpectedIssue{"Invalid YAC_MCP_RESULT_MAX_BYTES",
                    "YAC_MCP_RESULT_MAX_BYTES must be a positive integer"},
      ExpectedIssue{"Invalid YAC_WEB_SEARCH_TIMEOUT_SECONDS",
                    "YAC_WEB_SEARCH_TIMEOUT_SECONDS must be between 1 and 120"},
  };

  for (const auto& expected_issue : expected) {
    INFO(expected_issue.message);
    const auto* issue =
        FindIssueByMessage(result.issues, expected_issue.message);
    REQUIRE(issue != nullptr);
    REQUIRE(issue->severity == yac::chat::ConfigIssueSeverity::Error);
    REQUIRE(issue->detail.find(expected_issue.detail_fragment) !=
            std::string::npos);
  }
  REQUIRE(result.config.temperature == 0.7);
  REQUIRE(result.config.context_window == 0);
  REQUIRE(result.config.auto_compact_mode == "summarize");
  REQUIRE(result.config.mcp.result_max_bytes == 262144);
  REQUIRE(result.config.web_search.timeout_seconds == 25);
}
