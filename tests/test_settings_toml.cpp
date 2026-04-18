#include "chat/settings_toml.hpp"
#include "chat/settings_toml_template.hpp"
#include "chat/types.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ChatConfig;
using yac::chat::ConfigIssue;
using yac::chat::ConfigIssueSeverity;
using yac::chat::kDefaultSettingsToml;
using yac::chat::LoadSettingsFromToml;
using yac::chat::WriteDefaultSettingsToml;

namespace {

class TempFile {
 public:
  explicit TempFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_.parent_path() / name);
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

TEST_CASE("LoadSettingsFromToml is a no-op when the file is missing") {
  TempFile file("yac_test_settings_missing.toml");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE_FALSE(fields.provider_id);
  REQUIRE(config.model == "gpt-4o-mini");
}

TEST_CASE("LoadSettingsFromToml overlays explicit values") {
  TempFile file("yac_test_settings_full.toml");
  WriteFile(file.Path(),
            "temperature = 1.2\n"
            "system_prompt = \"hi\"\n"
            "\n"
            "[provider]\n"
            "id = \"zai\"\n"
            "model = \"glm-custom\"\n"
            "\n"
            "[lsp.clangd]\n"
            "command = \"/usr/bin/clangd-18\"\n"
            "args = [\"--background-index\"]\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(fields.provider_id);
  REQUIRE(fields.model);
  REQUIRE(fields.temperature);
  REQUIRE(fields.lsp_clangd_args);
  REQUIRE(config.provider_id == "zai");
  REQUIRE(config.model == "glm-custom");
  REQUIRE(config.temperature == 1.2);
  REQUIRE(config.system_prompt == std::string{"hi"});
  REQUIRE(config.lsp_clangd_command == "/usr/bin/clangd-18");
  REQUIRE(config.lsp_clangd_args ==
          std::vector<std::string>{"--background-index"});
}

TEST_CASE("LoadSettingsFromToml keeps defaults for absent fields") {
  TempFile file("yac_test_settings_partial.toml");
  WriteFile(file.Path(), "temperature = 0.3\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(fields.temperature);
  REQUIRE_FALSE(fields.provider_id);
  REQUIRE(config.temperature == 0.3);
  REQUIRE(config.provider_id == "openai");
  REQUIRE(config.model == "gpt-4o-mini");
}

TEST_CASE("LoadSettingsFromToml reports parse errors without throwing") {
  TempFile file("yac_test_settings_bad.toml");
  WriteFile(file.Path(), "garbage garbage garbage\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE_FALSE(fields.provider_id);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("settings.toml") != std::string::npos;
  }));
}

TEST_CASE("LoadSettingsFromToml reports invalid types") {
  TempFile file("yac_test_settings_types.toml");
  WriteFile(file.Path(), "temperature = \"hot\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("temperature") != std::string::npos;
  }));
  REQUIRE(config.temperature == 0.7);
}

TEST_CASE("LoadSettingsFromToml rejects out-of-range temperature") {
  TempFile file("yac_test_settings_temp_range.toml");
  WriteFile(file.Path(), "temperature = 5.0\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("temperature") != std::string::npos;
  }));
  REQUIRE(config.temperature == 0.7);
}

TEST_CASE("WriteDefaultSettingsToml round-trips to the default ChatConfig") {
  const auto dir =
      std::filesystem::temp_directory_path() / "yac_test_settings_roundtrip";
  std::filesystem::remove_all(dir);
  const auto path = dir / "settings.toml";

  ChatConfig expected;  // built-in defaults
  std::vector<ConfigIssue> write_issues;
  WriteDefaultSettingsToml(path, write_issues);
  REQUIRE(write_issues.empty());
  REQUIRE(std::filesystem::exists(path));

  ChatConfig loaded;
  std::vector<ConfigIssue> load_issues;
  auto fields = LoadSettingsFromToml(path, loaded, load_issues);
  REQUIRE(load_issues.empty());
  REQUIRE(fields.temperature);
  REQUIRE(fields.provider_id);
  REQUIRE(loaded.provider_id == expected.provider_id);
  REQUIRE(loaded.model == expected.model);
  REQUIRE(loaded.base_url == expected.base_url);
  REQUIRE(loaded.api_key_env == expected.api_key_env);
  REQUIRE(loaded.temperature == expected.temperature);
  REQUIRE(loaded.lsp_clangd_command == expected.lsp_clangd_command);
  REQUIRE(loaded.lsp_clangd_args == expected.lsp_clangd_args);

  std::filesystem::remove_all(dir);
}

TEST_CASE("WriteDefaultSettingsToml writes the canonical template") {
  const auto dir =
      std::filesystem::temp_directory_path() / "yac_test_settings_template";
  std::filesystem::remove_all(dir);
  const auto path = dir / "settings.toml";

  std::vector<ConfigIssue> issues;
  WriteDefaultSettingsToml(path, issues);
  REQUIRE(issues.empty());

  std::ifstream input(path);
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  REQUIRE(content == std::string{kDefaultSettingsToml});

  std::filesystem::remove_all(dir);
}
