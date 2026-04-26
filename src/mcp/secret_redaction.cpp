#include "mcp/secret_redaction.hpp"

#include <cstddef>
#include <regex.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp {
namespace {

// Sensitive keys and log shapes that must be redacted before they can reach
// logs.
constexpr std::string_view kSensitiveJsonQuotedPattern =
    R"re(("(Authorization|access_token|refresh_token|id_token|client_secret|Mcp-Session-Id)"[[:space:]]*:[[:space:]]*)"([^"\\]|\\.)*")re";
constexpr std::string_view kSensitiveJsonFallbackPattern =
    R"re(("(Authorization|access_token|refresh_token|id_token|client_secret|Mcp-Session-Id)"[[:space:]]*:[[:space:]]*)([^,[:space:]}\]]+))re";
constexpr std::string_view kAuthorizationHeaderPattern =
    R"re((^|[\r\n])Authorization:[[:space:]]*Bearer[[:space:]]+[A-Za-z0-9._+/=-]+)re";
constexpr std::string_view kBearerTokenPattern =
    R"re((^|[^[:alnum:]_])Bearer[[:space:]]+[A-Za-z0-9._+/=-]+)re";
constexpr std::string_view kOAuthCodePattern =
    R"re(([?&]code=)[^&#[:space:]"']+)re";

struct CompiledRegex {
  explicit CompiledRegex(std::string_view pattern) {
    const int rc = regcomp(&regex, std::string(pattern).c_str(),
                           REG_EXTENDED | REG_NEWLINE);
    if (rc != 0) {
      throw std::runtime_error("failed to compile redaction regex");
    }
  }

  CompiledRegex(const CompiledRegex&) = delete;
  CompiledRegex& operator=(const CompiledRegex&) = delete;
  CompiledRegex(CompiledRegex&&) = delete;
  CompiledRegex& operator=(CompiledRegex&&) = delete;

  ~CompiledRegex() { regfree(&regex); }

  regex_t regex{};
};

const CompiledRegex& SensitiveJsonQuotedRegex() {
  static const CompiledRegex regex(kSensitiveJsonQuotedPattern);
  return regex;
}

const CompiledRegex& SensitiveJsonFallbackRegex() {
  static const CompiledRegex regex(kSensitiveJsonFallbackPattern);
  return regex;
}

const CompiledRegex& AuthorizationHeaderRegex() {
  static const CompiledRegex regex(kAuthorizationHeaderPattern);
  return regex;
}

const CompiledRegex& BearerTokenRegex() {
  static const CompiledRegex regex(kBearerTokenPattern);
  return regex;
}

const CompiledRegex& OAuthCodeRegex() {
  static const CompiledRegex regex(kOAuthCodePattern);
  return regex;
}

std::string Capture(std::string_view text, const regmatch_t* matches,
                    std::size_t match_count, std::size_t group_index,
                    std::size_t base_offset) {
  if (group_index >= match_count) {
    return {};
  }
  const regmatch_t& match = matches[group_index];
  if (match.rm_so < 0 || match.rm_eo < 0) {
    return {};
  }

  const std::size_t begin = base_offset + static_cast<std::size_t>(match.rm_so);
  const auto length = static_cast<std::size_t>(match.rm_eo - match.rm_so);
  return std::string(text.substr(begin, length));
}

template <typename Builder>
std::string ReplaceAll(std::string_view raw, const regex_t& regex,
                       Builder&& build_replacement) {
  std::string output;
  output.reserve(raw.size());

  std::size_t segment_start = 0;
  while (segment_start <= raw.size()) {
    const std::size_t segment_end = raw.find('\0', segment_start);
    const std::size_t current_end =
        segment_end == std::string_view::npos ? raw.size() : segment_end;
    const std::string segment(
        raw.substr(segment_start, current_end - segment_start));

    const char* search = segment.c_str();
    const char* const segment_end_ptr = search + segment.size();
    while (search <= segment_end_ptr) {
      const std::size_t match_count = regex.re_nsub + 1;
      std::vector<regmatch_t> matches(match_count);
      const int rc = regexec(&regex, search, match_count, matches.data(), 0);
      if (rc == REG_NOMATCH) {
        output.append(search,
                      static_cast<std::size_t>(segment_end_ptr - search));
        break;
      }
      if (rc != 0 || matches[0].rm_so < 0 || matches[0].rm_eo < 0) {
        output.append(search,
                      static_cast<std::size_t>(segment_end_ptr - search));
        break;
      }

      const auto match_begin = static_cast<std::size_t>(matches[0].rm_so);
      const auto match_end = static_cast<std::size_t>(matches[0].rm_eo);
      output.append(search, match_begin);
      output.append(build_replacement(segment, search - segment.c_str(),
                                      matches.data(), match_count));
      search += match_end;
      if (match_end == 0) {
        ++search;
      }
    }

    if (current_end == raw.size()) {
      break;
    }
    output.push_back('\0');
    segment_start = current_end + 1;
  }

  return output;
}

std::string RedactJsonQuoted(std::string_view raw) {
  return ReplaceAll(raw, SensitiveJsonQuotedRegex().regex,
                    [](std::string_view segment, std::size_t base_offset,
                       const regmatch_t* matches, std::size_t match_count) {
                      return Capture(segment, matches, match_count, 1,
                                     base_offset) +
                             "\"[REDACTED]\"";
                    });
}

std::string RedactJsonFallback(std::string_view raw) {
  return ReplaceAll(raw, SensitiveJsonFallbackRegex().regex,
                    [](std::string_view segment, std::size_t base_offset,
                       const regmatch_t* matches, std::size_t match_count) {
                      return Capture(segment, matches, match_count, 1,
                                     base_offset) +
                             "[REDACTED]";
                    });
}

std::string RedactAuthorizationHeader(std::string_view raw) {
  return ReplaceAll(raw, AuthorizationHeaderRegex().regex,
                    [](std::string_view segment, std::size_t base_offset,
                       const regmatch_t* matches, std::size_t match_count) {
                      return Capture(segment, matches, match_count, 1,
                                     base_offset) +
                             "Authorization: [REDACTED]";
                    });
}

std::string RedactBearerTokens(std::string_view raw) {
  return ReplaceAll(raw, BearerTokenRegex().regex,
                    [](std::string_view segment, std::size_t base_offset,
                       const regmatch_t* matches, std::size_t match_count) {
                      return Capture(segment, matches, match_count, 1,
                                     base_offset) +
                             "Bearer [REDACTED]";
                    });
}

std::string RedactOAuthCode(std::string_view raw) {
  return ReplaceAll(raw, OAuthCodeRegex().regex,
                    [](std::string_view segment, std::size_t base_offset,
                       const regmatch_t* matches, std::size_t match_count) {
                      return Capture(segment, matches, match_count, 1,
                                     base_offset) +
                             "[REDACTED]";
                    });
}

}  // namespace

std::string RedactSecrets(std::string_view raw) {
  std::string redacted = RedactAuthorizationHeader(raw);
  redacted = RedactJsonQuoted(redacted);
  redacted = RedactJsonFallback(redacted);
  redacted = RedactOAuthCode(redacted);
  redacted = RedactBearerTokens(redacted);
  return redacted;
}

}  // namespace yac::mcp
