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

bool HasAnyUnsupportedMarker(std::string_view line) {
  return Contains(line, "unsupported") || Contains(line, "out of scope") ||
         Contains(line, "Unsupported") || Contains(line, "Out of scope") ||
         Contains(line, "out-of-scope") || Contains(line, "not implemented") ||
         Contains(line, "exclude") || Contains(line, "MUST NOT");
}

bool IsDocumentationPath(std::string_view path) {
  return path == "README.md" || path == "settings.example.toml" ||
         path == "src/chat/settings_toml_template.hpp" ||
         path.starts_with("docs/");
}

bool IsGuardrailTest(std::string_view path) {
  return path == "tests/test_openai_auth_guardrails.cpp";
}

bool IsAllowedForbiddenTokenMatch(const Match& match) {
  if (IsGuardrailTest(match.path)) {
    return true;
  }
  return IsDocumentationPath(match.path) && HasAnyUnsupportedMarker(match.line);
}

bool IsSuspiciousOpenAiHeadlessLine(std::string_view line) {
  return Contains(line, "headless") &&
         (Contains(line, "OpenAI") || Contains(line, "openai") ||
          Contains(line, "OAuth") || Contains(line, "oauth") ||
          Contains(line, "device") || Contains(line, "auth"));
}

bool IsAllowedHeadlessMatch(const Match& match) {
  if (IsGuardrailTest(match.path)) {
    return true;
  }
  if (!IsSuspiciousOpenAiHeadlessLine(match.line)) {
    return true;
  }
  return IsDocumentationPath(match.path) && HasAnyUnsupportedMarker(match.line);
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

TEST_CASE("OpenAI auth has no device auth implementation tokens",
          "[openai_auth_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const std::vector<std::filesystem::path> scan_roots = {
      "src", "tests", "README.md", "docs", "settings.example.toml"};
  const std::vector<std::string_view> tokens = {
      "deviceauth", "device_code", "device/code", "device_authorization"};

  const auto matches = ScanForTokens(root, scan_roots, tokens);
  std::vector<Match> violations;
  for (const auto& match : matches) {
    if (!IsAllowedForbiddenTokenMatch(match)) {
      violations.push_back(match);
    }
  }

  INFO(DescribeMatches(violations));
  REQUIRE(violations.empty());
}

TEST_CASE("OpenAI auth has no headless auth support references",
          "[openai_auth_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const std::vector<std::filesystem::path> scan_roots = {
      "src", "tests", "README.md", "docs", "settings.example.toml"};
  const std::vector<std::string_view> tokens = {"headless"};

  const auto matches = ScanForTokens(root, scan_roots, tokens);
  std::vector<Match> violations;
  for (const auto& match : matches) {
    if (!IsAllowedHeadlessMatch(match)) {
      violations.push_back(match);
    }
  }

  INFO(DescribeMatches(violations));
  REQUIRE(violations.empty());
}
