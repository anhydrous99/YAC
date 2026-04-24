#include "chat/chat_service_request_builder.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::chat::internal;

namespace {

const std::filesystem::path kTempWorkspaceRoot =
    std::filesystem::temp_directory_path() / "yac_test_agents_md";

std::filesystem::path PrepareWorkspace(const std::string& name) {
  const auto workspace = kTempWorkspaceRoot / name;
  std::filesystem::remove_all(workspace);
  std::filesystem::create_directories(workspace);
  return workspace;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary);
  REQUIRE(file.is_open());
  file << content;
}

ChatRequest BuildRequest(
    const std::filesystem::path& workspace,
    std::optional<std::string> system_prompt = std::nullopt) {
  ChatConfig config;
  config.workspace_root = workspace.string();
  config.system_prompt = std::move(system_prompt);

  ChatServiceRequestBuilder builder(std::move(config));
  return builder.BuildRequest({}, {});
}

}  // namespace

TEST_CASE("AGENTS.md content prepended to system prompt",
          "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("agents_prepend");
  WriteFile(workspace / "AGENTS.md", "Agents rules");

  const auto request = BuildRequest(workspace, std::string{"System prompt"});

  REQUIRE(request.messages.size() == 1);
  REQUIRE(request.messages.front().role == ChatRole::System);
  REQUIRE(request.messages.front().content == "Agents rules\n\nSystem prompt");
}

TEST_CASE("Falls back to CLAUDE.md when AGENTS.md missing",
          "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("claude_fallback");
  WriteFile(workspace / "CLAUDE.md", "Claude rules");

  const auto request = BuildRequest(workspace, std::nullopt);

  REQUIRE(request.messages.size() == 1);
  REQUIRE(request.messages.front().role == ChatRole::System);
  REQUIRE(request.messages.front().content == "Claude rules");
}

TEST_CASE("No file: no system message when system_prompt also absent",
          "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("no_file_no_prompt");

  const auto request = BuildRequest(workspace, std::nullopt);

  REQUIRE(request.messages.empty());
}

TEST_CASE("No file: system_prompt preserved when AGENTS.md absent",
          "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("prompt_only");

  const auto request = BuildRequest(workspace, std::string{"System prompt"});

  REQUIRE(request.messages.size() == 1);
  REQUIRE(request.messages.front().role == ChatRole::System);
  REQUIRE(request.messages.front().content == "System prompt");
}

TEST_CASE("Concatenated with config.system_prompt",
          "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("concat_prompt");
  WriteFile(workspace / "AGENTS.md", "Agents rules");

  const auto request = BuildRequest(workspace, std::string{"System prompt"});

  REQUIRE(request.messages.size() == 1);
  REQUIRE(request.messages.front().role == ChatRole::System);
  REQUIRE(request.messages.front().content == "Agents rules\n\nSystem prompt");
}

TEST_CASE("Truncates AGENTS.md at 8KB", "[agents_md][request_builder]") {
  const auto workspace = PrepareWorkspace("truncate_agents");
  WriteFile(workspace / "AGENTS.md", std::string(9000, 'a'));

  const auto request = BuildRequest(workspace, std::nullopt);

  REQUIRE(request.messages.size() == 1);
  REQUIRE(request.messages.front().role == ChatRole::System);
  REQUIRE(request.messages.front().content.size() == 8192);
  REQUIRE(request.messages.front().content == std::string(8192, 'a'));
}
