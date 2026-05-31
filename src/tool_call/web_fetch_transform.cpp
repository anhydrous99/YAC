#include "tool_call/web_fetch_transform.hpp"

#include "util/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::tool_call {
namespace {

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

[[nodiscard]] std::size_t FindInsensitive(std::string_view haystack,
                                          std::string_view needle,
                                          std::size_t offset = 0) {
  if (needle.empty() || offset >= haystack.size()) {
    return std::string_view::npos;
  }
  const std::string lowered_haystack = ToLowerAscii(haystack.substr(offset));
  const std::string lowered_needle = ToLowerAscii(needle);
  const std::size_t found = lowered_haystack.find(lowered_needle);
  if (found == std::string::npos) {
    return std::string_view::npos;
  }
  return offset + found;
}

[[nodiscard]] bool IsTagBoundary(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '>' ||
         c == '/';
}

[[nodiscard]] std::size_t FindOpeningTag(std::string_view html,
                                         std::string_view tag,
                                         std::size_t offset = 0) {
  std::size_t search = offset;
  while (search < html.size()) {
    const std::size_t found =
        FindInsensitive(html, "<" + std::string(tag), search);
    if (found == std::string_view::npos) {
      return std::string_view::npos;
    }
    const std::size_t boundary = found + tag.size() + 1;
    if (boundary < html.size() && IsTagBoundary(html[boundary])) {
      return found;
    }
    search = found + 1;
  }
  return std::string_view::npos;
}

[[nodiscard]] std::string DecodeHtmlEntities(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  std::size_t i = 0;
  while (i < input.size()) {
    if (input[i] != '&') {
      output.push_back(input[i]);
      ++i;
      continue;
    }
    const std::size_t semi = input.find(';', i + 1);
    if (semi == std::string_view::npos) {
      output.push_back(input[i]);
      ++i;
      continue;
    }
    const std::string_view entity = input.substr(i, semi - i + 1);
    if (entity == "&amp;") {
      output.push_back('&');
    } else if (entity == "&lt;") {
      output.push_back('<');
    } else if (entity == "&gt;") {
      output.push_back('>');
    } else if (entity == "&quot;") {
      output.push_back('"');
    } else if (entity == "&#39;" || entity == "&apos;") {
      output.push_back('\'');
    } else if (entity == "&nbsp;") {
      output.push_back(' ');
    } else {
      output.append(entity);
    }
    i = semi + 1;
  }
  return output;
}

[[nodiscard]] std::string CollapseInlineWhitespace(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  bool in_space = false;
  for (const char c : input) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!in_space && !output.empty()) {
        output.push_back(' ');
      }
      in_space = true;
      continue;
    }
    output.push_back(c);
    in_space = false;
  }
  while (!output.empty() && output.back() == ' ') {
    output.pop_back();
  }
  return output;
}

[[nodiscard]] std::string CollapseMarkdownWhitespace(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  int consecutive_newlines = 0;
  bool in_space = false;
  for (const char c : input) {
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      while (!output.empty() && output.back() == ' ') {
        output.pop_back();
      }
      if (consecutive_newlines < 2) {
        output.push_back('\n');
      }
      ++consecutive_newlines;
      in_space = false;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!in_space && !output.empty() && output.back() != '\n') {
        output.push_back(' ');
      }
      in_space = true;
      continue;
    }
    output.push_back(c);
    consecutive_newlines = 0;
    in_space = false;
  }
  while (!output.empty() && (output.back() == ' ' || output.back() == '\n')) {
    output.pop_back();
  }
  return output;
}

