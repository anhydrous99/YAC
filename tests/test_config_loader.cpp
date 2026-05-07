#include "chat/config.hpp"
#include "chat/config_loader.hpp"
#include "chat/prompt_library.hpp"
#include "chat/settings_toml.hpp"
#include "chat/types.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef SETTINGS_EXAMPLE_TOML_PATH
#error "SETTINGS_EXAMPLE_TOML_PATH must be defined to point at the repo file"
#endif

using yac::chat::ChatConfig;
using yac::chat::ConfigIssue;
using yac::chat::LoadChatConfigResultFrom;
using yac::chat::LoadConfig;
using yac::chat::LoadPromptLibrary;
using yac::chat::LoadSettingsFromToml;

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

class ScopedUnsetEnvVar {
 public:
  explicit ScopedUnsetEnvVar(std::string name) : name_(std::move(name)) {
    const char* previous = std::getenv(name_.c_str());
    if (previous != nullptr) {
      has_previous_ = true;
      previous_ = previous;
    }
    unsetenv(name_.c_str());
  }
  ~ScopedUnsetEnvVar() {
    if (has_previous_) {
      setenv(name_.c_str(), previous_.c_str(), 1);
    }
  }

  ScopedUnsetEnvVar(const ScopedUnsetEnvVar&) = delete;
  ScopedUnsetEnvVar& operator=(const ScopedUnsetEnvVar&) = delete;
  ScopedUnsetEnvVar(ScopedUnsetEnvVar&&) = delete;
  ScopedUnsetEnvVar& operator=(ScopedUnsetEnvVar&&) = delete;

 private:
  std::string name_;
  std::string previous_;
  bool has_previous_ = false;
};

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

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

bool HasIssue(const std::vector<ConfigIssue>& issues,
              std::string_view substring) {
  return std::ranges::any_of(issues, [&](const ConfigIssue& issue) {
    return issue.message.find(substring) != std::string::npos ||
           issue.detail.find(substring) != std::string::npos;
  });
}

}  // namespace

TEST_CASE("settings.example.toml parses cleanly via LoadSettingsFromToml") {
  const std::filesystem::path example_path{SETTINGS_EXAMPLE_TOML_PATH};
  REQUIRE(std::filesystem::exists(example_path));

  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(example_path, config, issues);

  REQUIRE(issues.empty());
  REQUIRE(fields.provider_id);
  REQUIRE(fields.model);
  REQUIRE(fields.base_url);
  REQUIRE(fields.api_key_env);
  REQUIRE(fields.temperature);
  REQUIRE(fields.max_tool_rounds);
  REQUIRE(fields.lsp_clangd_command);
  REQUIRE(fields.lsp_clangd_args);
  REQUIRE(fields.theme_name);
  REQUIRE(config.provider_id.value == "openai-compatible");
  REQUIRE(config.model.value == "gpt-4o-mini");
  REQUIRE(config.base_url == "https://api.openai.com/v1/");
  REQUIRE(config.api_key_env == "OPENAI_API_KEY");
  REQUIRE(config.temperature == 0.7);
  REQUIRE(config.max_tool_rounds == 32);
  REQUIRE(config.lsp_clangd_command == "clangd");
  REQUIRE(config.lsp_clangd_args.empty());
  REQUIRE(config.theme_name == "vivid");
  REQUIRE(config.sync_terminal_background);
  REQUIRE(config.auto_compact_enabled);
  REQUIRE(config.auto_compact_threshold == 0.8);
  REQUIRE(config.auto_compact_keep_last == 20);
  REQUIRE(config.auto_compact_mode == "summarize");
}

TEST_CASE("settings.example.toml round-trips through copy-and-reparse") {
  const std::filesystem::path example_path{SETTINGS_EXAMPLE_TOML_PATH};
  TempDir dir("yac_test_config_loader_roundtrip");
  const auto copied_path = dir.Path() / "settings.toml";

  WriteFile(copied_path, ReadFile(example_path));

  ChatConfig original;
  std::vector<ConfigIssue> first_issues;
  LoadSettingsFromToml(example_path, original, first_issues);

  ChatConfig replayed;
  std::vector<ConfigIssue> second_issues;
  LoadSettingsFromToml(copied_path, replayed, second_issues);

  REQUIRE(first_issues.empty());
  REQUIRE(second_issues.empty());
  REQUIRE(replayed.provider_id == original.provider_id);
  REQUIRE(replayed.model == original.model);
  REQUIRE(replayed.base_url == original.base_url);
  REQUIRE(replayed.api_key_env == original.api_key_env);
  REQUIRE(replayed.temperature == original.temperature);
  REQUIRE(replayed.max_tool_rounds == original.max_tool_rounds);
  REQUIRE(replayed.lsp_clangd_command == original.lsp_clangd_command);
  REQUIRE(replayed.lsp_clangd_args == original.lsp_clangd_args);
  REQUIRE(replayed.theme_name == original.theme_name);
  REQUIRE(replayed.theme_density == original.theme_density);
  REQUIRE(replayed.sync_terminal_background ==
          original.sync_terminal_background);
  REQUIRE(replayed.auto_compact_enabled == original.auto_compact_enabled);
  REQUIRE(replayed.auto_compact_threshold == original.auto_compact_threshold);
  REQUIRE(replayed.auto_compact_keep_last == original.auto_compact_keep_last);
  REQUIRE(replayed.auto_compact_mode == original.auto_compact_mode);
  REQUIRE(replayed.mcp.servers.size() == original.mcp.servers.size());
}

