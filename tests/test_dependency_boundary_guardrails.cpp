#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef YAC_PROJECT_ROOT
#define YAC_PROJECT_ROOT "."  // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace {

struct SourceAlias {
  std::string path;
  int line_number;
  std::string line;

  friend bool operator<(const SourceAlias& lhs, const SourceAlias& rhs) {
    if (lhs.path != rhs.path) {
      return lhs.path < rhs.path;
    }
    if (lhs.line_number != rhs.line_number) {
      return lhs.line_number < rhs.line_number;
    }
    return lhs.line < rhs.line;
  }

  friend bool operator==(const SourceAlias& lhs, const SourceAlias& rhs) {
    return lhs.path == rhs.path && lhs.line_number == rhs.line_number &&
           lhs.line == rhs.line;
  }
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

std::string Trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return "";
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::vector<std::string> ExtractQuotedStrings(std::string_view line) {
  std::vector<std::string> values;
  std::size_t cursor = 0;
  while (cursor < line.size()) {
    const auto open = line.find('"', cursor);
    if (open == std::string_view::npos) {
      break;
    }
    const auto close = line.find('"', open + 1);
    if (close == std::string_view::npos) {
      break;
    }
    values.emplace_back(line.substr(open + 1, close - open - 1));
    cursor = close + 1;
  }
  return values;
}

bool IsConcreteSourceTargetStart(std::string_view line) {
  return Contains(line, "yac_cc_library(") || Contains(line, "yac_cc_binary(");
}

std::set<std::string> ParseOwnedBuildSources(
    const std::filesystem::path& build_file) {
  std::set<std::string> sources;
  std::istringstream lines(ReadFile(build_file));
  std::string line;
  bool in_concrete_target = false;
  bool in_srcs = false;

  while (std::getline(lines, line)) {
    const std::string trimmed = Trim(line);
    if (!in_concrete_target && IsConcreteSourceTargetStart(trimmed)) {
      in_concrete_target = true;
      in_srcs = false;
      continue;
    }

    if (!in_concrete_target) {
      continue;
    }

    if (!in_srcs && trimmed.starts_with("srcs =")) {
      for (const auto& quoted : ExtractQuotedStrings(trimmed)) {
        if (quoted.ends_with(".cpp")) {
          sources.insert("src/" + quoted);
        }
      }
      if (Contains(trimmed, "[") && !Contains(trimmed, "]")) {
        in_srcs = true;
      }
    } else if (in_srcs) {
      for (const auto& quoted : ExtractQuotedStrings(trimmed)) {
        if (quoted.ends_with(".cpp")) {
          sources.insert("src/" + quoted);
        }
      }
      if (Contains(trimmed, "]")) {
        in_srcs = false;
      }
    }

    if (trimmed == ")") {
      in_concrete_target = false;
      in_srcs = false;
    }
  }

  return sources;
}

std::string ProjectRelativeSourcePath(const std::filesystem::path& path) {
  const std::string generic_path = path.generic_string();
  const std::string src_marker = "/src/";
  const auto marker_position = generic_path.rfind(src_marker);
  if (marker_position != std::string::npos) {
    return "src/" + generic_path.substr(marker_position + src_marker.size());
  }
  if (generic_path.starts_with("src/")) {
    return generic_path;
  }
  return generic_path;
}

std::set<std::string> ListCurrentCppSources(const std::filesystem::path& root) {
  std::set<std::string> sources;
  const std::filesystem::path src_root = root / "src";
  REQUIRE(std::filesystem::exists(src_root));

  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           src_root,
           std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
      continue;
    }
    sources.insert(ProjectRelativeSourcePath(entry.path()));
  }
  return sources;
}

std::vector<SourceAlias> ScanDirectOpenAiJsonAliases(
    const std::filesystem::path& root) {
  std::vector<SourceAlias> aliases;
  const std::filesystem::path src_root = root / "src";
  REQUIRE(std::filesystem::exists(src_root));

  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           src_root,
           std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension().string();
    if (extension != ".cpp" && extension != ".hpp") {
      continue;
    }
    std::istringstream lines(ReadFile(entry.path()));
    std::string line;
    int line_number = 0;
    while (std::getline(lines, line)) {
      ++line_number;
      if (Contains(line, "openai::_detail::Json")) {
        aliases.push_back(
            {ProjectRelativeSourcePath(entry.path()), line_number, Trim(line)});
      }
    }
  }

  std::ranges::sort(aliases, [](const SourceAlias& lhs,
                                const SourceAlias& rhs) { return lhs < rhs; });
  return aliases;
}

template <typename Values>
std::string DescribeValues(std::string_view title, const Values& values) {
  std::ostringstream out;
  out << title << '\n';
  for (const auto& value : values) {
    out << "  " << value << '\n';
  }
  return out.str();
}

std::string DescribeAliases(const std::vector<SourceAlias>& aliases) {
  std::ostringstream out;
  for (const auto& alias : aliases) {
    out << alias.path << ':' << alias.line_number << ": " << alias.line << '\n';
  }
  return out.str();
}

}  // namespace

TEST_CASE("src cpp files are owned by concrete Bazel targets",
          "[dependency_boundary_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const auto actual_sources = ListCurrentCppSources(root);
  const auto owned_sources = ParseOwnedBuildSources(root / "src/BUILD.bazel");

  std::vector<std::string> unowned_sources;
  std::ranges::set_difference(actual_sources, owned_sources,
                              std::back_inserter(unowned_sources));

  std::vector<std::string> stale_sources;
  std::ranges::set_difference(owned_sources, actual_sources,
                              std::back_inserter(stale_sources));

  INFO(
      DescribeValues("src/**/*.cpp files missing from concrete src/BUILD.bazel "
                     "target srcs:",
                     unowned_sources));
  INFO(
      DescribeValues("src/BUILD.bazel concrete target srcs with no matching "
                     "src/**/*.cpp file:",
                     stale_sources));
  REQUIRE(unowned_sources.empty());
  REQUIRE(stale_sources.empty());
}

TEST_CASE("direct openai private Json aliases stay inside approved inventory",
          "[dependency_boundary_guardrails]") {
  const std::filesystem::path root = YAC_PROJECT_ROOT;
  const std::vector<SourceAlias> expected_aliases = {
      {"src/provider/provider_json.hpp", 7,
       "using ProviderJson = openai::_detail::Json;"},
      {"src/tool_call/json.hpp", 7, "using Json = openai::_detail::Json;"},
  };

  const auto actual_aliases = ScanDirectOpenAiJsonAliases(root);

  INFO("Expected direct openai::_detail::Json inventory:\n" +
       DescribeAliases(expected_aliases));
  INFO("Actual direct openai::_detail::Json inventory:\n" +
       DescribeAliases(actual_aliases));
  REQUIRE(actual_aliases == expected_aliases);
}
