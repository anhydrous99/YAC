#pragma once

#include "markdown/ast.hpp"
#include "tool_call/types.hpp"
#include "util/time_util.hpp"

#include <chrono>
#include <optional>
#include <string>
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

}  // namespace yac::presentation
