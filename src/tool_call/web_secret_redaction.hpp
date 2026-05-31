#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

struct WebSecretRedactionConfig {
  std::vector<std::string> api_key_like_substrings;
};

[[nodiscard]] std::string RedactWebSecrets(
    std::string_view input, const WebSecretRedactionConfig& config);

}  // namespace yac::tool_call
