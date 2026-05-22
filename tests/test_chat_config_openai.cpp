#include "chat/config.hpp"
#include "chat/types.hpp"
#include "config_env_test_helpers.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

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

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

}  // namespace

TEST_CASE("openai preset applies default model, base url, and key env") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_openai_defaults.toml");
  WriteFile(file.Path(), "[provider]\nid = \"openai\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "openai-key");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.provider_id.value == "openai");
  REQUIRE(result.config.model.value == "gpt-4o-mini");
  REQUIRE(result.config.base_url == "https://api.openai.com/v1/");
  REQUIRE(result.config.api_key_env == "OPENAI_API_KEY");
  REQUIRE(result.config.api_key == "openai-key");
  REQUIRE(result.issues.empty());
}

TEST_CASE("openai preset preserves explicit overrides") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_openai_overrides.toml");
  WriteFile(file.Path(),
            "[provider]\n"
            "id = \"openai\"\n"
            "model = \"custom-model\"\n"
            "base_url = \"https://example.com/v1/\"\n"
            "api_key_env = \"YAC_OPENAI_TEST_KEY\"\n");

  ScopedEnvVar api_key("YAC_OPENAI_TEST_KEY", "override-key");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.provider_id.value == "openai");
  REQUIRE(result.config.model.value == "custom-model");
  REQUIRE(result.config.base_url == "https://example.com/v1/");
  REQUIRE(result.config.api_key_env == "YAC_OPENAI_TEST_KEY");
  REQUIRE(result.config.api_key == "override-key");
  REQUIRE(result.issues.empty());
}

TEST_CASE("YAC_PROVIDER=openai applies the openai preset") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_openai_env_provider.toml");
  WriteFile(file.Path(), "[provider]\nid = \"openai-compatible\"\n");

  ScopedEnvVar provider("YAC_PROVIDER", "openai");
  ScopedEnvVar api_key("OPENAI_API_KEY", "env-openai-key");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.provider_id.value == "openai");
  REQUIRE(result.config.model.value == "gpt-4o-mini");
  REQUIRE(result.config.base_url == "https://api.openai.com/v1/");
  REQUIRE(result.config.api_key_env == "OPENAI_API_KEY");
  REQUIRE(result.config.api_key == "env-openai-key");
  REQUIRE(result.issues.empty());
}

TEST_CASE("YAC_PROVIDER=openai preserves explicit TOML provider fields") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_openai_env_provider_preserves_toml.toml");
  WriteFile(file.Path(),
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"toml-openai-model\"\n"
            "base_url = \"https://toml-openai.example/v1/\"\n"
            "api_key_env = \"YAC_OPENAI_ENV_PROVIDER_KEY\"\n");

  ScopedEnvVar provider("YAC_PROVIDER", "openai");
  ScopedEnvVar api_key("YAC_OPENAI_ENV_PROVIDER_KEY", "toml-key");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.provider_id.value == "openai");
  REQUIRE(result.config.model.value == "toml-openai-model");
  REQUIRE(result.config.base_url == "https://toml-openai.example/v1/");
  REQUIRE(result.config.api_key_env == "YAC_OPENAI_ENV_PROVIDER_KEY");
  REQUIRE(result.config.api_key == "toml-key");
  REQUIRE(result.issues.empty());
}

TEST_CASE("default provider stays openai-compatible when id is absent") {
  ScopedEnvClear env_guard;
  TempFile file("yac_test_openai_default_unchanged.toml");
  WriteFile(file.Path(), "temperature = 0.4\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "default-key");
  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);

  REQUIRE(result.config.provider_id.value == "openai-compatible");
  REQUIRE(result.config.model.value == "gpt-4o-mini");
  REQUIRE(result.config.base_url == "https://api.openai.com/v1/");
  REQUIRE(result.config.api_key_env == "OPENAI_API_KEY");
  REQUIRE(result.config.api_key == "default-key");
}
