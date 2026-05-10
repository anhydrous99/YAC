#pragma once

#include "chat_session.hpp"
#include "message.hpp"

#include <cstddef>
#include <vector>

namespace yac::presentation {

struct MessageRenderItem {
  enum class Kind { User, Agent, Notice };

  Kind kind = Kind::User;
  // For User/Agent, indexes into ChatSession::Messages().
  // For Notice, indexes into ChatSession::Notices().
  size_t message_index = 0;

  auto operator==(const MessageRenderItem&) const -> bool = default;
};

[[nodiscard]] std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages,
    const std::vector<NoticeEntry>& notices);

[[nodiscard]] std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages);

}  // namespace yac::presentation
