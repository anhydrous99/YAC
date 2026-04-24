#include "chat_ui_clock_ticker.hpp"

#include <chrono>
#include <utility>

namespace yac::presentation {

namespace {

constexpr auto kTickInterval = std::chrono::seconds(30);

}  // namespace

ChatUiClockTicker::~ChatUiClockTicker() { Stop(); }

void ChatUiClockTicker::Start(UiTaskRunner ui_task_runner) {
  if (worker_.joinable()) {
    return;
  }
  worker_ = std::jthread(
      [ui_task_runner = std::move(ui_task_runner),
       this](std::stop_token stop_token) mutable {
        while (!stop_token.stop_requested()) {
          std::unique_lock lock(mutex_);
          wake_.wait_for(lock, stop_token, kTickInterval, [] { return false; });
          if (stop_token.stop_requested()) {
            return;
          }
          lock.unlock();
          ui_task_runner([] {});
        }
      });
}

void ChatUiClockTicker::Stop() {
  if (!worker_.joinable()) {
    return;
  }
  worker_.request_stop();
  wake_.notify_all();
  worker_ = std::jthread{};
}

}  // namespace yac::presentation
