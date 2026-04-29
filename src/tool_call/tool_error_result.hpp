#pragma once

#include "core_types/tool_call_types.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"

#include <string>
#include <utility>
#include <variant>

namespace yac::tool_call {

[[nodiscard]] inline ToolExecutionResult ErrorResult(ToolCallBlock block,
                                                     std::string message) {
  std::visit(
      [&message](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = message;
        }
      },
      block);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = Json{{"error", message}}.dump(),
      .is_error = true,
  };
}

}  // namespace yac::tool_call
