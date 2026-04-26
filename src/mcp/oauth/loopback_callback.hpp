#pragma once

#include "mcp/oauth/flow.hpp"

#include <iostream>
#include <istream>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace yac::mcp::oauth {

// Single-use loopback HTTP server bound to 127.0.0.1. Binds on construction;
// the accept loop polls stop_requested() every 100 ms via select().
class LoopbackCallbackServer {
 public:
  LoopbackCallbackServer();
  ~LoopbackCallbackServer();

  LoopbackCallbackServer(const LoopbackCallbackServer&) = delete;
  LoopbackCallbackServer& operator=(const LoopbackCallbackServer&) = delete;
  LoopbackCallbackServer(LoopbackCallbackServer&&) = delete;
  LoopbackCallbackServer& operator=(LoopbackCallbackServer&&) = delete;

  [[nodiscard]] std::string RedirectUri() const;

  [[nodiscard]] std::optional<std::pair<std::string, std::string>>
  RunUntilCallback(std::stop_token stop_token);

 private:
  int listen_fd_ = -1;
  unsigned short port_ = 0;
};

// Drives user interaction for an OAuth PKCE flow. Behaviour depends on mode:
//   browser_disabled=false                  -> loopback server + browser launch
//   browser_disabled=true, injected_url set -> parse code+state from URL
//   directly browser_disabled=true, no injected_url  -> print auth_url, read
//   line from in
[[nodiscard]] std::optional<std::pair<std::string, std::string>>
RunOAuthInteraction(const OAuthInteractionMode& mode, std::string_view auth_url,
                    std::stop_token stop_token, std::istream& in = std::cin);

}  // namespace yac::mcp::oauth