TEST_CASE("LoadConfig aggregates settings.toml and prompt library") {
  TempDir dir("yac_test_config_loader_aggregate");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto prompts_dir = dir.Path() / "prompts";

  WriteFile(settings_path,
            "temperature = 0.42\n"
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"gpt-4o-mini\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");

  auto loaded = LoadConfig(settings_path, prompts_dir);

  REQUIRE(loaded.chat.config.temperature == 0.42);
  REQUIRE(loaded.chat.config.provider_id.value == "openai-compatible");
  REQUIRE(loaded.chat.config.model.value == "gpt-4o-mini");
  REQUIRE(std::filesystem::exists(prompts_dir / "init.toml"));
  REQUIRE(std::filesystem::exists(prompts_dir / "review.toml"));

  std::vector<std::string> names;
  names.reserve(loaded.prompt_library.prompts.size());
  for (const auto& prompt : loaded.prompt_library.prompts) {
    names.push_back(prompt.name);
  }
  REQUIRE(std::ranges::find(names, std::string{"init"}) != names.end());
  REQUIRE(std::ranges::find(names, std::string{"review"}) != names.end());
}

TEST_CASE("LoadConfig matches separate LoadChatConfig + LoadPromptLibrary") {
  TempDir dir_aggregate("yac_test_config_loader_aggregate_compare");
  TempDir dir_split("yac_test_config_loader_split_compare");

  constexpr const char* kSettings =
      "temperature = 1.2\n"
      "max_tool_rounds = 48\n"
      "[provider]\n"
      "id = \"openai-compatible\"\n"
      "model = \"gpt-4o-mini\"\n"
      "[lsp.clangd]\n"
      "command = \"/usr/bin/clangd-18\"\n";

  const auto aggregate_settings = dir_aggregate.Path() / "settings.toml";
  const auto aggregate_prompts = dir_aggregate.Path() / "prompts";
  const auto split_settings = dir_split.Path() / "settings.toml";
  const auto split_prompts = dir_split.Path() / "prompts";
  WriteFile(aggregate_settings, kSettings);
  WriteFile(split_settings, kSettings);

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");

  auto aggregate = LoadConfig(aggregate_settings, aggregate_prompts);
  auto split_chat = LoadChatConfigResultFrom(split_settings,
                                             /*create_if_missing=*/true);
  auto split_prompts_result =
      LoadPromptLibrary(split_prompts, /*seed_defaults=*/true);

  REQUIRE(aggregate.chat.config.temperature == split_chat.config.temperature);
  REQUIRE(aggregate.chat.config.max_tool_rounds ==
          split_chat.config.max_tool_rounds);
  REQUIRE(aggregate.chat.config.provider_id == split_chat.config.provider_id);
  REQUIRE(aggregate.chat.config.model == split_chat.config.model);
  REQUIRE(aggregate.chat.config.base_url == split_chat.config.base_url);
  REQUIRE(aggregate.chat.config.lsp_clangd_command ==
          split_chat.config.lsp_clangd_command);
  REQUIRE(aggregate.chat.issues.size() == split_chat.issues.size());

  REQUIRE(aggregate.prompt_library.prompts.size() ==
          split_prompts_result.prompts.size());
  REQUIRE(aggregate.prompt_library.issues.size() ==
          split_prompts_result.issues.size());
}

