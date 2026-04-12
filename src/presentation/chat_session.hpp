#pragma once

#include "message.hpp"

#include <memory>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatSession {
 public:
  void AddMessage(Sender sender, std::string content);
  void AddToolCallMessage(tool_call::ToolCallBlock block);
  void AppendToLastAgentMessage(std::string delta);
  void SetToolExpanded(size_t index, bool expanded);

  [[nodiscard]] const std::vector<Message>& Messages() const;
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] size_t MessageCount() const;
  [[nodiscard]] bool* ToolExpandedState(size_t index);

 private:
  std::vector<Message> messages_;
  std::vector<std::unique_ptr<bool>> tool_expanded_states_;
};

}  // namespace yac::presentation
