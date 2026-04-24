#include "message_render_cache.hpp"

namespace yac::presentation {

void MessageRenderCache::ResetElement() {
  element = std::nullopt;
  terminal_width = -1;
  tool_element = std::nullopt;
  tool_terminal_width = -1;
}

void MessageRenderCache::ResetContent() {
  markdown_blocks = std::nullopt;
  ResetElement();
}

MessageRenderCache& MessageRenderCacheStore::For(MessageId id) {
  return caches_[id];
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

void MessageRenderCacheStore::Clear() {
  caches_.clear();
}

}  // namespace yac::presentation
