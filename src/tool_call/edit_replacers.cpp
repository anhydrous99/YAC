#include "tool_call/edit_replacers.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr std::string_view kMultipleMatchesError =
    "Found multiple matches for oldString. Provide more surrounding lines in "
    "oldString to identify the correct match.";
constexpr std::string_view kOldStringNotFoundError =
    "oldString not found in content.";
constexpr size_t kContextLines = 3;

struct LineInfo {
  std::string_view text;
  size_t start = 0;
  size_t end = 0;
  size_t end_with_newline = 0;
};

struct MatchRange {
  size_t start = 0;
  size_t end = 0;
};

[[nodiscard]] bool IsWhitespace(unsigned char ch) {
  return std::isspace(ch) != 0;
}

[[nodiscard]] std::string_view TrimTrailingWhitespace(std::string_view text) {
  size_t end = text.size();
  while (end > 0 && IsWhitespace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(0, end);
}

[[nodiscard]] std::string CollapseWhitespaceRuns(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool in_whitespace = false;
  for (char ch : text) {
    if (IsWhitespace(static_cast<unsigned char>(ch))) {
      if (!in_whitespace) {
        normalized.push_back(' ');
        in_whitespace = true;
      }
      continue;
    }
    normalized.push_back(ch);
    in_whitespace = false;
  }
  return normalized;
}

[[nodiscard]] std::vector<LineInfo> SplitLinesWithOffsets(
    std::string_view text) {
  std::vector<LineInfo> lines;
  if (text.empty()) {
    return lines;
  }

  size_t line_start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') {
      continue;
    }
    LineInfo line;
    line.text = text.substr(line_start, i - line_start);
    line.start = line_start;
    line.end = i;
    line.end_with_newline = i + 1;
    lines.push_back(line);
    line_start = i + 1;
  }

  if (line_start < text.size()) {
    LineInfo line;
    line.text = text.substr(line_start);
    line.start = line_start;
    line.end = text.size();
    line.end_with_newline = text.size();
    lines.push_back(line);
  }

  return lines;
}

[[nodiscard]] std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  if (text.empty()) {
    return lines;
  }

  size_t line_start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') {
      continue;
    }
    lines.emplace_back(text.substr(line_start, i - line_start));
    line_start = i + 1;
  }

  if (line_start < text.size()) {
    lines.emplace_back(text.substr(line_start));
  }

  return lines;
}

template <typename Normalize>
[[nodiscard]] std::vector<MatchRange> FindLineWindowMatches(
    std::string_view content, std::string_view old_string,
    Normalize normalize) {
  const auto content_lines = SplitLinesWithOffsets(content);
  const auto old_lines = SplitLinesWithOffsets(old_string);
  if (old_lines.empty() || content_lines.size() < old_lines.size()) {
    return {};
  }

  const bool old_has_trailing_newline =
      !old_string.empty() && old_string.back() == '\n';
  std::vector<MatchRange> matches;
  for (size_t i = 0; i + old_lines.size() <= content_lines.size(); ++i) {
    bool matched = true;
    for (size_t j = 0; j < old_lines.size(); ++j) {
      if (normalize(content_lines[i + j].text) !=
          normalize(old_lines[j].text)) {
        matched = false;
        break;
      }
    }
    if (!matched) {
      continue;
    }
    const auto& first = content_lines[i];
    const auto& last = content_lines[i + old_lines.size() - 1];
    MatchRange match;
    match.start = first.start;
    match.end = old_has_trailing_newline ? last.end_with_newline : last.end;
    matches.push_back(match);
  }

  return matches;
}

[[nodiscard]] std::optional<std::string> ReplaceSingleMatch(
    std::string_view content, const std::vector<MatchRange>& matches,
    std::string_view new_string) {
  if (matches.empty()) {
    return std::nullopt;
  }
  if (matches.size() > 1) {
    throw std::runtime_error(std::string(kMultipleMatchesError));
  }

  std::string replaced(content);
  const auto& match = matches.front();
  replaced.replace(match.start, match.end - match.start, new_string);
  return replaced;
}

enum class DiffOpType { Add, Remove, Context };

struct DiffOp {
  DiffOpType type = DiffOpType::Context;
  std::string content;
};

}  // namespace

std::optional<std::string> SimpleReplacer(std::string_view content,
                                          std::string_view old_string,
                                          std::string_view new_string) {
  if (old_string.empty()) {
    return std::nullopt;
  }

  size_t count = 0;
  size_t match_pos = std::string_view::npos;
  size_t search_pos = 0;
  while ((search_pos = content.find(old_string, search_pos)) !=
         std::string_view::npos) {
    ++count;
    match_pos = search_pos;
    if (count > 1) {
      throw std::runtime_error(std::string(kMultipleMatchesError));
    }
    search_pos += old_string.size();
  }

  if (count == 0) {
    return std::nullopt;
  }

  std::string replaced(content);
  replaced.replace(match_pos, old_string.size(), new_string);
  return replaced;
}

