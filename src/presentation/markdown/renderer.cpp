#include "renderer.hpp"

#include "ftxui/dom/elements.hpp"
#include "presentation/util/string_util.hpp"

#include <string>

namespace yac::presentation::markdown {

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks) {
  return Render(blocks, RenderContext{});
}

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks,
                                        const RenderContext& context) {
  ftxui::Elements elements;
  for (size_t i = 0; i < blocks.size(); ++i) {
    elements.push_back(RenderBlock(blocks[i], context));
    if (i + 1 < blocks.size()) {
      elements.push_back(ftxui::text(""));
    }
  }
  return ftxui::vbox(elements);
}

ftxui::Element MarkdownRenderer::RenderBlock(const BlockNode& block,
                                             const RenderContext& context) {
  return std::visit(
      [&context](const auto& node) -> ftxui::Element {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Heading>) {
          return RenderHeading(node, context);
        } else if constexpr (std::is_same_v<T, Paragraph>) {
          return RenderParagraph(node, context);
        } else if constexpr (std::is_same_v<T, CodeBlock>) {
          return RenderCodeBlock(node, context);
        } else if constexpr (std::is_same_v<T, Blockquote>) {
          return RenderBlockquote(node, context);
        } else if constexpr (std::is_same_v<T, UnorderedList>) {
          return RenderUnorderedList(node, context);
        } else if constexpr (std::is_same_v<T, OrderedList>) {
          return RenderOrderedList(node, context);
        } else if constexpr (std::is_same_v<T, HorizontalRule>) {
          return RenderHorizontalRule(context);
        } else {
          return ftxui::text("");
        }
      },
      block);
}

namespace {

ftxui::Elements SplitIntoWords(const std::string& content,
                               const ftxui::Decorator& style) {
  ftxui::Elements elements;
  size_t start = 0;
  while (start < content.size()) {
    auto pos = content.find(' ', start);
    if (pos == std::string::npos) {
      pos = content.size();
    }
    auto word = content.substr(start, pos - start);
    if (!word.empty()) {
      elements.push_back(ftxui::text(word) | style);
    }
    if (pos < content.size()) {
      elements.push_back(ftxui::text(" "));
    }
    start = pos + 1;
  }
  return elements;
}

}  // namespace

ftxui::Element MarkdownRenderer::RenderInline(
    const std::vector<InlineNode>& nodes, const RenderContext& context) {
  ftxui::Elements elements;
  for (const auto& node : nodes) {
    auto node_elements = RenderInlineWords(node, context);
    elements.insert(elements.end(), node_elements.begin(), node_elements.end());
  }
  return ftxui::hflow(elements);
}

ftxui::Elements MarkdownRenderer::RenderInlineWords(
    const InlineNode& node, const RenderContext& context) {
  const auto& theme = context.Colors();
  return std::visit(
      [&theme](const auto& n) -> ftxui::Elements {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, Text>) {
          return SplitIntoWords(n.content,
                                ftxui::color(theme.chrome.body_text));
        } else if constexpr (std::is_same_v<T, Bold>) {
          return SplitIntoWords(
              n.content, ftxui::bold | ftxui::color(theme.chrome.body_text));
        } else if constexpr (std::is_same_v<T, Italic>) {
          return SplitIntoWords(
              n.content, ftxui::italic | ftxui::color(theme.chrome.body_text));
        } else if constexpr (std::is_same_v<T, Strikethrough>) {
          return SplitIntoWords(
              n.content,
              ftxui::strikethrough | ftxui::color(theme.chrome.body_text));
        } else if constexpr (std::is_same_v<T, InlineCode>) {
          return {ftxui::text(" " + n.content + " ") | ftxui::bold |
                  ftxui::bgcolor(theme.code.inline_bg) |
                  ftxui::color(theme.code.inline_fg)};
        } else if constexpr (std::is_same_v<T, Link>) {
          return {ftxui::text(n.text) | ftxui::color(theme.markdown.link) |
                  ftxui::underlined};
        } else {
          return {ftxui::text("")};
        }
      },
      node);
}

