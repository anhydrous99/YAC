#include "chat_ui_render_plan.hpp"

namespace yac::presentation {

std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages,
    const std::vector<NoticeEntry>& notices) {
  std::vector<MessageRenderItem> plan;
  plan.reserve(messages.size() + notices.size());

  size_t mi = 0;
  size_t ni = 0;
  while (mi < messages.size() && ni < notices.size()) {
    // Stable order: when timestamps tie, the message wins so notices
    // emitted at the same instant land just after the message they
    // followed (typical pattern: ModelChangedEvent → AppendNotice).
    if (notices[ni].created_at < messages[mi].created_at) {
      plan.push_back(MessageRenderItem{.kind = MessageRenderItem::Kind::Notice,
                                       .message_index = ni});
      ++ni;
    } else {
      plan.push_back(MessageRenderItem{
          .kind = messages[mi].sender == Sender::Agent
                      ? MessageRenderItem::Kind::Agent
                      : MessageRenderItem::Kind::User,
          .message_index = mi,
      });
      ++mi;
    }
  }
  for (; mi < messages.size(); ++mi) {
    plan.push_back(MessageRenderItem{
        .kind = messages[mi].sender == Sender::Agent
                    ? MessageRenderItem::Kind::Agent
                    : MessageRenderItem::Kind::User,
        .message_index = mi,
    });
  }
  for (; ni < notices.size(); ++ni) {
    plan.push_back(MessageRenderItem{.kind = MessageRenderItem::Kind::Notice,
                                     .message_index = ni});
  }
  return plan;
}

std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages) {
  static const std::vector<NoticeEntry> k_empty;
  return BuildMessageRenderPlan(messages, k_empty);
}

}  // namespace yac::presentation
