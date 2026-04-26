#pragma once

#include <string>
#include <string_view>

namespace yac::mcp::oauth {

[[nodiscard]] std::string GenerateCodeVerifier();
[[nodiscard]] std::string DeriveCodeChallenge(std::string_view verifier);

}  // namespace yac::mcp::oauth
