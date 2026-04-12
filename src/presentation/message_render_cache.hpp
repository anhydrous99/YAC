#pragma once

#include "markdown/ast.hpp"
#include "message.hpp"
#include "util/time_util.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

struct MessageRenderCache {
  std::optional<std::vector<markdown::BlockNode>> markdown_blocks;
  util::RelativeTimeCache relative_time;
  std::optional<ftxui::Element> element;
  int terminal_width = -1;

  void ResetElement();
  void ResetContent();
};

class MessageRenderCacheStore {
 public:
  [[nodiscard]] MessageRenderCache& For(MessageId id);
  void ResetElement(MessageId id);
  void ResetContent(MessageId id);
  void Clear();

 private:
  std::unordered_map<MessageId, MessageRenderCache> caches_;
};

}  // namespace yac::presentation
