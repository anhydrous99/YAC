#pragma once
#include <chrono>
#include <thread>

namespace yac::test {

// Polls pred() until it returns true or timeout elapses.
// Returns true if predicate succeeded, false on timeout.
template <typename Pred>
bool WaitUntil(
    Pred pred,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{5000},
    std::chrono::milliseconds poll = std::chrono::milliseconds{5}) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(poll);
  }
  return true;
}

}  // namespace yac::test
