#include "chat/settings_toml.hpp"
#include "chat/settings_toml_template.hpp"
#include "chat/types.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <catch2/catch_test_macros.hpp>

using yac::chat::ChatConfig;
using yac::chat::ConfigIssue;
using yac::chat::ConfigIssueSeverity;
using yac::chat::kDefaultSettingsToml;
using yac::chat::kDefaultToolRoundLimit;
using yac::chat::LoadSettingsFromToml;
using yac::chat::SaveThemeNameToSettingsToml;
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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("LoadSettingsFromToml is a no-op when the file is missing") {
  TempFile file("yac_test_settings_missing.toml");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE_FALSE(fields.provider_id);
  REQUIRE(config.model.value == "gpt-4o-mini");
}

TEST_CASE("LoadSettingsFromToml overlays explicit values") {
  TempFile file("yac_test_settings_full.toml");
  WriteFile(file.Path(),
            "temperature = 1.2\n"
            "max_tool_rounds = 48\n"
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
  REQUIRE(fields.max_tool_rounds);
  REQUIRE(fields.lsp_clangd_args);
  REQUIRE(config.provider_id.value == "zai");
  REQUIRE(config.model.value == "glm-custom");
  REQUIRE(config.temperature == 1.2);
  REQUIRE(config.max_tool_rounds == 48);
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
  REQUIRE(config.max_tool_rounds == kDefaultToolRoundLimit);
  REQUIRE(config.provider_id.value == "openai-compatible");
  REQUIRE(config.model.value == "gpt-4o-mini");
}

TEST_CASE("LoadSettingsFromToml defaults sync_terminal_background to true") {
  TempFile file("yac_test_settings_theme_default.toml");
  WriteFile(file.Path(), "temperature = 0.5\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.sync_terminal_background);
}

TEST_CASE("LoadSettingsFromToml overlays [theme] sync_terminal_background") {
  TempFile file("yac_test_settings_theme_off.toml");
  WriteFile(file.Path(),
            "[theme]\n"
            "sync_terminal_background = false\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE_FALSE(config.sync_terminal_background);
}

TEST_CASE("LoadSettingsFromToml reports invalid [theme] type") {
  TempFile file("yac_test_settings_theme_bad_field.toml");
  WriteFile(file.Path(),
            "[theme]\n"
            "sync_terminal_background = \"yes\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("sync_terminal_background") != std::string::npos;
  }));
  REQUIRE(config.sync_terminal_background);
}

TEST_CASE("SaveThemeNameToSettingsToml updates existing theme name") {
  TempFile file("yac_test_settings_theme_save_existing.toml");
  WriteFile(file.Path(),
            "# keep me\n"
            "temperature = 0.5\n"
            "\n"
            "[theme]\n"
            "# also keep me\n"
            "name = \"opencode\" # replaced\n"
            "sync_terminal_background = false\n"
            "\n"
            "[provider]\n"
            "id = \"openai\"\n");

  std::vector<ConfigIssue> issues;
  REQUIRE(SaveThemeNameToSettingsToml(file.Path(), "catppuccin", issues));
  REQUIRE(issues.empty());

  const auto content = ReadFile(file.Path());
  REQUIRE(content.find("# keep me\n") != std::string::npos);
  REQUIRE(content.find("# also keep me\n") != std::string::npos);
  REQUIRE(content.find("name = \"catppuccin\"\n") != std::string::npos);
  REQUIRE(content.find("sync_terminal_background = false\n") !=
          std::string::npos);
  REQUIRE(content.find("[provider]\n") != std::string::npos);

  ChatConfig config;
  std::vector<ConfigIssue> load_issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, load_issues);
  REQUIRE(load_issues.empty());
  REQUIRE(fields.theme_name);
  REQUIRE(config.theme_name == "catppuccin");
  REQUIRE_FALSE(config.sync_terminal_background);
}

