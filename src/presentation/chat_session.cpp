#include "chat_session.hpp"

#include "markdown/parser.hpp"

#include <utility>

namespace yac::presentation {

MessageId ChatSession::AddMessage(Sender sender, std::string content,
                                  MessageStatus status) {
  auto id = next_id_++;
  Message message{sender, std::move(content)};
  message.id = id;
  message.status = status;
  if (sender == Sender::Agent) {
    message.render_cache.markdown_blocks =
        markdown::MarkdownParser::Parse(message.Text());
  }
  messages_.push_back(std::move(message));
  return id;
}

void ChatSession::AddToolCallMessage(tool_call::ToolCallBlock block) {
  auto id = next_id_++;
  auto message = Message::Tool(std::move(block));
  message.id = id;
  messages_.push_back(std::move(message));
  tool_expanded_states_.push_back(std::make_unique<bool>(true));
}

void ChatSession::AppendToAgentMessage(MessageId id, std::string delta) {
  if (delta.empty()) {
    return;
  }

  auto idx = FindMessageIndex(id);
  if (!idx.has_value()) {
    return;
  }

  auto& message = messages_[*idx];
  message.Text() += delta;
  message.render_cache.markdown_blocks =
      markdown::MarkdownParser::Parse(message.Text());
  message.render_cache.ResetElement();
}

void ChatSession::SetMessageStatus(MessageId id, MessageStatus status) {
  auto idx = FindMessageIndex(id);
  if (!idx.has_value()) {
    return;
  }

  auto& message = messages_[*idx];
  if (message.status == status) {
    return;
  }
  message.status = status;
  message.render_cache.ResetElement();
}

void ChatSession::SetToolExpanded(size_t index, bool expanded) {
  if (index >= tool_expanded_states_.size()) {
    return;
  }

  *tool_expanded_states_[index] = expanded;
}

void ChatSession::ClearMessages() {
  messages_.clear();
  tool_expanded_states_.clear();
}

std::optional<size_t> ChatSession::FindMessageIndex(MessageId id) const {
  for (size_t i = 0; i < messages_.size(); ++i) {
    if (messages_[i].id == id) {
      return i;
    }
  }
  return std::nullopt;
}

const std::vector<Message>& ChatSession::Messages() const {
  return messages_;
}

bool ChatSession::Empty() const {
  return messages_.empty();
}

size_t ChatSession::MessageCount() const {
  return messages_.size();
}

bool* ChatSession::ToolExpandedState(size_t index) {
  while (tool_expanded_states_.size() <= index) {
    tool_expanded_states_.push_back(std::make_unique<bool>(false));
  }
  return tool_expanded_states_[index].get();
}

}  // namespace yac::presentation