[[nodiscard]] std::string RemoveElementBlocks(std::string html,
                                              std::string_view tag) {
  std::size_t search = 0;
  while (search < html.size()) {
    const std::size_t open = FindOpeningTag(html, tag, search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t open_end = html.find('>', open);
    if (open_end == std::string::npos) {
      html.erase(open);
      break;
    }
    const std::size_t close =
        FindInsensitive(html, "</" + std::string(tag), open_end + 1);
    if (close == std::string::npos) {
      html.erase(open, open_end - open + 1);
      search = open;
      continue;
    }
    const std::size_t close_end = html.find('>', close);
    html.erase(open, close_end == std::string::npos ? std::string::npos
                                                    : close_end - open + 1);
    search = open;
  }
  return html;
}

[[nodiscard]] std::string RemoveSingleTags(std::string html,
                                           std::string_view tag) {
  std::size_t search = 0;
  while (search < html.size()) {
    const std::size_t open = FindOpeningTag(html, tag, search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t close = html.find('>', open);
    html.erase(open, close == std::string::npos ? std::string::npos
                                                : close - open + 1);
    search = open;
  }
  return html;
}

[[nodiscard]] std::string RemoveHiddenHtml(std::string html) {
  for (const std::string_view tag : {"script", "style", "noscript"}) {
    html = RemoveElementBlocks(std::move(html), tag);
  }
  for (const std::string_view tag : {"meta", "link"}) {
    html = RemoveSingleTags(std::move(html), tag);
  }
  return html;
}

[[nodiscard]] std::string ReplaceTagWith(std::string html, std::string_view tag,
                                         std::string_view replacement) {
  std::size_t search = 0;
  while (search < html.size()) {
    const std::size_t open = FindOpeningTag(html, tag, search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t close = html.find('>', open);
    html.replace(
        open, close == std::string::npos ? std::string::npos : close - open + 1,
        replacement);
    search = open + replacement.size();
  }
  return html;
}

[[nodiscard]] std::string ReplaceClosingTagWith(std::string html,
                                                std::string_view tag,
                                                std::string_view replacement) {
  std::size_t search = 0;
  const std::string close_tag = "</" + std::string(tag);
  while (search < html.size()) {
    const std::size_t open = FindInsensitive(html, close_tag, search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t boundary = open + close_tag.size();
    if (boundary >= html.size() || !IsTagBoundary(html[boundary])) {
      search = open + 1;
      continue;
    }
    const std::size_t close = html.find('>', open);
    html.replace(
        open, close == std::string::npos ? std::string::npos : close - open + 1,
        replacement);
    search = open + replacement.size();
  }
  return html;
}

[[nodiscard]] std::string StripTagsToText(std::string_view html) {
  std::string output;
  output.reserve(html.size());
  bool in_tag = false;
  for (const char c : html) {
    if (c == '<') {
      in_tag = true;
      output.push_back(' ');
      continue;
    }
    if (c == '>') {
      in_tag = false;
      output.push_back(' ');
      continue;
    }
    if (!in_tag) {
      output.push_back(c);
    }
  }
  return CollapseInlineWhitespace(DecodeHtmlEntities(output));
}

[[nodiscard]] std::optional<std::string> ExtractAttribute(
    std::string_view tag, std::string_view name) {
  const std::string lowered = ToLowerAscii(tag);
  const std::string lowered_name = ToLowerAscii(name);
  std::size_t search = 0;
  while (search < lowered.size()) {
    const std::size_t found = lowered.find(lowered_name, search);
    if (found == std::string::npos) {
      return std::nullopt;
    }
    const bool left_boundary =
        found == 0 ||
        std::isalnum(static_cast<unsigned char>(lowered[found - 1])) == 0;
    std::size_t cursor = found + lowered_name.size();
    while (cursor < lowered.size() &&
           std::isspace(static_cast<unsigned char>(lowered[cursor])) != 0) {
      ++cursor;
    }
    if (!left_boundary || cursor >= lowered.size() || lowered[cursor] != '=') {
      search = found + 1;
      continue;
    }
    ++cursor;
    while (cursor < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= tag.size()) {
      return std::nullopt;
    }
    if (tag[cursor] == '"' || tag[cursor] == '\'') {
      const char quote = tag[cursor++];
      const std::size_t end = tag.find(quote, cursor);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      return DecodeHtmlEntities(tag.substr(cursor, end - cursor));
    }
    const std::size_t end = tag.find_first_of(" \t\r\n>", cursor);
    return DecodeHtmlEntities(tag.substr(cursor, end == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : end - cursor));
  }
  return std::nullopt;
}

[[nodiscard]] std::string ConvertLinksToMarkdown(std::string html) {
  std::size_t search = 0;
  while (search < html.size()) {
    const std::size_t open = FindOpeningTag(html, "a", search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t open_end = html.find('>', open);
    if (open_end == std::string::npos) {
      break;
    }
    const std::size_t close = FindInsensitive(html, "</a", open_end + 1);
    if (close == std::string::npos) {
      search = open_end + 1;
      continue;
    }
    const std::size_t close_end = html.find('>', close);
    const std::string text =
        StripTagsToText(html.substr(open_end + 1, close - open_end - 1));
    const std::optional<std::string> href =
        ExtractAttribute(html.substr(open, open_end - open + 1), "href");
    const std::string replacement = href.has_value() && !href->empty()
                                        ? "[" + text + "](" + *href + ")"
                                        : text;
    html.replace(open,
                 close_end == std::string::npos ? std::string::npos
                                                : close_end - open + 1,
                 replacement);
    search = open + replacement.size();
  }
  return html;
}

[[nodiscard]] std::string ConvertPairedTag(
    std::string html, std::string_view tag,
    const std::function<std::string(std::string_view)>& convert) {
  std::size_t search = 0;
  while (search < html.size()) {
    const std::size_t open = FindOpeningTag(html, tag, search);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t open_end = html.find('>', open);
    if (open_end == std::string::npos) {
      break;
    }
    const std::size_t close =
        FindInsensitive(html, "</" + std::string(tag), open_end + 1);
    if (close == std::string::npos) {
      search = open_end + 1;
      continue;
    }
    const std::size_t close_end = html.find('>', close);
    const std::string replacement =
        convert(html.substr(open_end + 1, close - open_end - 1));
    html.replace(open,
                 close_end == std::string::npos ? std::string::npos
                                                : close_end - open + 1,
                 replacement);
    search = open + replacement.size();
  }
  return html;
}

[[nodiscard]] std::string ConvertHeadingTagsToMarkdown(std::string html) {
  for (int level = 1; level <= 6; ++level) {
    const std::string tag = "h" + std::to_string(level);
    const std::string prefix(static_cast<std::size_t>(level), '#');
    std::size_t search = 0;
    while (search < html.size()) {
      const std::size_t open = FindOpeningTag(html, tag, search);
      if (open == std::string::npos) {
        break;
      }
      const std::size_t open_end = html.find('>', open);
      if (open_end == std::string::npos) {
        break;
      }
      const std::size_t close = FindInsensitive(html, "</" + tag, open_end + 1);
      if (close == std::string::npos) {
        search = open_end + 1;
        continue;
      }
      const std::size_t close_end = html.find('>', close);
      const std::string text =
          StripTagsToText(html.substr(open_end + 1, close - open_end - 1));
      std::string replacement;
      if (!text.empty()) {
        replacement.reserve(prefix.size() + text.size() + 5);
        replacement.append("\n");
        replacement.append(prefix);
        replacement.append(" ");
        replacement.append(text);
        replacement.append("\n\n");
      }
      html.replace(open,
                   close_end == std::string::npos ? std::string::npos
                                                  : close_end - open + 1,
                   replacement);
      search = open + replacement.size();
    }
  }
  return html;
}

[[nodiscard]] std::string VisibleTextFromHtml(std::string html) {
  html = RemoveHiddenHtml(std::move(html));
  for (const std::string_view tag :
       {"br", "p", "div", "section", "article", "header", "footer", "li", "tr",
        "table", "h1", "h2", "h3", "h4", "h5", "h6"}) {
    html = ReplaceTagWith(std::move(html), tag, "\n");
    html = ReplaceClosingTagWith(std::move(html), tag, "\n");
  }
  return CollapseMarkdownWhitespace(StripTagsToText(html));
}

[[nodiscard]] std::string MarkdownFromHtml(std::string html) {
  html = ConvertLinksToMarkdown(RemoveHiddenHtml(std::move(html)));
  html = ConvertHeadingTagsToMarkdown(std::move(html));
  html = ConvertPairedTag(std::move(html), "li", [](std::string_view inner) {
    const std::string text = StripTagsToText(inner);
    return text.empty() ? std::string{} : "\n- " + text + "\n";
  });
  html = ConvertPairedTag(std::move(html), "p", [](std::string_view inner) {
    const std::string text = StripTagsToText(inner);
    return text.empty() ? std::string{} : "\n" + text + "\n\n";
  });
  for (const std::string_view tag : {"td", "th"}) {
    html = ReplaceTagWith(std::move(html), tag, " | ");
    html = ReplaceClosingTagWith(std::move(html), tag, " ");
  }
  html = ReplaceTagWith(std::move(html), "tr", "\n");
  html = ReplaceClosingTagWith(std::move(html), "tr", "\n");
  for (const std::string_view tag : {"br", "div", "section", "article", "table",
                                     "thead", "tbody", "ul", "ol"}) {
    html = ReplaceTagWith(std::move(html), tag, "\n");
    html = ReplaceClosingTagWith(std::move(html), tag, "\n");
  }
  return CollapseMarkdownWhitespace(StripTagsToText(html));
}

[[nodiscard]] std::string BaseContentType(std::string_view content_type) {
  const std::size_t semicolon = content_type.find(';');
  return ToLowerAscii(::yac::util::TrimSv(content_type.substr(0, semicolon)));
}

[[nodiscard]] bool IsHtmlContentType(std::string_view content_type) {
  const std::string base = BaseContentType(content_type);
  return base == "text/html" || base == "application/xhtml+xml";
}

[[nodiscard]] bool IsTextContentType(std::string_view content_type) {
  const std::string base = BaseContentType(content_type);
  return base == "text/plain" || base.starts_with("text/");
}

}  // namespace

std::string TransformWebFetchBody(std::string_view body,
                                  std::string_view content_type,
                                  std::string_view format) {
  const std::string normalized_format = ToLowerAscii(format);
  const bool is_html = IsHtmlContentType(content_type);
  const bool is_text = IsTextContentType(content_type);
  if (!is_html && !is_text) {
    throw std::runtime_error("web_fetch cannot transform content type");
  }
  if (normalized_format == "html") {
    return std::string(body);
  }
  if (normalized_format == "text") {
    return is_html ? VisibleTextFromHtml(std::string(body)) : std::string(body);
  }
  if (normalized_format == "markdown") {
    return is_html ? MarkdownFromHtml(std::string(body)) : std::string(body);
  }
  throw std::runtime_error("web_fetch unsupported format");
}

}  // namespace yac::tool_call
