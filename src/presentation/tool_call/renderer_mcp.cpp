#include "../markdown/parser.hpp"
#include "../markdown/renderer.hpp"
#include "renderer.hpp"
#include "renderer_helpers.hpp"

#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {
namespace {

ftxui::Element RenderMcpTextBlock(const tool_data::McpResultBlock& block,
                                  const theme::Theme& theme,
                                  const RenderContext& context) {
  auto ast = markdown::MarkdownParser::Parse(block.text);
  auto rendered = markdown::MarkdownRenderer::Render(ast, context);
  return rendered | ftxui::color(theme.semantic.text_body);
}

ftxui::Element RenderMcpImageBlock(const tool_data::McpResultBlock& block,
                                   const theme::Theme& theme) {
  auto label = "[image: " + block.mime_type + ", " +
               std::to_string(block.bytes) + " bytes]";
  return ftxui::text(label) | ftxui::color(theme.semantic.text_muted);
}

ftxui::Element RenderMcpAudioBlock(const tool_data::McpResultBlock& block,
                                   const theme::Theme& theme) {
  auto label = "[audio: " + block.mime_type + ", " +
               std::to_string(block.bytes) + " bytes]";
  return ftxui::text(label) | ftxui::color(theme.semantic.text_muted);
}

ftxui::Element RenderMcpResourceLinkBlock(
    const tool_data::McpResultBlock& block, const theme::Theme& theme) {
  auto label = block.name.empty() ? block.uri : block.name + " " + block.uri;
  return ftxui::text(label) | ftxui::color(theme.semantic.text_body);
}

ftxui::Element RenderMcpEmbeddedResourceBlock(
    const tool_data::McpResultBlock& block, const theme::Theme& theme,
    const RenderContext& context) {
  if (block.mime_type.starts_with("text/") && !block.text.empty()) {
    auto ast = markdown::MarkdownParser::Parse(block.text);
    auto rendered = markdown::MarkdownRenderer::Render(ast, context);
    return rendered | ftxui::color(theme.semantic.text_body);
  }
  auto label =
      "[" + block.mime_type + ", " + std::to_string(block.bytes) + " bytes]";
  return ftxui::text(label) | ftxui::color(theme.semantic.text_muted);
}

}  // namespace

ftxui::Element ToolCallRenderer::RenderMcpToolCall(
    const tool_data::McpToolCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;

  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  }

  if (call.is_truncated) {
    content.push_back(
        RenderWrappedLine("(truncated)", theme.semantic.text_muted));
  }

  for (const auto& block : call.result_blocks) {
    switch (block.kind) {
      case tool_data::McpResultBlockKind::Text:
        content.push_back(RenderMcpTextBlock(block, theme, context));
        break;
      case tool_data::McpResultBlockKind::Image:
        content.push_back(RenderMcpImageBlock(block, theme));
        break;
      case tool_data::McpResultBlockKind::Audio:
        content.push_back(RenderMcpAudioBlock(block, theme));
        break;
      case tool_data::McpResultBlockKind::ResourceLink:
        content.push_back(RenderMcpResourceLinkBlock(block, theme));
        break;
      case tool_data::McpResultBlockKind::EmbeddedResource:
        content.push_back(
            RenderMcpEmbeddedResourceBlock(block, theme, context));
        break;
    }
  }

  auto label = std::string("[MCP: ") + call.server_id + "] " + call.tool_name;
  return RenderContainer("M", label, theme.semantic.accent_primary,
                         std::move(content), theme);
}

}  // namespace yac::presentation::tool_call
