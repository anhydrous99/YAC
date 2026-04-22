#pragma once

#include "ast.hpp"
#include "parser.hpp"

#include <string>
#include <vector>

namespace yac::presentation::markdown::parser_detail {

[[nodiscard]] std::vector<BlockNode> ParseBlockNodes(
    const std::vector<std::string>& lines, const ParseOptions& opts);

}  // namespace yac::presentation::markdown::parser_detail
