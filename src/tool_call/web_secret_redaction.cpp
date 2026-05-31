#include "tool_call/web_secret_redaction.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr std::string_view kRedactedPlaceholder = "[REDACTED]";

void RedactAllOccurrences(std::string& text, std::string_view secret) {
  if (secret.empty()) {
    return;
  }

  std::size_t search_from = 0;
  while (search_from < text.size()) {
    const std::size_t found = text.find(secret, search_from);
    if (found == std::string::npos) {
      break;
    }
    text.replace(found, secret.size(), kRedactedPlaceholder);
    search_from = found + kRedactedPlaceholder.size();
  }
}

}  // namespace

std::string RedactWebSecrets(std::string_view input,
                             const WebSecretRedactionConfig& config) {
  std::string redacted(input);
  std::vector<std::string_view> secrets;
  secrets.reserve(config.api_key_like_substrings.size());
  for (const auto& secret : config.api_key_like_substrings) {
    if (!secret.empty()) {
      secrets.push_back(secret);
    }
  }
  std::ranges::sort(secrets,
            [](std::string_view lhs, std::string_view rhs) {
              if (lhs.size() != rhs.size()) {
                return lhs.size() > rhs.size();
              }
              return lhs < rhs;
            });

  for (const std::string_view secret : secrets) {
    RedactAllOccurrences(redacted, secret);
  }
  return redacted;
}

}  // namespace yac::tool_call
