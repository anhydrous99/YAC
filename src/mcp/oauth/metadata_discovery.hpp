#pragma once

#include "mcp/oauth/flow.hpp"

#include <string_view>

namespace yac::mcp::oauth {

OAuthDiscoveryResult DiscoverProtectedResource(
    std::string_view mcp_endpoint, std::string_view www_authenticate_header);

OAuthDiscoveryResult DiscoverAuthorizationServer(std::string_view as_url);

}  // namespace yac::mcp::oauth