std::optional<std::string> LineTrimmedReplacer(std::string_view content,
                                               std::string_view old_string,
                                               std::string_view new_string) {
  return ReplaceSingleMatch(
      content,
      FindLineWindowMatches(content, old_string,
                            [](std::string_view line) {
                              return std::string(TrimTrailingWhitespace(line));
                            }),
      new_string);
}

std::optional<std::string> WhitespaceNormalizedReplacer(
    std::string_view content, std::string_view old_string,
    std::string_view new_string) {
  return ReplaceSingleMatch(
      content,
      FindLineWindowMatches(
          content, old_string,
          [](std::string_view line) { return CollapseWhitespaceRuns(line); }),
      new_string);
}

std::string ReplaceAll(std::string_view content, std::string_view old_string,
                       std::string_view new_string) {
  if (old_string.empty()) {
    throw std::runtime_error(std::string(kOldStringNotFoundError));
  }

  std::string replaced;
  replaced.reserve(content.size());
  size_t search_pos = 0;
  size_t found = content.find(old_string, search_pos);
  if (found == std::string_view::npos) {
    throw std::runtime_error(std::string(kOldStringNotFoundError));
  }

  while (found != std::string_view::npos) {
    replaced.append(content.substr(search_pos, found - search_pos));
    replaced.append(new_string);
    search_pos = found + old_string.size();
    found = content.find(old_string, search_pos);
  }
  replaced.append(content.substr(search_pos));
  return replaced;
}

std::vector<DiffLine> ComputeDiff(std::string_view old_text,
                                  std::string_view new_text) {
  const auto old_lines = SplitLines(old_text);
  const auto new_lines = SplitLines(new_text);
  const size_t old_size = old_lines.size();
  const size_t new_size = new_lines.size();

  std::vector<std::vector<int>> dp(old_size + 1,
                                   std::vector<int>(new_size + 1, 0));
  for (size_t i = old_size; i-- > 0;) {
    for (size_t j = new_size; j-- > 0;) {
      if (old_lines[i] == new_lines[j]) {
        dp[i][j] = 1 + dp[i + 1][j + 1];
      } else {
        dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
      }
    }
  }

  std::vector<DiffOp> ops;
  size_t i = 0;
  size_t j = 0;
  while (i < old_size && j < new_size) {
    if (old_lines[i] == new_lines[j]) {
      DiffOp op;
      op.type = DiffOpType::Context;
      op.content = old_lines[i];
      ops.push_back(op);
      ++i;
      ++j;
      continue;
    }

    if (dp[i + 1][j] >= dp[i][j + 1]) {
      DiffOp op;
      op.type = DiffOpType::Remove;
      op.content = old_lines[i];
      ops.push_back(op);
      ++i;
      continue;
    }

    DiffOp op;
    op.type = DiffOpType::Add;
    op.content = new_lines[j];
    ops.push_back(op);
    ++j;
  }

  while (i < old_size) {
    DiffOp op;
    op.type = DiffOpType::Remove;
    op.content = old_lines[i++];
    ops.push_back(op);
  }
  while (j < new_size) {
    DiffOp op;
    op.type = DiffOpType::Add;
    op.content = new_lines[j++];
    ops.push_back(op);
  }

  if (ops.empty()) {
    return {};
  }

  std::vector<size_t> changed_indices;
  for (size_t index = 0; index < ops.size(); ++index) {
    if (ops[index].type != DiffOpType::Context) {
      changed_indices.push_back(index);
    }
  }

  std::vector<DiffLine> result;
  if (changed_indices.empty()) {
    result.reserve(ops.size());
    for (const auto& op : ops) {
      DiffLine line;
      line.type = DiffLine::Context;
      line.content = op.content;
      result.push_back(line);
    }
    return result;
  }

  std::vector<std::pair<size_t, size_t>> ranges;
  for (size_t index : changed_indices) {
    const size_t start = index > kContextLines ? index - kContextLines : 0;
    const size_t end =
        std::min(ops.size() - 1, index + static_cast<size_t>(kContextLines));
    if (!ranges.empty() && start <= ranges.back().second + 1) {
      ranges.back().second = std::max(ranges.back().second, end);
      continue;
    }
    ranges.emplace_back(start, end);
  }

  for (const auto& [start, end] : ranges) {
    for (size_t index = start; index <= end; ++index) {
      DiffLine::Type type = DiffLine::Context;
      switch (ops[index].type) {
        case DiffOpType::Add:
          type = DiffLine::Add;
          break;
        case DiffOpType::Remove:
          type = DiffLine::Remove;
          break;
        case DiffOpType::Context:
          type = DiffLine::Context;
          break;
      }
      DiffLine line;
      line.type = type;
      line.content = ops[index].content;
      result.push_back(line);
    }
  }

  return result;
}

}  // namespace yac::tool_call
