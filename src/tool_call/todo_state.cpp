#include "tool_call/todo_state.hpp"

#include <mutex>
#include <utility>

namespace yac::tool_call {

void TodoState::Update(std::vector<TodoItem> todos) {
  std::scoped_lock lock(mutex_);
  items_ = std::move(todos);
}

std::vector<TodoItem> TodoState::Current() const {
  std::scoped_lock lock(mutex_);
  return items_;
}

void TodoState::Clear() {
  std::scoped_lock lock(mutex_);
  items_.clear();
}

}  // namespace yac::tool_call
