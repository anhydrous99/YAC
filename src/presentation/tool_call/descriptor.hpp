#pragma once

#include "core_types/tool_call_types.hpp"

#include <string>

namespace yac::presentation::tool_call {

namespace tool_data = ::yac::tool_call;

struct ToolCallDescriptor {
  std::string tag;
  std::string label;
  std::string summary;
};

[[nodiscard]] ToolCallDescriptor DescribeToolCall(
    const tool_data::ToolCallBlock& block);

}  // namespace yac::presentation::tool_call
