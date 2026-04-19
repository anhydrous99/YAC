#include "chat/prompt_library.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ConfigIssue;
using yac::chat::LoadPromptLibrary;
using yac::chat::PromptDefinition;
using yac::chat::RenderPrompt;

namespace {

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
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
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

bool HasIssueContaining(const std::vector<ConfigIssue>& issues,
                        const std::string& text) {
  return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
    return issue.message.find(text) != std::string::npos ||
           issue.detail.find(text) != std::string::npos;
  });
}

std::vector<std::string> PromptNames(
    const std::vector<PromptDefinition>& prompts) {
  std::vector<std::string> names;
  names.reserve(prompts.size());
  for (const auto& prompt : prompts) {
    names.push_back(prompt.name);
  }
  return names;
}

}  // namespace

TEST_CASE("LoadPromptLibrary seeds init and review defaults") {
  TempDir dir("yac_test_prompt_library_seed");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/true);

  REQUIRE(std::filesystem::exists(dir.Path() / "init.toml"));
  REQUIRE(std::filesystem::exists(dir.Path() / "review.toml"));
  REQUIRE(result.issues.empty());
  REQUIRE(PromptNames(result.prompts) ==
          std::vector<std::string>{"init", "review"});
  REQUIRE(result.prompts[0].prompt.find("Create or update `AGENTS.md`") !=
          std::string::npos);
  REQUIRE(result.prompts[1].prompt.find("You are a code reviewer") !=
          std::string::npos);
}

TEST_CASE("LoadPromptLibrary does not overwrite existing seeded prompts") {
  TempDir dir("yac_test_prompt_library_no_overwrite");
  WriteFile(dir.Path() / "init.toml",
            "description = \"Custom init\"\n"
            "prompt = \"custom $ARGUMENTS\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/true);

  REQUIRE(ReadFile(dir.Path() / "init.toml").find("custom $ARGUMENTS") !=
          std::string::npos);
  REQUIRE(std::filesystem::exists(dir.Path() / "review.toml"));
  auto init =
      std::find_if(result.prompts.begin(), result.prompts.end(),
                   [](const auto& prompt) { return prompt.name == "init"; });
  REQUIRE(init != result.prompts.end());
  REQUIRE(init->description == "Custom init");
  REQUIRE(init->prompt == "custom $ARGUMENTS");
}

TEST_CASE("LoadPromptLibrary parses valid custom prompt files") {
  TempDir dir("yac_test_prompt_library_valid");
  WriteFile(dir.Path() / "ship-it.toml",
            "description = \"Ship review\"\n"
            "prompt = \"Review release notes\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/false);

  REQUIRE(result.issues.empty());
  REQUIRE(result.prompts.size() == 1);
  REQUIRE(result.prompts[0].name == "ship-it");
  REQUIRE(result.prompts[0].description == "Ship review");
  REQUIRE(result.prompts[0].prompt == "Review release notes");
}

TEST_CASE("LoadPromptLibrary falls back for invalid prompt descriptions") {
  TempDir dir("yac_test_prompt_library_description");
  WriteFile(dir.Path() / "notes.toml",
            "description = 42\n"
            "prompt = \"Summarize\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/false);

  REQUIRE(result.prompts.size() == 1);
  REQUIRE(result.prompts[0].description == "Run predefined prompt");
  REQUIRE(HasIssueContaining(result.issues, "description"));
}

TEST_CASE("LoadPromptLibrary skips prompt files without string prompts") {
  TempDir dir("yac_test_prompt_library_missing_prompt");
  WriteFile(dir.Path() / "bad.toml", "description = \"Bad\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/false);

  REQUIRE(result.prompts.empty());
  REQUIRE(HasIssueContaining(result.issues, "prompt"));
}

TEST_CASE("LoadPromptLibrary skips invalid command file names") {
  TempDir dir("yac_test_prompt_library_invalid_name");
  WriteFile(dir.Path() / "Bad Name.toml",
            "description = \"Bad\"\n"
            "prompt = \"Nope\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/false);

  REQUIRE(result.prompts.empty());
  REQUIRE(HasIssueContaining(result.issues, "File names"));
}

TEST_CASE("LoadPromptLibrary returns prompts in deterministic name order") {
  TempDir dir("yac_test_prompt_library_sort");
  WriteFile(dir.Path() / "zeta.toml", "prompt = \"z\"\n");
  WriteFile(dir.Path() / "alpha.toml", "prompt = \"a\"\n");

  auto result = LoadPromptLibrary(dir.Path(), /*seed_defaults=*/false);

  REQUIRE(PromptNames(result.prompts) ==
          std::vector<std::string>{"alpha", "zeta"});
}

TEST_CASE("RenderPrompt replaces all argument tokens with trimmed arguments") {
  REQUIRE(RenderPrompt("before $ARGUMENTS after $ARGUMENTS", "  main  ") ==
          "before main after main");
}

TEST_CASE("RenderPrompt accepts a prompt definition") {
  PromptDefinition prompt{
      .name = "review",
      .description = "Review",
      .prompt = "Review $ARGUMENTS",
  };

  REQUIRE(RenderPrompt(prompt, "\tfeature\n") == "Review feature");
}
