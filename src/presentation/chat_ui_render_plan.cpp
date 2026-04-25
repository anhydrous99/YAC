#include "chat_ui_render_plan.hpp"

namespace yac::presentation {

std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages) {
  std::vector<MessageRenderItem> plan;
  plan.reserve(messages.size());
  for (size_t i = 0; i < messages.size(); ++i) {
    plan.push_back(MessageRenderItem{
        .kind = messages[i].sender == Sender::Agent
                    ? MessageRenderItem::Kind::Agent
                    : MessageRenderItem::Kind::User,
        .message_index = i,
    });
  }
  return plan;
}

}  // namespace yac::presentation
