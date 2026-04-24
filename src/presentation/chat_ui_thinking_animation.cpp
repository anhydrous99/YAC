#include "chat_ui_thinking_animation.hpp"

#include <atomic>
#include <chrono>
#include <utility>

namespace yac::presentation {

namespace {

constexpr auto kThinkingFrameDuration = std::chrono::milliseconds(500);

}  // namespace

struct ChatUiThinkingAnimation::AnimationState {
  std::atomic_bool alive = true;
};

ChatUiThinkingAnimation::ChatUiThinkingAnimation()
    : animation_state_(std::make_shared<AnimationState>()) {}

ChatUiThinkingAnimation::~ChatUiThinkingAnimation() {
  if (animation_state_) {
    animation_state_->alive.store(false, std::memory_order_release);
  }
  Stop();
}

void ChatUiThinkingAnimation::SetUiTaskRunner(UiTaskRunner ui_task_runner) {
  ui_task_runner_ = std::move(ui_task_runner);
}

void ChatUiThinkingAnimation::ResetFrame() {
  frame_ = 0;
}

int ChatUiThinkingAnimation::Frame() const {
  return frame_;
}

void ChatUiThinkingAnimation::Sync(
    const std::function<bool()>& has_pending_agent_message) {
  if (has_pending_agent_message()) {
    Start(has_pending_agent_message);
    return;
  }

  Stop();
}

void ChatUiThinkingAnimation::AdvanceFrame() {
  frame_ = (frame_ + 1) % 10;
}

void ChatUiThinkingAnimation::Start(
    const std::function<bool()>& has_pending_agent_message) {
  if (worker_.joinable() || !ui_task_runner_ || !animation_state_) {
    return;
  }

  auto ui_task_runner = ui_task_runner_;
  auto animation_state = animation_state_;
  worker_ = std::jthread(
      [this, ui_task_runner = std::move(ui_task_runner),
       animation_state = std::move(animation_state),
       has_pending_agent_message](std::stop_token stop_token) mutable {
        while (!stop_token.stop_requested()) {
          std::unique_lock lock(mutex_);
          wake_.wait_for(lock, stop_token, kThinkingFrameDuration,
                         [] { return false; });
          if (stop_token.stop_requested()) {
            return;
          }
          lock.unlock();

          ui_task_runner([this, animation_state, has_pending_agent_message] {
            if (!animation_state->alive.load(std::memory_order_acquire)) {
              return;
            }
            if (!has_pending_agent_message()) {
              Sync(has_pending_agent_message);
              return;
            }
            AdvanceFrame();
          });
        }
      });
}

void ChatUiThinkingAnimation::Stop() {
  if (!worker_.joinable()) {
    return;
  }

  worker_.request_stop();
  wake_.notify_all();
  worker_ = std::jthread{};
}

}  // namespace yac::presentation
