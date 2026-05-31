#pragma once

#include "tool_call/web_fetch.hpp"

#include <string>
#include <string_view>

namespace yac::tool_call {

struct ParsedUrl {
  std::string scheme;
  std::string host;
};

[[nodiscard]] ParsedUrl ParseHttpUrl(std::string_view url);
void EnforceUrlPolicy(const ParsedUrl& parsed, WebFetchNetworkPolicy policy);

}  // namespace yac::tool_call
