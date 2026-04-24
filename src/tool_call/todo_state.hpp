#pragma once

#include "core_types/tool_call_types.hpp"

#include <mutex>
#include <vector>

namespace yac::tool_call {

class TodoState {
 public:
  void Update(std::vector<TodoItem> todos);
  [[nodiscard]] std::vector<TodoItem> Current() const;
  void Clear();

 private:
  mutable std::mutex mutex_;
  std::vector<TodoItem> items_;
};

}  // namespace yac::tool_call
