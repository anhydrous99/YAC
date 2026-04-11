#include "chat_session.hpp"

#include "markdown/parser.hpp"

#include <utility>

namespace yac::presentation {

void ChatSession::AddMessage(Sender sender, std::string content) {
  Message message{sender, std::move(content)};
  if (sender == Sender::Agent) {
    message.render_cache.markdown_blocks =
        markdown::MarkdownParser::Parse(message.Text());
  }
  messages_.push_back(std::move(message));
}

void ChatSession::AddToolCallMessage(tool_call::ToolCallBlock block) {
  messages_.push_back(Message::Tool(std::move(block)));
  tool_expanded_states_.push_back(std::make_unique<bool>(true));
}

void ChatSession::SetToolExpanded(size_t index, bool expanded) {
  if (index >= tool_expanded_states_.size()) {
    return;
  }

  *tool_expanded_states_[index] = expanded;
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
