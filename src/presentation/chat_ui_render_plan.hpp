#pragma once

#include "message.hpp"

#include <cstddef>
#include <vector>

namespace yac::presentation {

struct MessageRenderItem {
  enum class Kind { User, Agent };

  Kind kind = Kind::User;
  size_t message_index = 0;

  auto operator==(const MessageRenderItem&) const -> bool = default;
};

[[nodiscard]] std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages);

}  // namespace yac::presentation
