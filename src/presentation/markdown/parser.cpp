#include "parser.hpp"

#include "block_parser.hpp"
#include "presentation/util/string_util.hpp"

namespace yac::presentation::markdown {

std::vector<BlockNode> MarkdownParser::Parse(std::string_view markdown) {
  return Parse(markdown, ParseOptions{});
}

std::vector<BlockNode> MarkdownParser::Parse(std::string_view markdown,
                                             const ParseOptions& opts) {
  auto lines = util::SplitLines(markdown);
  return parser_detail::ParseBlockNodes(lines, opts);
}

}  // namespace yac::presentation::markdown
