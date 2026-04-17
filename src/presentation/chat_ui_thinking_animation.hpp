#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace yac::presentation {

class ChatUiThinkingAnimation {
 public:
  using UiTask = std::function<void()>;
  using UiTaskRunner = std::function<void(UiTask)>;

  ChatUiThinkingAnimation();
  ~ChatUiThinkingAnimation();
  ChatUiThinkingAnimation(const ChatUiThinkingAnimation&) = delete;
  ChatUiThinkingAnimation(ChatUiThinkingAnimation&&) = delete;
  ChatUiThinkingAnimation& operator=(const ChatUiThinkingAnimation&) = delete;
  ChatUiThinkingAnimation& operator=(ChatUiThinkingAnimation&&) = delete;

  void SetUiTaskRunner(UiTaskRunner ui_task_runner);
  void ResetFrame();
  [[nodiscard]] int Frame() const;
  void Sync(const std::function<bool()>& has_pending_agent_message);

 private:
  struct AnimationState;

  void AdvanceFrame();
  void Start(const std::function<bool()>& has_pending_agent_message);
  void Stop();

  int frame_ = 0;
  UiTaskRunner ui_task_runner_;
  std::shared_ptr<AnimationState> animation_state_;
  std::mutex mutex_;
  std::condition_variable_any wake_;
  std::jthread worker_;
};

}  // namespace yac::presentation
