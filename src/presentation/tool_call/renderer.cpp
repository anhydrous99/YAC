#include "renderer.hpp"

#include "../ui_spacing.hpp"
#include "../util/string_util.hpp"
#include "descriptor.hpp"
#include "renderer_helpers.hpp"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

namespace tool_data = ::yac::tool_call;

ftxui::Element ToolCallRenderer::Render(const tool_data::ToolCallBlock& block) {
  return Render(block, RenderContext{});
}

ftxui::Element ToolCallRenderer::Render(const tool_data::ToolCallBlock& block,
                                        const RenderContext& context) {
  return std::visit(
      [&context](const auto& call) -> ftxui::Element {
        using T = std::decay_t<decltype(call)>;
        if constexpr (std::is_same_v<T, tool_data::BashCall>) {
          return RenderBash(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::FileEditCall>) {
          return RenderFileEdit(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::FileReadCall>) {
          return RenderFileRead(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::FileWriteCall>) {
          return RenderFileWrite(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::ListDirCall>) {
          return RenderListDir(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GrepCall>) {
          return RenderGrep(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GlobCall>) {
          return RenderGlob(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebFetchCall>) {
          return RenderWebFetch(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebSearchCall>) {
          return RenderWebSearch(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspDiagnosticsCall>) {
          return RenderLspDiagnostics(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspReferencesCall>) {
          return RenderLspReferences(call, context);
        } else if constexpr (std::is_same_v<T,
                                            tool_data::LspGotoDefinitionCall>) {
          return RenderLspGotoDefinition(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspRenameCall>) {
          return RenderLspRename(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspSymbolsCall>) {
          return RenderLspSymbols(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::SubAgentCall>) {
          return RenderSubAgent(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::TodoWriteCall>) {
          return RenderTodoWrite(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::AskUserCall>) {
          return RenderAskUser(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::McpToolCall>) {
          return RenderMcpToolCall(call, context);
        } else {
          return ftxui::text("");
        }
      },
      block);
}

std::string ToolCallRenderer::BuildSummary(
    const tool_data::ToolCallBlock& block) {
  return DescribeToolCall(block).summary;
}

std::string ToolCallRenderer::BuildGroupSummary(
    const std::vector<const tool_data::ToolCallBlock*>& blocks) {
  if (blocks.empty()) {
    return {};
  }

  struct Tally {
    std::string tag;
    int count;
  };
  std::vector<Tally> tallies;
  tallies.reserve(10);
  for (const auto* block : blocks) {
    if (block == nullptr) {
      continue;
    }
    auto tag = DescribeToolCall(*block).tag;
    auto it = std::ranges::find_if(
        tallies, [&tag](const Tally& t) { return t.tag == tag; });
    if (it == tallies.end()) {
      tallies.push_back({std::move(tag), 1});
    } else {
      it->count = it->count + 1;
    }
  }

  if (tallies.empty()) {
    return {};
  }

  std::ranges::sort(tallies, [](const Tally& a, const Tally& b) {
    if (a.count != b.count) {
      return a.count > b.count;
    }
    return a.tag < b.tag;
  });

  constexpr size_t kMaxTerms = 4;
  const bool overflow = tallies.size() > kMaxTerms;
  const size_t limit = std::min(tallies.size(), kMaxTerms);

  std::string out;
  for (size_t i = 0; i < limit; ++i) {
    if (!out.empty()) {
      out += " \xc2\xb7 ";
    }
    out += std::to_string(tallies[i].count);
    out += ' ';
    out += tallies[i].tag;
  }
  if (overflow) {
    out += " \xc2\xb7 \xe2\x80\xa6";
  }
  return out;
}

ftxui::Element ToolCallRenderer::BuildWritePeek(
    const tool_data::FileWriteCall& call, const RenderContext& context) {
  if (call.content_tail.empty()) {
    return ftxui::Element{};
  }
  const auto& theme = context.Colors();
  const auto lines = util::SplitLines(call.content_tail);
  if (lines.empty()) {
    return ftxui::Element{};
  }
  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.edit_add;

  auto make_bar_row = [&](const std::string& text) {
    return ftxui::hbox({
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::text("\xe2\x94\x83") | ftxui::color(accent) | ftxui::bold,
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::paragraph(text) | ftxui::color(theme.semantic.text_muted) |
            ftxui::dim | ftxui::flex,
        ftxui::text(std::string(layout::kCardPadX, ' ')),
    });
  };

  auto spacer_row = [&]() {
    return ftxui::hbox({
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::text("\xe2\x94\x83") | ftxui::color(accent),
        ftxui::filler(),
    });
  };

  const size_t limit = std::min(lines.size(), static_cast<size_t>(3));
  ftxui::Elements rows;
  rows.reserve(limit + 2);
  rows.push_back(spacer_row());
  for (size_t i = 0; i < limit; ++i) {
    rows.push_back(make_bar_row(lines[i]));
  }
  rows.push_back(spacer_row());
  return ftxui::vbox(std::move(rows));
}

std::string ToolCallRenderer::BuildLabel(
    const tool_data::ToolCallBlock& block) {
  return DescribeToolCall(block).label;
}

}  // namespace yac::presentation::tool_call