TEST_CASE("SaveThemeNameToSettingsToml inserts name in existing theme table") {
  TempFile file("yac_test_settings_theme_save_insert.toml");
  WriteFile(file.Path(),
            "[theme]\n"
            "sync_terminal_background = false\n");

  std::vector<ConfigIssue> issues;
  REQUIRE(SaveThemeNameToSettingsToml(file.Path(), "system", issues));
  REQUIRE(issues.empty());

  ChatConfig config;
  std::vector<ConfigIssue> load_issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, load_issues);
  REQUIRE(load_issues.empty());
  REQUIRE(fields.theme_name);
  REQUIRE(config.theme_name == "system");
  REQUIRE_FALSE(config.sync_terminal_background);
}

TEST_CASE("SaveThemeNameToSettingsToml appends theme table when absent") {
  TempFile file("yac_test_settings_theme_save_append.toml");
  WriteFile(file.Path(), "temperature = 0.5\n");

  std::vector<ConfigIssue> issues;
  REQUIRE(SaveThemeNameToSettingsToml(file.Path(), "catppuccin", issues));
  REQUIRE(issues.empty());

  const auto content = ReadFile(file.Path());
  REQUIRE(content.find("temperature = 0.5\n") != std::string::npos);
  REQUIRE(content.find("[theme]\n") != std::string::npos);
  REQUIRE(content.find("name = \"catppuccin\"\n") != std::string::npos);

  ChatConfig config;
  std::vector<ConfigIssue> load_issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, load_issues);
  REQUIRE(load_issues.empty());
  REQUIRE(fields.theme_name);
  REQUIRE(config.theme_name == "catppuccin");
}

TEST_CASE("SaveThemeNameToSettingsToml creates missing settings file") {
  TempFile dir("yac_test_settings_theme_save_missing");
  const auto path = dir.Path() / "settings.toml";

  std::vector<ConfigIssue> issues;
  REQUIRE(SaveThemeNameToSettingsToml(path, "system", issues));
  REQUIRE(issues.empty());
  REQUIRE(std::filesystem::exists(path));

  ChatConfig config;
  std::vector<ConfigIssue> load_issues;
  auto fields = LoadSettingsFromToml(path, config, load_issues);
  REQUIRE(load_issues.empty());
  REQUIRE(fields.theme_name);
  REQUIRE(config.theme_name == "system");
}

TEST_CASE("SaveThemeNameToSettingsToml leaves malformed TOML untouched") {
  TempFile file("yac_test_settings_theme_save_bad_toml.toml");
  WriteFile(file.Path(), "garbage garbage garbage\n");
  const auto before = ReadFile(file.Path());

  std::vector<ConfigIssue> issues;
  REQUIRE_FALSE(SaveThemeNameToSettingsToml(file.Path(), "system", issues));
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("settings.toml") != std::string::npos;
  }));
  REQUIRE(ReadFile(file.Path()) == before);
}

TEST_CASE("SaveThemeNameToSettingsToml leaves invalid theme table untouched") {
  TempFile file("yac_test_settings_theme_save_bad_theme.toml");
  WriteFile(file.Path(),
            "[theme]\n"
            "name = 42\n");
  const auto before = ReadFile(file.Path());

  std::vector<ConfigIssue> issues;
  REQUIRE_FALSE(SaveThemeNameToSettingsToml(file.Path(), "system", issues));
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("theme.name") != std::string::npos;
  }));
  REQUIRE(ReadFile(file.Path()) == before);
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

TEST_CASE("LoadSettingsFromToml rejects out-of-range max_tool_rounds") {
  TempFile file("yac_test_settings_tool_rounds_range.toml");
  WriteFile(file.Path(), "max_tool_rounds = 0\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("max_tool_rounds") != std::string::npos;
  }));
  REQUIRE(config.max_tool_rounds == kDefaultToolRoundLimit);
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
  REQUIRE(fields.max_tool_rounds);
  REQUIRE(fields.provider_id);
  REQUIRE(loaded.provider_id == expected.provider_id);
  REQUIRE(loaded.model == expected.model);
  REQUIRE(loaded.base_url == expected.base_url);
  REQUIRE(loaded.api_key_env == expected.api_key_env);
  REQUIRE(loaded.temperature == expected.temperature);
  REQUIRE(loaded.max_tool_rounds == expected.max_tool_rounds);
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

