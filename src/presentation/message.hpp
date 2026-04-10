#pragma once

#include "markdown/ast.hpp"
#include "tool_call/types.hpp"
#include "util/time_util.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

enum class Sender { User, Agent, Tool };

struct Message {
  Sender sender = Sender::User;
  std::string content;
  std::string role_label;
  std::string timestamp;
  std::optional<std::vector<markdown::BlockNode>> cached_blocks;
  std::optional<tool_call::ToolCallBlock> tool_call;
  mutable util::RelativeTimeCache cached_relative_time;
  mutable std::optional<ftxui::Element> cached_element;
  mutable int cached_terminal_width = -1;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();

  [[nodiscard]] std::string DisplayLabel() const;
};

template <typename UserFn, typename AgentFn, typename ToolFn,
          typename DefaultFn>
decltype(auto) SenderSwitch(Sender sender, UserFn&& when_user,
                            AgentFn&& when_agent, ToolFn&& when_tool,
                            DefaultFn&& default_value) {
  switch (sender) {
    case Sender::User:
      return std::forward<UserFn>(when_user)();
    case Sender::Agent:
      return std::forward<AgentFn>(when_agent)();
    case Sender::Tool:
      return std::forward<ToolFn>(when_tool)();
    default:
      return std::forward<DefaultFn>(default_value)();
  }
}

template <typename UserFn, typename AgentFn, typename ToolFn>
decltype(auto) SenderSwitch(Sender sender, UserFn&& when_user,
                            AgentFn&& when_agent, ToolFn&& when_tool) {
  switch (sender) {
    case Sender::User:
      return std::forward<UserFn>(when_user)();
    case Sender::Agent:
      return std::forward<AgentFn>(when_agent)();
    case Sender::Tool:
    default:
      return std::forward<ToolFn>(when_tool)();
  }
}

}  // namespace yac::presentation
