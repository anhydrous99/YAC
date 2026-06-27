#pragma once

#include "tool_call/web_fetch.hpp"

#include <string>
#include <string_view>

struct sockaddr;  // NOLINT(readability-identifier-naming) - libc type

namespace yac::tool_call {

struct ParsedUrl {
  std::string scheme;
  std::string host;
};

[[nodiscard]] ParsedUrl ParseHttpUrl(std::string_view url);
void EnforceUrlPolicy(const ParsedUrl& parsed, WebFetchNetworkPolicy policy);

// Returns true when `address` is a private/loopback/link-local peer that must
// not be reached under the real-network policy. Installed as curl's
// connect-time guard so the address actually dialed is validated, closing the
// DNS-rebinding window left by a separate pre-resolution check. Unknown address
// families are treated as private (fail closed).
[[nodiscard]] bool IsPrivateSocketAddress(const sockaddr* address);

}  // namespace yac::tool_call
