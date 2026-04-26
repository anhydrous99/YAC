#include "chat/config.hpp"

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

}  // namespace

TEST_CASE("override_command") {
  TempFile file("yac_test_mcp_env_override.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"-y\", \"@upstash/context7-mcp\"]\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_CTX7_COMMAND", "custom-bin");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  REQUIRE(result.config.mcp.servers[0].command == "custom-bin");
}

TEST_CASE("unknown_server_ignored") {
  TempFile file("yac_test_mcp_env_unknown.toml");
  WriteFile(file.Path(), "temperature = 0.5\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_NONEXIST_COMMAND", "x");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.empty());
}
