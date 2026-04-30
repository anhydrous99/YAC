#include "app/streaming_coalescer.hpp"

#include <utility>
#include <variant>

#include <ftxui/component/event.hpp>

namespace yac::app {

StreamingCoalescer::StreamingCoalescer(ftxui::App& screen,
                                       ChatEventBridge& bridge)
    : screen_(screen), bridge_(bridge) {
  worker_ = std::jthread([this](std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
      std::unique_lock lock(mutex_);
      cv_.wait_for(lock, kFlushInterval, [this, &stop_token] {
        return stop_token.stop_requested() || flush_now_;
      });
      if (stop_token.stop_requested()) {
        return;
      }
      flush_now_ = false;
      FlushLocked(lock);
    }
  });
}

StreamingCoalescer::~StreamingCoalescer() {
  worker_.request_stop();
  {
    std::scoped_lock lock(mutex_);
    flush_now_ = true;
  }
  cv_.notify_all();
}

void StreamingCoalescer::Dispatch(chat::ChatEvent event) {
  if (auto* delta = std::get_if<chat::TextDeltaEvent>(&event.payload)) {
    std::scoped_lock lock(mutex_);
    auto [it, inserted] =
        pending_deltas_.try_emplace(delta->message_id, *delta);
    if (!inserted) {
      it->second.text += delta->text;
    }
    return;
  }
  {
    std::unique_lock lock(mutex_);
    FlushLocked(lock);
  }
  PostEvent(std::move(event));
}

void StreamingCoalescer::FlushLocked(std::unique_lock<std::mutex>& lock) {
  if (pending_deltas_.empty()) {
    return;
  }
  auto drained = std::move(pending_deltas_);
  pending_deltas_.clear();
  lock.unlock();
  for (auto& entry : drained) {
    PostEvent(chat::ChatEvent{std::move(entry.second)});
  }
  lock.lock();
}

void StreamingCoalescer::PostEvent(chat::ChatEvent event) {
  auto& bridge = bridge_;
  auto& screen = screen_;
  screen.Post([&bridge, &screen, event = std::move(event)] {
    bridge.HandleEvent(event);
    screen.PostEvent(ftxui::Event::Custom);
  });
}

}  // namespace yac::app
