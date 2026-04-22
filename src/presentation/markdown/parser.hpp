#pragma once

#include "ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace yac::presentation::markdown {

struct ParseOptions {
  bool streaming{false};
};

class MarkdownParser {
 public:
  [[nodiscard]] static std::vector<BlockNode> Parse(std::string_view markdown);
  [[nodiscard]] static std::vector<BlockNode> Parse(std::string_view markdown,
                                                    const ParseOptions& opts);
};

}  // namespace yac::presentation::markdown
