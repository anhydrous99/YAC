#include "tool_call/web_fetch.hpp"

#include "util/string_util.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <curl/curl.h>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::tool_call {
namespace {

using Json = nlohmann::json;

struct ParsedUrl {
  std::string scheme;
  std::string host;
};

struct CurlFetchState {
  const WebFetchTransportRequest* request = nullptr;
  std::stop_token stop_token;
  std::optional<std::string> callback_error;
};

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(
      lowered, lowered.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
  return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '>' || c == '/';
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
  for (int level = 1; level <= 6; ++level) {
    const std::string tag = "h" + std::to_string(level);
    const std::string prefix(static_cast<std::size_t>(level), '#');
    html = ConvertPairedTag(
        std::move(html), tag, [prefix](std::string_view inner) {
          const std::string text = StripTagsToText(inner);
          return text.empty() ? std::string{}
                              : "\n" + prefix + " " + text + "\n\n";
        });
  }
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

[[nodiscard]] std::string TransformWebFetchBody(std::string_view body,
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

[[nodiscard]] bool IsValidHostCharacter(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.' ||
         c == '_' || c == ':';
}

[[nodiscard]] ParsedUrl ParseHttpUrl(std::string_view url) {
  constexpr std::string_view kSchemeDelimiter = "://";
  const std::size_t scheme_end = url.find(kSchemeDelimiter);
  if (scheme_end == std::string_view::npos || scheme_end == 0) {
    throw std::runtime_error("Malformed URL");
  }

  ParsedUrl parsed;
  parsed.scheme = ToLowerAscii(url.substr(0, scheme_end));
  if (parsed.scheme != "http" && parsed.scheme != "https") {
    throw std::runtime_error("only http and https URLs are supported");
  }

  const std::size_t authority_start = scheme_end + kSchemeDelimiter.size();
  const std::size_t authority_end = url.find_first_of("/?#", authority_start);
  std::string_view authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    throw std::runtime_error("Malformed URL");
  }

  if (authority.front() == '[') {
    const std::size_t closing = authority.find(']');
    if (closing == std::string_view::npos) {
      throw std::runtime_error("Malformed URL");
    }
    parsed.host = std::string(authority.substr(1, closing - 1));
    if (closing + 1 < authority.size() && authority[closing + 1] != ':') {
      throw std::runtime_error("Malformed URL");
    }
  } else {
    const std::size_t colon = authority.rfind(':');
    const bool has_single_colon =
        colon != std::string_view::npos && authority.find(':') == colon;
    parsed.host =
        std::string(has_single_colon ? authority.substr(0, colon) : authority);
  }

  if (parsed.host.empty() ||
      !std::ranges::all_of(parsed.host, IsValidHostCharacter)) {
    throw std::runtime_error("Malformed URL");
  }
  return parsed;
}

[[nodiscard]] bool IsPrivateIpv4HostOrder(std::uint32_t ip) {
  return (ip >> 24) == 10 || (ip >> 24) == 127 ||
         (ip >= 0xAC100000 && ip <= 0xAC1FFFFF) ||
         (ip >= 0xC0A80000 && ip <= 0xC0A8FFFF) ||
         (ip >= 0xA9FE0000 && ip <= 0xA9FEFFFF) ||
         (ip >= 0x64400000 && ip <= 0x647FFFFF) ||
         ip <= 0x00FFFFFF;
}

[[nodiscard]] bool IsPrivateIpv4(const in_addr& address) {
  return IsPrivateIpv4HostOrder(ntohl(address.s_addr));
}

[[nodiscard]] bool IsPrivateIpv6(const in6_addr& address) {
  const auto& bytes = address.s6_addr;
  bool loopback_prefix = true;
  bool ipv4_mapped_prefix = true;
  for (std::size_t index = 0; index < 10; ++index) {
    loopback_prefix = loopback_prefix && bytes[index] == 0;
    ipv4_mapped_prefix = ipv4_mapped_prefix && bytes[index] == 0;
  }
  for (std::size_t index = 10; index < 15; ++index) {
    loopback_prefix = loopback_prefix && bytes[index] == 0;
  }
  const bool loopback = loopback_prefix && bytes[15] == 1;
  const bool ipv4_mapped =
      ipv4_mapped_prefix && bytes[10] == 0xff && bytes[11] == 0xff;
  const std::uint32_t mapped_ipv4 =
      (static_cast<std::uint32_t>(bytes[12]) << 24U) |
      (static_cast<std::uint32_t>(bytes[13]) << 16U) |
      (static_cast<std::uint32_t>(bytes[14]) << 8U) |
      static_cast<std::uint32_t>(bytes[15]);
  return loopback ||
         (ipv4_mapped && IsPrivateIpv4HostOrder(mapped_ipv4)) ||
         (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
         (bytes[0] & 0xfe) == 0xfc;
}

[[nodiscard]] bool HostResolvesToPrivateAddress(const std::string& host) {
  in_addr ipv4{};
  if (inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
    return IsPrivateIpv4(ipv4);
  }
  in6_addr ipv6{};
  if (inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
    return IsPrivateIpv6(ipv6);
  }

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* raw_results = nullptr;
  const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &raw_results);
  if (rc != 0) {
    return false;
  }
  const auto cleanup = [](addrinfo* info) { freeaddrinfo(info); };
  std::unique_ptr<addrinfo, decltype(cleanup)> results(raw_results, cleanup);
  for (const addrinfo* current = results.get(); current != nullptr;
       current = current->ai_next) {
    if (current->ai_family == AF_INET) {
      const auto* addr = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
      if (IsPrivateIpv4(addr->sin_addr)) {
        return true;
      }
    } else if (current->ai_family == AF_INET6) {
      const auto* addr =
          reinterpret_cast<const sockaddr_in6*>(current->ai_addr);
      if (IsPrivateIpv6(addr->sin6_addr)) {
        return true;
      }
    }
  }
  return false;
}

void EnforceUrlPolicy(const ParsedUrl& parsed, WebFetchNetworkPolicy policy) {
  if (policy != WebFetchNetworkPolicy::RealNetwork) {
    return;
  }
  if (HostResolvesToPrivateAddress(parsed.host)) {
    throw std::runtime_error("private network addresses are blocked");
  }
}

[[nodiscard]] std::chrono::milliseconds ClampTimeout(int timeout_seconds) {
  const int clamped =
      std::clamp(timeout_seconds, 1, kWebFetchMaxTimeoutSeconds);
  return std::chrono::seconds(clamped);
}

[[nodiscard]] std::optional<std::size_t> ParseContentLength(
    std::string_view value) {
  value = ::yac::util::TrimSv(value);
  std::size_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

[[nodiscard]] std::string NormalizeHeaderName(std::string_view name) {
  return ToLowerAscii(::yac::util::TrimSv(name));
}

[[nodiscard]] std::string SizeCapError() {
  return "response exceeded " + std::to_string(kWebFetchMaxBodyBytes) +
         " bytes";
}

[[nodiscard]] bool ContainsTimeout(std::string_view value) {
  const std::string lowered = ToLowerAscii(value);
  return lowered.find("timeout") != std::string::npos ||
         lowered.find("timedout") != std::string::npos ||
         lowered.find("timed out") != std::string::npos;
}

[[nodiscard]] std::runtime_error NormalizeTransportError(
    const std::exception& error) {
  const std::string message = error.what();
  if (ContainsTimeout(message)) {
    return std::runtime_error("web_fetch timeout");
  }
  return std::runtime_error(message);
}

void RequireHttpSuccess(long status_code) {
  if (status_code >= 200 && status_code < 300) {
    return;
  }
  std::ostringstream message;
  message << "web_fetch HTTP " << status_code;
  throw std::runtime_error(message.str());
}

struct ResponseCollector {
  std::string content_type;
  std::string body;

  void OnHeader(std::string_view line) {
    if (line.starts_with("HTTP/")) {
      content_type.clear();
      return;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      return;
    }

    const std::string name = NormalizeHeaderName(line.substr(0, colon));
    std::string value = ::yac::util::Trim(line.substr(colon + 1));
    if (name == "content-length") {
      const auto declared = ParseContentLength(value);
      if (declared.has_value() && *declared > kWebFetchMaxBodyBytes) {
        throw std::runtime_error(SizeCapError());
      }
    } else if (name == "content-type") {
      content_type = std::move(value);
    }
  }

  void OnBody(std::string_view chunk) {
    if (chunk.size() > kWebFetchMaxBodyBytes - body.size()) {
      throw std::runtime_error(SizeCapError());
    }
    body.append(chunk);
  }
};

[[nodiscard]] std::size_t CurlHeaderCallback(char* buffer, std::size_t size,
                                             std::size_t nitems,
                                             void* userdata) {
  const std::size_t bytes = size * nitems;
  auto* state = static_cast<CurlFetchState*>(userdata);
  if (state->request->on_header_line) {
    try {
      state->request->on_header_line(std::string_view(buffer, bytes));
    } catch (const std::exception& error) {
      state->callback_error = error.what();
      return 0;
    } catch (...) {
      state->callback_error = "HTTP header callback failed.";
      return 0;
    }
  }
  return bytes;
}

[[nodiscard]] std::size_t CurlBodyCallback(char* ptr, std::size_t size,
                                           std::size_t nmemb, void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* state = static_cast<CurlFetchState*>(userdata);
  if (state->request->on_body_chunk) {
    try {
      state->request->on_body_chunk(std::string_view(ptr, bytes));
    } catch (const std::exception& error) {
      state->callback_error = error.what();
      return 0;
    } catch (...) {
      state->callback_error = "HTTP body callback failed.";
      return 0;
    }
  }
  return bytes;
}

int CurlProgressCallback(void* clientp, curl_off_t download_total,
                         curl_off_t download_now, curl_off_t upload_total,
                         curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  const auto* state = static_cast<CurlFetchState*>(clientp);
  return state->stop_token.stop_requested() ? 1 : 0;
}

}  // namespace

WebFetchTransportResponse CurlWebFetchTransport::Fetch(
    const WebFetchTransportRequest& request, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_fetch cancelled");
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }
  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: text/html, text/plain, */*");
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  CurlFetchState state{.request = &request, .stop_token = stop_token};
  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CurlHeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlBodyCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(request.timeout.count()));
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
  const CURLcode result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_fetch cancelled");
  }
  if (state.callback_error.has_value()) {
    throw std::runtime_error(*state.callback_error);
  }
  if (result != CURLE_OK) {
    if (result == CURLE_OPERATION_TIMEDOUT) {
      throw std::runtime_error("web_fetch timeout");
    }
    throw std::runtime_error(curl_easy_strerror(result));
  }

  WebFetchTransportResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  return response;
}

WebFetchResponse FetchWebUrl(const WebFetchRequest& request,
                             WebFetchTransport& transport,
                             std::stop_token stop_token) {
  const ParsedUrl parsed = ParseHttpUrl(request.url);
  EnforceUrlPolicy(parsed, request.network_policy);

  ResponseCollector collector;
  WebFetchTransportRequest transport_request{
      .url = request.url,
      .timeout = ClampTimeout(request.timeout),
      .on_header_line =
          [&collector](std::string_view line) { collector.OnHeader(line); },
      .on_body_chunk =
          [&collector](std::string_view chunk) { collector.OnBody(chunk); }};
  WebFetchTransportResponse transport_response;
  try {
    transport_response = transport.Fetch(transport_request, stop_token);
  } catch (const std::exception& error) {
    throw NormalizeTransportError(error);
  }
  RequireHttpSuccess(transport_response.status_code);
  collector.body = TransformWebFetchBody(collector.body, collector.content_type,
                                         request.format);
  return WebFetchResponse{.url = request.url,
                          .status_code = transport_response.status_code,
                          .content_type = std::move(collector.content_type),
                          .body = std::move(collector.body)};
}

ToolExecutionResult ExecuteWebFetchTool(const WebFetchRequest& request,
                                        WebFetchTransport& transport,
                                        std::stop_token stop_token) {
  try {
    WebFetchResponse response = FetchWebUrl(request, transport, stop_token);
    WebFetchCall call{
        .url = response.url,
        .title = "",
        .excerpt = response.body,
        .format = request.format,
        .timeout = std::clamp(request.timeout, 1, kWebFetchMaxTimeoutSeconds)};
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"url", response.url},
                            {"status_code", response.status_code},
                            {"content_type", response.content_type},
                            {"body", response.body}}
                           .dump()};
  } catch (const std::exception& error) {
    WebFetchCall call{
        .url = request.url,
        .format = request.format,
        .timeout = std::clamp(request.timeout, 1, kWebFetchMaxTimeoutSeconds)};
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"error", error.what()}}.dump(),
        .is_error = true};
  }
}

}  // namespace yac::tool_call
