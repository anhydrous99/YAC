#include "tool_call/todo_state.hpp"

#include <mutex>
#include <utility>

namespace yac::tool_call {

void TodoState::Update(std::vector<TodoItem> todos) {
  std::lock_guard lock(mutex_);
  items_ = std::move(todos);
}

std::vector<TodoItem> TodoState::Current() const {
  std::lock_guard lock(mutex_);
  return items_;
}

void TodoState::Clear() {
  std::lock_guard lock(mutex_);
  items_.clear();
}

}  // namespace yac::tool_call
