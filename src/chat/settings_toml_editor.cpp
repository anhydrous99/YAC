#include "chat/settings_toml_editor.hpp"

#include "chat/reasoning_effort.hpp"
#include "util/string_util.hpp"

#include <iomanip>
#include <sstream>

namespace yac::chat {

namespace {

void AddError(std::vector<ConfigIssue>& issues, std::string message,
              std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Error,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

std::vector<TextLine> SplitPreservingNewlines(std::string_view content) {
  std::vector<TextLine> lines;
  size_t pos = 0;
  while (pos < content.size()) {
    const auto line_end = content.find_first_of("\r\n", pos);
    if (line_end == std::string_view::npos) {
      lines.push_back({std::string(content.substr(pos)), ""});
      break;
    }
    std::string newline;
    size_t next_pos = line_end + 1;
    if (content[line_end] == '\r' && next_pos < content.size() &&
        content[next_pos] == '\n') {
      newline = "\r\n";
      ++next_pos;
    } else {
      newline = content[line_end];
    }
    lines.push_back(
        {std::string(content.substr(pos, line_end - pos)), newline});
    pos = next_pos;
  }
  return lines;
}

std::string DetectNewline(const std::vector<TextLine>& lines) {
  for (const auto& line : lines) {
    if (!line.newline.empty()) {
      return line.newline;
    }
  }
  return "\n";
}

std::optional<std::string> ParseTableHeader(std::string_view line) {
  line = ::yac::util::TrimSv(line);
  if (line.empty() || line.front() != '[' || line.starts_with("[[")) {
    return std::nullopt;
  }
  const auto close = line.find(']');
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const auto rest = ::yac::util::TrimSv(line.substr(close + 1));
  if (!rest.empty() && rest.front() != '#') {
    return std::nullopt;
  }
  return std::string(::yac::util::TrimSv(line.substr(1, close - 1)));
}

std::optional<std::string> ParseArrayTableHeader(std::string_view line) {
  line = ::yac::util::TrimSv(line);
  if (!line.starts_with("[[")) {
    return std::nullopt;
  }
  const auto close = line.find("]]");
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const auto rest = ::yac::util::TrimSv(line.substr(close + 2));
  if (!rest.empty() && rest.front() != '#') {
    return std::nullopt;
  }
  return std::string(::yac::util::TrimSv(line.substr(2, close - 2)));
}

bool IsAnyTableHeader(std::string_view line) {
  return ParseTableHeader(line).has_value() ||
         ParseArrayTableHeader(line).has_value();
}

bool IsKeyAssignment(std::string_view line, std::string_view key) {
  line = ::yac::util::TrimLeftSv(line);
  if (line.empty() || line.front() == '#' || !line.starts_with(key)) {
    return false;
  }
  const auto rest = ::yac::util::TrimLeftSv(line.substr(key.size()));
  return !rest.empty() && rest.front() == '=';
}

std::string QuoteTomlString(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\t':
        output << "\\t";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\r':
        output << "\\r";
        break;
      default:
        if (ch < 0x20) {
          output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<int>(ch) << std::dec;
        } else {
          output << static_cast<char>(ch);
        }
    }
  }
  output << '"';
  return output.str();
}

std::string JoinLines(const std::vector<TextLine>& lines) {
  std::string output;
  for (const auto& line : lines) {
    output += line.text;
    output += line.newline;
  }
  return output;
}

}  // namespace

std::string WithThemeName(std::string_view content,
                          std::string_view theme_name) {
  auto lines = SplitPreservingNewlines(content);
  const auto newline = DetectNewline(lines);
  const auto assignment = "name = " + QuoteTomlString(theme_name);

  std::optional<size_t> theme_start;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto table_name = ParseTableHeader(lines[i].text);
    if (table_name == "theme") {
      theme_start = i;
      break;
    }
  }

  if (!theme_start.has_value()) {
    if (!lines.empty()) {
      if (lines.back().newline.empty()) {
        lines.back().newline = newline;
      }
      if (!lines.back().text.empty()) {
        lines.push_back({"", newline});
      }
    }
    lines.push_back({"[theme]", newline});
    lines.push_back({assignment, newline});
    return JoinLines(lines);
  }

  size_t theme_end = lines.size();
  for (size_t i = *theme_start + 1; i < lines.size(); ++i) {
    if (ParseTableHeader(lines[i].text).has_value()) {
      theme_end = i;
      break;
    }
  }

  for (size_t i = *theme_start + 1; i < theme_end; ++i) {
    if (IsKeyAssignment(lines[i].text, "name")) {
      lines[i].text = assignment;
      return JoinLines(lines);
    }
  }

  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*theme_start + 1),
               TextLine{.text = assignment, .newline = newline});
  return JoinLines(lines);
}

