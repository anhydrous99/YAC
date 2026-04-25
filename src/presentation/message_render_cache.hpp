#pragma once

#include "markdown/ast.hpp"
#include "message.hpp"
#include "util/time_util.hpp"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

struct TextSegmentCache {
  std::optional<std::vector<markdown::BlockNode>> markdown_blocks;
  std::optional<ftxui::Element> element;
  int terminal_width = -1;
};

struct MessageRenderCache {
  util::RelativeTimeCache relative_time;
  std::vector<TextSegmentCache> text_segments;
  // Aggregate-element cache for messages rendered as a single Element
  // (user messages, header, etc.).
  std::optional<ftxui::Element> element;
  int terminal_width = -1;

  void ResetElement();
  void ResetContent();
  void ResetTextSegment(size_t segment_index);
  [[nodiscard]] TextSegmentCache& EnsureSegment(size_t segment_index);
};

struct ToolRenderCache {
  std::optional<ftxui::Element> element;
  int terminal_width = -1;
};

class MessageRenderCacheStore {
 public:
  [[nodiscard]] MessageRenderCache& For(MessageId id);
  [[nodiscard]] ToolRenderCache& ForTool(MessageId tool_id);
  void ResetElement(MessageId id);
  void ResetContent(MessageId id);
  void ResetTextSegment(MessageId id, size_t segment_index);
  void ResetToolElement(MessageId tool_id);
  void Clear();

 private:
  std::unordered_map<MessageId, MessageRenderCache> caches_;
  std::unordered_map<MessageId, ToolRenderCache> tool_caches_;
};

}  // namespace yac::presentation
