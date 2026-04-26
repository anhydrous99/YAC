#pragma once

#include <string>
#include <string_view>

namespace yac::mcp {

std::string RedactSecrets(std::string_view raw);

}  // namespace yac::mcp