#ifndef _WIN32
TEST_CASE("WriteDefaultSettingsToml creates the file with mode 0600") {
  const auto dir =
      std::filesystem::temp_directory_path() / "yac_test_settings_perms";
  std::filesystem::remove_all(dir);
  const auto path = dir / "settings.toml";

  std::vector<ConfigIssue> issues;
  WriteDefaultSettingsToml(path, issues);
  REQUIRE(issues.empty());
  REQUIRE(std::filesystem::exists(path));

  struct stat file_stat{};
  REQUIRE(::stat(path.c_str(), &file_stat) == 0);
  REQUIRE((file_stat.st_mode & 0777) == 0600);

  std::filesystem::remove_all(dir);
}

TEST_CASE("WriteDefaultSettingsToml does not overwrite an existing file") {
  const auto dir =
      std::filesystem::temp_directory_path() / "yac_test_settings_no_clobber";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto path = dir / "settings.toml";

  constexpr std::string_view kExistingContent = "# pre-existing\n";
  {
    std::ofstream pre(path, std::ios::trunc);
    pre << kExistingContent;
  }

  std::vector<ConfigIssue> issues;
  WriteDefaultSettingsToml(path, issues);
  REQUIRE(issues.empty());

  std::ifstream input(path);
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  REQUIRE(content == std::string{kExistingContent});

  std::filesystem::remove_all(dir);
}
#endif

TEST_CASE("LoadSettingsFromToml overlays [compact] section") {
  TempFile file("yac_test_settings_compact.toml");
  WriteFile(file.Path(),
            "[compact]\n"
            "auto_enabled = false\n"
            "threshold = 0.65\n"
            "keep_last = 50\n"
            "mode = \"truncate\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE_FALSE(config.auto_compact_enabled);
  REQUIRE(config.auto_compact_threshold == 0.65);
  REQUIRE(config.auto_compact_keep_last == 50);
  REQUIRE(config.auto_compact_mode == "truncate");
}

TEST_CASE("LoadSettingsFromToml keeps compact defaults when [compact] absent") {
  TempFile file("yac_test_settings_no_compact.toml");
  WriteFile(file.Path(), "temperature = 0.5\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.auto_compact_enabled);
  REQUIRE(config.auto_compact_threshold == 0.8);
  REQUIRE(config.auto_compact_keep_last == 20);
  REQUIRE(config.auto_compact_mode == "summarize");
}

TEST_CASE("LoadSettingsFromToml rejects unknown compact.mode") {
  TempFile file("yac_test_settings_bad_mode.toml");
  WriteFile(file.Path(),
            "[compact]\n"
            "mode = \"shrubbery\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error;
  }));
  REQUIRE(config.auto_compact_mode == "summarize");
}

TEST_CASE("LoadSettingsFromToml rejects out-of-range compact.threshold") {
  TempFile file("yac_test_settings_bad_threshold.toml");
  WriteFile(file.Path(),
            "[compact]\n"
            "threshold = 1.5\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error;
  }));
}

TEST_CASE("LoadSettingsFromToml rejects out-of-range compact.keep_last") {
  TempFile file("yac_test_settings_bad_keep_last.toml");
  WriteFile(file.Path(),
            "[compact]\n"
            "keep_last = 0\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error;
  }));
}

TEST_CASE("LoadSettingsFromToml reports invalid [compact] table type") {
  TempFile file("yac_test_settings_compact_scalar.toml");
  WriteFile(file.Path(), "compact = \"all\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(std::ranges::any_of(issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error;
  }));
}
