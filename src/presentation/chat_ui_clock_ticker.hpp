#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace yac::presentation {

// Fires a no-op UI task every kTickInterval so the FTXUI event loop re-renders
// and relative timestamps (e.g. "just now" → "1m ago") stay current.
class ChatUiClockTicker {
 public:
  using UiTask = std::function<void()>;
  using UiTaskRunner = std::function<void(UiTask)>;

  ChatUiClockTicker() = default;
  ~ChatUiClockTicker();
  ChatUiClockTicker(const ChatUiClockTicker&) = delete;
  ChatUiClockTicker(ChatUiClockTicker&&) = delete;
  ChatUiClockTicker& operator=(const ChatUiClockTicker&) = delete;
  ChatUiClockTicker& operator=(ChatUiClockTicker&&) = delete;

  void Start(UiTaskRunner ui_task_runner);
  void Stop();

 private:
  std::mutex mutex_;
  std::condition_variable_any wake_;
  std::jthread worker_;
};

}  // namespace yac::presentation