ftxui::Element MarkdownRenderer::RenderHeading(const Heading& h,
                                               const RenderContext& context) {
  const auto& theme = context.Colors();
  auto inline_elem = RenderInline(h.children, context);

  if (h.level <= 2) {
    return ftxui::vbox({
        inline_elem | ftxui::bold | ftxui::color(theme.markdown.heading),
        ftxui::separator() | ftxui::color(theme.markdown.separator),
    });
  }

  if (h.level <= 4) {
    return inline_elem | ftxui::bold | ftxui::color(theme.markdown.heading);
  }

  return inline_elem | ftxui::color(theme.chrome.dim_text);
}

ftxui::Element MarkdownRenderer::RenderParagraph(const Paragraph& p,
                                                 const RenderContext& context) {
  return RenderInline(p.children, context);
}

ftxui::Element MarkdownRenderer::RenderCodeBlock(const CodeBlock& cb,
                                                 const RenderContext& context) {
  const auto& theme = context.Colors();
  auto code_lines = util::SplitLines(cb.source);
  auto gutter_width = std::to_string(code_lines.size()).size() + 1;

  ftxui::Elements inner;
  if (!cb.language.empty()) {
    auto label_pad = std::string(gutter_width, ' ');
    inner.push_back(ftxui::hbox({
        ftxui::text(label_pad) | ftxui::bgcolor(theme.code.bg),
        ftxui::text(" " + cb.language + " ") | ftxui::bgcolor(theme.code.fg) |
            ftxui::color(theme.code.bg) | ftxui::bold,
    }));
  }

  auto even_bg = theme.code.bg;
  auto odd_bg = theme.code.alt_bg;

  for (size_t i = 0; i < code_lines.size(); ++i) {
    auto num_str = std::to_string(i + 1);
    auto padded_num =
        std::string(gutter_width - num_str.size(), ' ') + num_str + " ";
    auto line_elem = syntax::SyntaxHighlighter::Highlight(code_lines[i],
                                                          cb.language, context);

    auto bg = (i % 2 == 1) ? odd_bg : even_bg;
    inner.push_back(ftxui::hbox({
        ftxui::text(padded_num) | ftxui::color(theme.chrome.dim_text) |
            ftxui::dim | ftxui::bgcolor(bg),
        line_elem | ftxui::bgcolor(bg),
    }));
  }

  return ftxui::vbox(inner) | ftxui::bgcolor(theme.code.bg) |
         ftxui::borderRounded | ftxui::color(theme.code.block_border);
}

ftxui::Element MarkdownRenderer::RenderBlockquote(
    const Blockquote& bq, const RenderContext& context) {
  const auto& theme = context.Colors();
  const std::array<ftxui::Color, 4> border_colors = {
      theme.markdown.quote_border,
      ftxui::Color::RGB(203, 166, 247),
      ftxui::Color::RGB(137, 180, 250),
      ftxui::Color::RGB(166, 227, 161),
  };

  auto color_idx = static_cast<size_t>(bq.nesting_level) % border_colors.size();
  const auto& border_color = border_colors.at(color_idx);

  std::string indent(static_cast<size_t>(bq.nesting_level) * 2, ' ');

  ftxui::Elements line_elements;
  for (const auto& line : bq.lines) {
    line_elements.push_back(
        ftxui::hbox({ftxui::text(indent + "│ ") | ftxui::color(border_color),
                     RenderInline(line, context)}));
  }

  return ftxui::vbox(line_elements) | ftxui::bgcolor(theme.markdown.quote_bg);
}

ftxui::Element MarkdownRenderer::RenderUnorderedList(
    const UnorderedList& ul, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements items;
  for (const auto& item : ul.items) {
    items.push_back(ftxui::hbox(
        {ftxui::text("  "),
         ftxui::text("• ") | ftxui::color(theme.role.agent) | ftxui::bold,
         RenderInline(item.children, context)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderOrderedList(
    const OrderedList& ol, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements items;
  for (size_t i = 0; i < ol.items.size(); ++i) {
    auto num = std::to_string(i + 1) + ". ";
    items.push_back(ftxui::hbox(
        {ftxui::text("  "),
         ftxui::text(num) | ftxui::color(theme.chrome.dim_text) | ftxui::bold,
         RenderInline(ol.items[i].children, context)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderHorizontalRule(
    const RenderContext& context) {
  return ftxui::separator() | ftxui::color(context.Colors().markdown.separator);
}

}  // namespace yac::presentation::markdown
