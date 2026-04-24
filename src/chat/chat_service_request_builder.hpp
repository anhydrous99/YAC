#pragma once

#include "chat/types.hpp"
#include "tool_call/executor.hpp"

#include <memory>
#include <vector>

namespace yac::tool_call {
class TodoState;
}  // namespace yac::tool_call

namespace yac::chat::internal {

class ChatServiceRequestBuilder {
 public:
  explicit ChatServiceRequestBuilder(ChatConfig config);

  [[nodiscard]] const ChatConfig& Config() const;
  [[nodiscard]] ChatRequest BuildRequest(
      const std::vector<ChatMessage>& history,
      const std::vector<ToolDefinition>& tools) const;

 private:
  ChatConfig config_;
};

[[nodiscard]] std::shared_ptr<::yac::tool_call::ToolExecutor>
MakeChatToolExecutor(const ChatConfig& config,
                     ::yac::tool_call::TodoState& todo_state);

}  // namespace yac::chat::internal
