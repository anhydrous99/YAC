#pragma once

#include "ast.hpp"

#include <string_view>
#include <vector>

namespace yac::presentation::markdown::parser_detail {

[[nodiscard]] std::vector<InlineNode> ParseInlineNodes(std::string_view text);

}  // namespace yac::presentation::markdown::parser_detail