TEST_CASE("env-precedence: YAC_* overrides TOML values") {
  TempDir dir("yac_test_config_loader_env_precedence");
  const auto settings_path = dir.Path() / "settings.toml";

  WriteFile(settings_path,
            "temperature = 0.3\n"
            "max_tool_rounds = 12\n"
            "system_prompt = \"file-prompt\"\n"
            "workspace_root = \"/from-file\"\n"
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"file-model\"\n"
            "base_url = \"https://file.example.com/\"\n"
            "api_key_env = \"FILE_API_KEY\"\n"
            "[lsp.clangd]\n"
            "command = \"file-clangd\"\n"
            "args = [\"--file\"]\n"
            "[theme]\n"
            "name = \"vivid\"\n"
            "density = \"comfortable\"\n");

  ScopedEnvVar yac_provider("YAC_PROVIDER", "openai-compatible");
  ScopedEnvVar yac_model("YAC_MODEL", "env-model");
  ScopedEnvVar yac_base_url("YAC_BASE_URL", "https://env.example.com/");
  ScopedEnvVar yac_api_key_env("YAC_API_KEY_ENV", "ENV_API_KEY");
  ScopedEnvVar env_api_key("ENV_API_KEY", "secret-from-env");
  ScopedEnvVar yac_temperature("YAC_TEMPERATURE", "1.4");
  ScopedEnvVar yac_max_tool_rounds("YAC_MAX_TOOL_ROUNDS", "96");
  ScopedEnvVar yac_system_prompt("YAC_SYSTEM_PROMPT", "env-prompt");
  ScopedEnvVar yac_workspace_root("YAC_WORKSPACE_ROOT", "/from-env");
  ScopedEnvVar yac_lsp_command("YAC_LSP_CLANGD_COMMAND", "env-clangd");
  ScopedEnvVar yac_lsp_args("YAC_LSP_CLANGD_ARGS", "--env --background-index");
  ScopedEnvVar yac_theme_name("YAC_THEME_NAME", "system");
  ScopedEnvVar yac_theme_density("YAC_THEME_DENSITY", "compact");

  auto result = LoadChatConfigResultFrom(settings_path,
                                         /*create_if_missing=*/false);

  REQUIRE(result.config.provider_id.value == "openai-compatible");
  REQUIRE(result.config.model.value == "env-model");
  REQUIRE(result.config.base_url == "https://env.example.com/");
  REQUIRE(result.config.api_key_env == "ENV_API_KEY");
  REQUIRE(result.config.api_key == "secret-from-env");
  REQUIRE(result.config.temperature == 1.4);
  REQUIRE(result.config.max_tool_rounds == 96);
  REQUIRE(result.config.system_prompt == std::string{"env-prompt"});
  REQUIRE(result.config.workspace_root == "/from-env");
  REQUIRE(result.config.lsp_clangd_command == "env-clangd");
  REQUIRE(result.config.lsp_clangd_args ==
          std::vector<std::string>{"--env", "--background-index"});
  REQUIRE(result.config.theme_name == "system");
  REQUIRE(result.config.theme_density == "compact");
  REQUIRE_FALSE(HasIssue(result.issues, "Invalid"));
}

TEST_CASE("env-precedence: TOML value used when YAC_* env var unset") {
  TempDir dir("yac_test_config_loader_env_unset");
  const auto settings_path = dir.Path() / "settings.toml";

  WriteFile(settings_path,
            "temperature = 0.55\n"
            "max_tool_rounds = 7\n"
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"toml-model\"\n");

  ScopedUnsetEnvVar unset_provider("YAC_PROVIDER");
  ScopedUnsetEnvVar unset_model("YAC_MODEL");
  ScopedUnsetEnvVar unset_base_url("YAC_BASE_URL");
  ScopedUnsetEnvVar unset_temperature("YAC_TEMPERATURE");
  ScopedUnsetEnvVar unset_tool_rounds("YAC_MAX_TOOL_ROUNDS");
  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");

  auto result = LoadChatConfigResultFrom(settings_path,
                                         /*create_if_missing=*/false);

  REQUIRE(result.config.provider_id.value == "openai-compatible");
  REQUIRE(result.config.model.value == "toml-model");
  REQUIRE(result.config.temperature == 0.55);
  REQUIRE(result.config.max_tool_rounds == 7);
}

TEST_CASE("env-precedence: YAC_MCP_<ID>_* overrides per-server fields") {
  TempDir dir("yac_test_config_loader_env_mcp");
  const auto settings_path = dir.Path() / "settings.toml";

  WriteFile(settings_path,
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"-y\", \"@upstash/context7-mcp\"]\n"
            "enabled = true\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override_command("YAC_MCP_CTX7_COMMAND", "from-env-bin");
  ScopedEnvVar override_args("YAC_MCP_CTX7_ARGS", "--from --env");
  ScopedEnvVar override_enabled("YAC_MCP_CTX7_ENABLED", "false");

  auto result = LoadChatConfigResultFrom(settings_path,
                                         /*create_if_missing=*/false);

  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  REQUIRE(result.config.mcp.servers[0].command == "from-env-bin");
  REQUIRE(result.config.mcp.servers[0].args ==
          std::vector<std::string>{"--from", "--env"});
  REQUIRE_FALSE(result.config.mcp.servers[0].enabled);
}

TEST_CASE("LoadConfig preserves env-precedence ordering") {
  TempDir dir("yac_test_config_loader_aggregate_env");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto prompts_dir = dir.Path() / "prompts";

  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"file-model\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar yac_model("YAC_MODEL", "env-model");

  auto loaded = LoadConfig(settings_path, prompts_dir);

  REQUIRE(loaded.chat.config.model.value == "env-model");
}
