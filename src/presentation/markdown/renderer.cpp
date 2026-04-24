#include "renderer.hpp"

#include "../ui_spacing.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "presentation/util/string_util.hpp"

#include <string>
#include <utility>

namespace yac::presentation::markdown {

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks) {
  return Render(blocks, RenderContext{}, ftxui::Element{});
}

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks,
                                        const RenderContext& context) {
  return Render(blocks, context, ftxui::Element{});
}

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks,
                                        const RenderContext& context,
                                        ftxui::Element trailing_inline) {
  if (blocks.empty()) {
    return trailing_inline ? std::move(trailing_inline)
                           : ftxui::vbox({ftxui::text("")});
  }
  ftxui::Elements elements;
  for (size_t i = 0; i < blocks.size(); ++i) {
    const bool is_last = (i + 1 == blocks.size());
    elements.push_back(
        RenderBlock(blocks[i], context,
                    is_last ? std::move(trailing_inline) : ftxui::Element{}));
    if (!is_last) {
      elements.push_back(ftxui::text(""));
    }
  }
  return ftxui::vbox(elements);
}

ftxui::Element MarkdownRenderer::RenderBlock(const BlockNode& block,
                                             const RenderContext& context,
                                             ftxui::Element trailing_inline) {
  return std::visit(
      [&context, &trailing_inline](const auto& node) -> ftxui::Element {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Heading>) {
          return RenderHeading(node, context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, Paragraph>) {
          return RenderParagraph(node, context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, CodeBlock>) {
          return RenderCodeBlock(node, context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, std::shared_ptr<Blockquote>>) {
          return RenderBlockquote(*node, context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T,
                                            std::shared_ptr<UnorderedList>>) {
          return RenderUnorderedList(*node, context,
                                     std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, std::shared_ptr<OrderedList>>) {
          return RenderOrderedList(*node, context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, HorizontalRule>) {
          return RenderHorizontalRule(context, std::move(trailing_inline));
        } else if constexpr (std::is_same_v<T, Table>) {
          return RenderTable(node, context, std::move(trailing_inline));
        } else {
          return ftxui::text("");
        }
      },
      block);
}

namespace {

std::string RepeatUtf8(std::string_view unit, int count) {
  std::string result;
  result.reserve(unit.size() * static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    result.append(unit);
  }
  return result;
}

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
    const std::vector<InlineNode>& nodes, const RenderContext& context,
    ftxui::Element trailing_inline) {
  // Split into rows on LineBreak boundaries so hard breaks visually wrap.
  std::vector<ftxui::Elements> rows;
  rows.emplace_back();
  for (const auto& node : nodes) {
    if (std::holds_alternative<LineBreak>(node)) {
      rows.emplace_back();
      continue;
    }
    auto pieces = RenderInlineWords(node, context);
    rows.back().insert(rows.back().end(), pieces.begin(), pieces.end());
  }
  if (trailing_inline) {
    rows.back().push_back(std::move(trailing_inline));
  }
  if (rows.size() == 1) {
    return ftxui::hflow(rows[0]);
  }
  ftxui::Elements row_elems;
  row_elems.reserve(rows.size());
  for (auto& row : rows) {
    row_elems.push_back(ftxui::hflow(row));
  }
  return ftxui::vbox(std::move(row_elems));
}

ftxui::Element MarkdownRenderer::RenderInlineRun(const InlineNode& node,
                                                 const RenderContext& context) {
  const auto& theme = context.Colors();
  return std::visit(
      [&theme](const auto& n) -> ftxui::Element {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, Text>) {
          return ftxui::text(n.content) | ftxui::color(theme.chrome.body_text);
        } else if constexpr (std::is_same_v<T, Bold>) {
          return ftxui::text(n.content) | ftxui::bold |
                 ftxui::color(theme.chrome.body_text);
        } else if constexpr (std::is_same_v<T, Italic>) {
          return ftxui::text(n.content) | ftxui::italic |
                 ftxui::color(theme.chrome.body_text);
        } else if constexpr (std::is_same_v<T, Strikethrough>) {
          return ftxui::text(n.content) | ftxui::strikethrough |
                 ftxui::color(theme.chrome.body_text);
        } else if constexpr (std::is_same_v<T, InlineCode>) {
          return ftxui::text(" " + n.content + " ") | ftxui::bold |
                 ftxui::bgcolor(theme.code.inline_bg) |
                 ftxui::color(theme.code.inline_fg);
        } else if constexpr (std::is_same_v<T, Link>) {
          return ftxui::hyperlink(n.url, ftxui::text(n.text) |
                                             ftxui::color(theme.markdown.link) |
                                             ftxui::underlined);
        } else if constexpr (std::is_same_v<T, Image>) {
          std::string label =
              "[" + (n.alt.empty() ? std::string("image") : n.alt) + "]";
          return ftxui::hyperlink(n.url, ftxui::text(label) |
                                             ftxui::color(theme.markdown.link) |
                                             ftxui::underlined);
        } else if constexpr (std::is_same_v<T, LineBreak>) {
          return ftxui::text(" ");
        } else {
          return ftxui::text("");
        }
      },
      node);
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
          return {ftxui::hyperlink(
              n.url, ftxui::text(n.text) | ftxui::color(theme.markdown.link) |
                         ftxui::underlined)};
        } else if constexpr (std::is_same_v<T, Image>) {
          std::string label =
              "[" + (n.alt.empty() ? std::string("image") : n.alt) + "]";
          return {ftxui::hyperlink(
              n.url, ftxui::text(label) | ftxui::color(theme.markdown.link) |
                         ftxui::underlined)};
        } else if constexpr (std::is_same_v<T, LineBreak>) {
          return {ftxui::text(" ")};
        } else {
          return {ftxui::text("")};
        }
      },
      node);
}

ftxui::Element MarkdownRenderer::RenderHeading(const Heading& h,
                                               const RenderContext& context,
                                               ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  auto inline_elem =
      RenderInline(h.children, context, std::move(trailing_inline));

  if (h.level == 1) {
    auto line = ftxui::text(RepeatUtf8("\xe2\x95\x81", 20)) |
                ftxui::color(theme.markdown.heading) | ftxui::dim;
    return ftxui::vbox({
        inline_elem | ftxui::bold | ftxui::color(theme.markdown.heading),
        line,
    });
  }

  if (h.level == 2) {
    auto line = ftxui::text(RepeatUtf8("\xe2\x94\x80", 20)) |
                ftxui::color(theme.markdown.heading) | ftxui::dim;
    return ftxui::vbox({
        inline_elem | ftxui::bold | ftxui::color(theme.markdown.heading),
        line,
    });
  }

  if (h.level <= 4) {
    return inline_elem | ftxui::bold | ftxui::color(theme.markdown.heading);
  }

  return inline_elem | ftxui::color(theme.chrome.dim_text);
}

ftxui::Element MarkdownRenderer::RenderParagraph(
    const Paragraph& p, const RenderContext& context,
    ftxui::Element trailing_inline) {
  return RenderInline(p.children, context, std::move(trailing_inline));
}

ftxui::Element MarkdownRenderer::RenderCodeBlock(
    const CodeBlock& cb, const RenderContext& context,
    ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  auto code_lines = util::SplitLines(cb.source);
  auto gutter_width = std::to_string(code_lines.size()).size() + 1;

  ftxui::Elements inner;
  if (!cb.language.empty() || cb.partial) {
    std::string label = cb.language.empty() ? std::string("...") : cb.language;
    if (cb.partial && !cb.language.empty()) {
      label += " ...";
    }
    inner.push_back(ftxui::hbox({
        ftxui::filler(),
        ftxui::text("[" + label + "]") |
            ftxui::color(theme.semantic.text_muted) | ftxui::dim,
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
    ftxui::Elements row_children = {
        ftxui::text(std::string(layout::kCardPadX, ' ')) | ftxui::bgcolor(bg),
        ftxui::text("\xe2\x94\x82") |
            ftxui::color(theme.semantic.border_subtle) | ftxui::bgcolor(bg),
        ftxui::text(padded_num) | ftxui::color(theme.chrome.dim_text) |
            ftxui::dim | ftxui::bgcolor(bg),
        line_elem | ftxui::bgcolor(bg),
        ftxui::text(std::string(layout::kCardPadX, ' ')) | ftxui::bgcolor(bg),
    };
    if (trailing_inline && i + 1 == code_lines.size()) {
      row_children.push_back(std::move(trailing_inline) | ftxui::bgcolor(bg));
    }
    inner.push_back(ftxui::hbox(std::move(row_children)));
  }

  return ftxui::vbox(inner) | ftxui::bgcolor(theme.code.bg);
}

ftxui::Element MarkdownRenderer::RenderBlockquote(
    const Blockquote& bq, const RenderContext& context,
    ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();

  ftxui::Element body;
  if (bq.children.empty()) {
    body = trailing_inline ? std::move(trailing_inline) : ftxui::text("");
  } else {
    body = Render(bq.children, context, std::move(trailing_inline));
  }
  return ftxui::hbox({
             ftxui::text("\xe2\x96\x8e ") | ftxui::color(theme.chrome.dim_text),
             body | ftxui::flex,
         }) |
         ftxui::bgcolor(theme.markdown.quote_bg);
}

ftxui::Element MarkdownRenderer::RenderUnorderedList(
    const UnorderedList& ul, const RenderContext& context,
    ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  ftxui::Elements items;
  for (size_t i = 0; i < ul.items.size(); ++i) {
    const bool is_last = (i + 1 == ul.items.size());
    const auto& item = ul.items[i];
    ftxui::Element marker_elem;
    if (item.task) {
      const char* glyph = item.checked ? "\xe2\x98\x91 "   // ☑
                                       : "\xe2\x98\x90 ";  // ☐
      marker_elem =
          ftxui::text(glyph) | ftxui::bold |
          ftxui::color(item.checked ? theme.role.agent : theme.chrome.dim_text);
    } else {
      marker_elem = ftxui::text("\xe2\x80\xa2 ") |
                    ftxui::color(theme.role.agent) | ftxui::bold;
    }
    auto body = Render(item.children, context,
                       is_last ? std::move(trailing_inline) : ftxui::Element{});
    items.push_back(ftxui::hbox({
        ftxui::text("  "),
        std::move(marker_elem),
        std::move(body) | ftxui::flex,
    }));
  }
  if (trailing_inline && ul.items.empty()) {
    items.push_back(std::move(trailing_inline));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderOrderedList(
    const OrderedList& ol, const RenderContext& context,
    ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  ftxui::Elements items;
  for (size_t i = 0; i < ol.items.size(); ++i) {
    const bool is_last = (i + 1 == ol.items.size());
    auto num = std::to_string(ol.start + static_cast<int>(i)) + ". ";
    auto body = Render(ol.items[i].children, context,
                       is_last ? std::move(trailing_inline) : ftxui::Element{});
    items.push_back(ftxui::hbox({
        ftxui::text("  "),
        ftxui::text(num) | ftxui::color(theme.chrome.dim_text) | ftxui::bold,
        std::move(body) | ftxui::flex,
    }));
  }
  if (trailing_inline && ol.items.empty()) {
    items.push_back(std::move(trailing_inline));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderHorizontalRule(
    const RenderContext& context, ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  auto hr = ftxui::text(RepeatUtf8("\xe2\x94\x80", 20)) |
            ftxui::color(theme.chrome.dim_text) | ftxui::dim;
  if (trailing_inline) {
    return ftxui::vbox({hr, std::move(trailing_inline)});
  }
  return hr;
}

ftxui::Element MarkdownRenderer::RenderTableCell(
    const std::vector<InlineNode>& cell, ColumnAlignment align,
    const RenderContext& context, bool is_header,
    ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  // Table cells layout inside a non-wrapping hbox. If we used
  // RenderInlineWords here, each word becomes its own ftxui::text, and when
  // the table shrinks a column below its natural width FTXUI distributes the
  // deficit across every child - clipping one character off every word
  // instead of only at the cell's right edge. Emit one element per styled
  // run so shrinkage clips the run as a whole.
  ftxui::Elements runs;
  for (const auto& node : cell) {
    runs.push_back(RenderInlineRun(node, context));
  }
  if (trailing_inline) {
    runs.push_back(std::move(trailing_inline));
  }
  if (runs.empty()) {
    runs.push_back(ftxui::text(""));
  }

  auto content = ftxui::hbox(runs);
  if (is_header) {
    content = content | ftxui::bold | ftxui::color(theme.markdown.heading);
  }

  switch (align) {
    case ColumnAlignment::Right:
      return ftxui::hbox(
          {ftxui::filler(), std::move(content), ftxui::text(" ")});
    case ColumnAlignment::Center:
      return ftxui::hbox(
          {ftxui::filler(), std::move(content), ftxui::filler()});
    case ColumnAlignment::Left:
    case ColumnAlignment::Default:
      return ftxui::hbox(
          {ftxui::text(" "), std::move(content), ftxui::filler()});
  }
  return content;
}

ftxui::Element MarkdownRenderer::RenderTable(const Table& t,
                                             const RenderContext& context,
                                             ftxui::Element trailing_inline) {
  const auto& theme = context.Colors();
  const size_t ncols = t.columns.size();
  if (ncols == 0) {
    return trailing_inline ? std::move(trailing_inline) : ftxui::text("");
  }

  const size_t ndata = t.rows.size();
  std::vector<std::vector<ftxui::Element>> grid;
  grid.reserve(1 + ndata);

  std::vector<ftxui::Element> header_cells;
  header_cells.reserve(ncols);
  for (size_t c = 0; c < ncols; ++c) {
    header_cells.push_back(RenderTableCell(t.header[c], t.columns[c], context,
                                           /*is_header=*/true,
                                           ftxui::Element{}));
  }
  grid.push_back(std::move(header_cells));

  for (size_t r = 0; r < ndata; ++r) {
    const bool is_last_row = (r + 1 == ndata);
    std::vector<ftxui::Element> cells;
    cells.reserve(ncols);
    for (size_t c = 0; c < ncols; ++c) {
      const bool is_trailing_cell =
          is_last_row && (c + 1 == ncols) && static_cast<bool>(trailing_inline);
      cells.push_back(RenderTableCell(
          t.rows[r][c], t.columns[c], context, /*is_header=*/false,
          is_trailing_cell ? std::move(trailing_inline) : ftxui::Element{}));
    }
    grid.push_back(std::move(cells));
  }

  ftxui::Table tab(grid);
  tab.SelectAll().Border(ftxui::LIGHT);
  tab.SelectAll().Separator(ftxui::LIGHT);
  auto table_elem = tab.Render() | ftxui::color(theme.code.border);

  if (ndata == 0 && trailing_inline) {
    return ftxui::vbox({std::move(table_elem), std::move(trailing_inline)});
  }
  return table_elem;
}

}  // namespace yac::presentation::markdown
