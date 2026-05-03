#pragma once

#include "core_types/tool_call_types.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace yac::tool_call {

namespace detail {

inline void ApplyErrorToBlock(ToolCallBlock& block,
                              const std::string& message) {
  std::visit(
      [&message](auto& call) {
        using Call = std::decay_t<decltype(call)>;
        if constexpr (std::is_same_v<Call, ToolCallError>) {
          call.is_error = true;
          call.error.message = message;
        } else if constexpr (requires { call.is_error; }) {
          call.is_error = true;
        }
        if constexpr (!std::is_same_v<Call, ToolCallError> &&
                      requires { call.error; }) {
          call.error = message;
        }
      },
      block);
}

}  // namespace detail

[[nodiscard]] inline ToolExecutionResult ErrorResult(ToolCallBlock block,
                                                     std::string message) {
  detail::ApplyErrorToBlock(block, message);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = Json{{"error", message}}.dump(),
      .is_error = true,
  };
}

[[nodiscard]] inline ToolExecutionResult ErrorResult(ToolCallBlock block,
                                                     std::string message,
                                                     Json result_json) {
  detail::ApplyErrorToBlock(block, message);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = result_json.dump(),
      .is_error = true,
  };
}

}  // namespace yac::tool_call
