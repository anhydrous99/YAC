#include "chat/config.hpp"
#include "chat/types.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

bool HasIssue(const std::vector<yac::chat::ConfigIssue>& issues,
              std::string_view substring) {
  return std::ranges::any_of(
      issues, [&](const yac::chat::ConfigIssue& issue) {
        return issue.message.find(substring) != std::string::npos;
      });
}

constexpr const char* kBedrockToml =
    "[provider]\n"
    "id = \"bedrock\"\n"
    "model = \"anthropic.claude-3-5-haiku-20241022-v1:0\"\n";

}  // namespace

TEST_CASE("bedrock max_tokens defaults to 4096 when unset") {
  TempFile file("yac_test_bedrock_max_tokens_default.toml");
  WriteFile(file.Path(), kBedrockToml);

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.config.options.at("max_tokens") == "4096");
  REQUIRE_FALSE(HasIssue(result.issues, "max_tokens"));
}

TEST_CASE("bedrock max_tokens accepts a valid integer from env") {
  TempFile file("yac_test_bedrock_max_tokens_env.toml");
  WriteFile(file.Path(), kBedrockToml);

  ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "8192");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.config.options.at("max_tokens") == "8192");
  REQUIRE_FALSE(HasIssue(result.issues, "max_tokens"));
}

TEST_CASE("bedrock max_tokens rejects non-numeric values") {
  TempFile file("yac_test_bedrock_max_tokens_alpha.toml");
  WriteFile(file.Path(), kBedrockToml);

  ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "abc");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
  REQUIRE(result.config.options.at("max_tokens") == "4096");
}

TEST_CASE("bedrock max_tokens rejects partially-numeric values") {
  TempFile file("yac_test_bedrock_max_tokens_partial.toml");
  WriteFile(file.Path(), kBedrockToml);

  ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "123abc");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
  REQUIRE(result.config.options.at("max_tokens") == "4096");
}

TEST_CASE("bedrock max_tokens rejects out-of-range values") {
  TempFile file("yac_test_bedrock_max_tokens_oob.toml");
  WriteFile(file.Path(), kBedrockToml);

  SECTION("zero") {
    ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "0");
    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
    REQUIRE(result.config.options.at("max_tokens") == "4096");
  }
  SECTION("negative") {
    ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "-5");
    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
    REQUIRE(result.config.options.at("max_tokens") == "4096");
  }
  SECTION("absurdly large") {
    ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "9999999");
    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
    REQUIRE(result.config.options.at("max_tokens") == "4096");
  }
}

TEST_CASE("non-bedrock provider does not validate max_tokens") {
  TempFile file("yac_test_non_bedrock_max_tokens.toml");
  WriteFile(file.Path(),
            "[provider]\n"
            "id = \"openai\"\n"
            "model = \"gpt-4o\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  // YAC_BEDROCK_MAX_TOKENS should be ignored entirely when provider != bedrock.
  ScopedEnvVar override("YAC_BEDROCK_MAX_TOKENS", "abc");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE_FALSE(HasIssue(result.issues, "Invalid Bedrock max_tokens"));
}
