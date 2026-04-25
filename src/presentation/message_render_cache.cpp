#include "message_render_cache.hpp"

namespace yac::presentation {

void MessageRenderCache::ResetElement() {
  element = std::nullopt;
  terminal_width = -1;
  for (auto& segment : text_segments) {
    segment.element = std::nullopt;
    segment.terminal_width = -1;
  }
}

void MessageRenderCache::ResetContent() {
  text_segments.clear();
  ResetElement();
}

void MessageRenderCache::ResetTextSegment(size_t segment_index) {
  if (segment_index >= text_segments.size()) {
    return;
  }
  auto& segment = text_segments[segment_index];
  segment.markdown_blocks = std::nullopt;
  segment.element = std::nullopt;
  segment.terminal_width = -1;
}

TextSegmentCache& MessageRenderCache::EnsureSegment(size_t segment_index) {
  if (segment_index >= text_segments.size()) {
    text_segments.resize(segment_index + 1);
  }
  return text_segments[segment_index];
}

MessageRenderCache& MessageRenderCacheStore::For(MessageId id) {
  return caches_[id];
}

ToolRenderCache& MessageRenderCacheStore::ForTool(MessageId tool_id) {
  return tool_caches_[tool_id];
}

void MessageRenderCacheStore::ResetElement(MessageId id) {
  if (auto it = caches_.find(id); it != caches_.end()) {
    it->second.ResetElement();
  }
}

void MessageRenderCacheStore::ResetContent(MessageId id) {
  if (auto it = caches_.find(id); it != caches_.end()) {
    it->second.ResetContent();
  }
}

void MessageRenderCacheStore::ResetTextSegment(MessageId id,
                                               size_t segment_index) {
  if (auto it = caches_.find(id); it != caches_.end()) {
    it->second.ResetTextSegment(segment_index);
  }
}

void MessageRenderCacheStore::ResetToolElement(MessageId tool_id) {
  if (auto it = tool_caches_.find(tool_id); it != tool_caches_.end()) {
    it->second.element = std::nullopt;
    it->second.terminal_width = -1;
  }
}

void MessageRenderCacheStore::Clear() {
  caches_.clear();
  tool_caches_.clear();
}

}  // namespace yac::presentation