std::optional<size_t> MatchingModelSettingsIndex(toml::table& table,
                                                 const ProviderId& provider_id,
                                                 const ModelId& model) {
  auto* settings = table["provider"]["model_settings"].as_array();
  if (settings == nullptr) {
    return std::nullopt;
  }
  size_t entry_index = 0;
  for (auto& entry : *settings) {
    auto* entry_table = entry.as_table();
    if (entry_table != nullptr) {
      const auto* parsed_provider = (*entry_table)["provider"].as_string();
      const auto* parsed_model = (*entry_table)["model"].as_string();
      if (parsed_provider != nullptr && parsed_model != nullptr &&
          parsed_provider->get() == provider_id.value &&
          parsed_model->get() == model.value) {
        return entry_index;
      }
    }
    ++entry_index;
  }
  return std::nullopt;
}

std::optional<std::pair<size_t, size_t>> FindModelSettingsBlock(
    const std::vector<TextLine>& lines, size_t target_index) {
  size_t entry_index = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (ParseArrayTableHeader(lines[i].text) != "provider.model_settings") {
      continue;
    }
    if (entry_index != target_index) {
      ++entry_index;
      continue;
    }
    size_t end = lines.size();
    for (size_t j = i + 1; j < lines.size(); ++j) {
      if (IsAnyTableHeader(lines[j].text)) {
        end = j;
        break;
      }
    }
    return std::pair{i, end};
  }
  return std::nullopt;
}

std::string WithProviderModelEffort(std::string_view content,
                                    const ProviderId& provider_id,
                                    const ModelId& model,
                                    std::optional<ReasoningEffort> effort,
                                    std::optional<size_t> matching_index,
                                    std::vector<ConfigIssue>& issues) {
  auto lines = SplitPreservingNewlines(content);
  const auto newline = DetectNewline(lines);

  if (!matching_index.has_value()) {
    if (!effort.has_value()) {
      return std::string(content);
    }
    if (!lines.empty()) {
      if (lines.back().newline.empty()) {
        lines.back().newline = newline;
      }
      if (!lines.back().text.empty()) {
        lines.push_back({"", newline});
      }
    }
    lines.push_back({"[[provider.model_settings]]", newline});
    lines.push_back(
        {"provider = " + QuoteTomlString(provider_id.value), newline});
    lines.push_back({"model = " + QuoteTomlString(model.value), newline});
    lines.push_back({"effort = " + QuoteTomlString(std::string(
                                       ReasoningEffortToString(*effort))),
                     newline});
    return JoinLines(lines);
  }

  const auto block = FindModelSettingsBlock(lines, *matching_index);
  if (!block.has_value()) {
    AddError(issues, "Cannot edit provider.model_settings entry",
             "Use [[provider.model_settings]] table blocks for editable "
             "provider/model effort settings.");
    return std::string(content);
  }
  const auto [block_start, block_end] = *block;
  const auto assignment =
      effort.has_value()
          ? "effort = " +
                QuoteTomlString(std::string(ReasoningEffortToString(*effort)))
          : std::string{};

  for (size_t i = block_start + 1; i < block_end; ++i) {
    if (!IsKeyAssignment(lines[i].text, "effort")) {
      continue;
    }
    if (effort.has_value()) {
      lines[i].text = assignment;
    } else {
      lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return JoinLines(lines);
  }

  if (!effort.has_value()) {
    return JoinLines(lines);
  }

  size_t insert_at = block_end;
  for (size_t i = block_start + 1; i < block_end; ++i) {
    if (IsKeyAssignment(lines[i].text, "model")) {
      insert_at = i + 1;
      break;
    }
  }
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at),
               TextLine{.text = assignment, .newline = newline});
  return JoinLines(lines);
}

}  // namespace yac::chat
