#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef YAC_PROJECT_ROOT
#define YAC_PROJECT_ROOT "."  // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace {

struct Match {
  std::string path;
  int line_number;
  std::string token;
  std::string line;
};

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input.good());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool Contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

bool IsPathOrSuffix(std::string_view path, std::string_view suffix) {
  if (path == suffix) {
    return true;
  }
  if (path.size() <= suffix.size() || !path.ends_with(suffix)) {
    return false;
  }
  return path[path.size() - suffix.size() - 1] == '/';
}

bool IsGuardrailTest(std::string_view path) {
  return IsPathOrSuffix(path, "tests/test_openai_auth_guardrails.cpp");
}

std::vector<Match> ScanForTokens(
    const std::filesystem::path& root,
    const std::vector<std::filesystem::path>& scan_roots,
    const std::vector<std::string_view>& tokens) {
  std::vector<Match> matches;
  for (const auto& scan_root : scan_roots) {
    const auto absolute_root = root / scan_root;
    if (!std::filesystem::exists(absolute_root)) {
      continue;
    }
    if (std::filesystem::is_regular_file(absolute_root)) {
      const auto relative = std::filesystem::relative(absolute_root, root);
      const std::string text = ReadFile(absolute_root);
      if (Contains(text, std::string_view{"\0", 1})) {
        continue;
      }
      std::istringstream lines(text);
      std::string line;
      int line_number = 0;
      while (std::getline(lines, line)) {
        ++line_number;
        for (const auto token : tokens) {
          if (Contains(line, token)) {
            matches.push_back({relative.generic_string(), line_number,
                               std::string(token), line});
          }
        }
      }
      continue;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             absolute_root,
             std::filesystem::directory_options::skip_permission_denied)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto relative = std::filesystem::relative(entry.path(), root);
      const std::string text = ReadFile(entry.path());
      if (Contains(text, std::string_view{"\0", 1})) {
        continue;
      }
      std::istringstream lines(text);
      std::string line;
      int line_number = 0;
      while (std::getline(lines, line)) {
        ++line_number;
        for (const auto token : tokens) {
          if (Contains(line, token)) {
            matches.push_back({relative.generic_string(), line_number,
                               std::string(token), line});
          }
        }
      }
    }
  }
  return matches;
}

std::string DescribeMatches(const std::vector<Match>& matches) {
  std::ostringstream out;
  for (const auto& match : matches) {
    out << match.path << ':' << match.line_number << " matched " << match.token
        << ": " << match.line << '\n';
  }
  return out.str();
}

}  // namespace

TEST_CASE("MCP OAuth implementation has no OpenAI Codex protocol tokens",
          "[openai_auth_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const std::vector<std::filesystem::path> scan_roots = {"src/mcp"};
  const std::vector<std::string_view> tokens = {
      "OpenAI", "openai", "Codex", "codex", "ChatGPT-Account-Id", "originator"};

  const auto matches = ScanForTokens(root, scan_roots, tokens);
  std::vector<Match> violations;
  for (const auto& match : matches) {
    if (!IsGuardrailTest(match.path)) {
      violations.push_back(match);
    }
  }

  INFO(DescribeMatches(violations));
  REQUIRE(violations.empty());
}

TEST_CASE("OpenAI API-key provider path has no Codex OAuth headers",
          "[openai_auth_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const std::vector<std::filesystem::path> scan_roots = {
      "src/provider/openai_compatible_chat_provider.cpp",
      "src/provider/openai_compatible_chat_provider.hpp",
      "src/provider/openai_compatible_chat_protocol.cpp",
      "src/provider/openai_compatible_chat_protocol.hpp"};
  const std::vector<std::string_view> tokens = {
      "ChatGPT-Account-Id", "originator:", "/backend-api/codex", "session_id:"};

  const auto matches = ScanForTokens(root, scan_roots, tokens);
  std::vector<Match> violations;
  for (const auto& match : matches) {
    if (!IsGuardrailTest(match.path)) {
      violations.push_back(match);
    }
  }

  INFO(DescribeMatches(violations));
  REQUIRE(violations.empty());
}
