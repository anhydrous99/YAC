#pragma once

#include "app/chat_event_bridge.hpp"
#include "chat/types.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <ftxui/component/app.hpp>

namespace yac::app {

class StreamingCoalescer {
 public:
  StreamingCoalescer(ftxui::App& screen, ChatEventBridge& bridge);
  ~StreamingCoalescer();

  StreamingCoalescer(const StreamingCoalescer&) = delete;
  StreamingCoalescer& operator=(const StreamingCoalescer&) = delete;
  StreamingCoalescer(StreamingCoalescer&&) = delete;
  StreamingCoalescer& operator=(StreamingCoalescer&&) = delete;

  void Dispatch(chat::ChatEvent event);

 private:
  static constexpr auto kFlushInterval = std::chrono::milliseconds(33);

  void FlushLocked(std::unique_lock<std::mutex>& lock);
  void PostEvent(chat::ChatEvent event);

  ftxui::App& screen_;
  ChatEventBridge& bridge_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::unordered_map<chat::ChatMessageId, chat::TextDeltaEvent> pending_deltas_;
  bool flush_now_ = false;
  std::jthread worker_;
};

}  // namespace yac::app
