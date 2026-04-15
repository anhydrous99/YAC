#pragma once

#include "chat_session.hpp"
#include "command_palette.hpp"
#include "composer_state.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"
#include "message.hpp"
#include "message_renderer.hpp"
#include "slash_command_menu.hpp"
#include "slash_command_registry.hpp"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yac::presentation {

class ChatUI {
 public:
  using OnSendCallback = std::function<void(const std::string&)>;
  using OnCommandCallback = std::function<void(const std::string&)>;
  using UiTask = std::function<void()>;
  using UiTaskRunner = std::function<void(UiTask)>;

  static constexpr int kMaxInputLines = 8;

  ChatUI();
  explicit ChatUI(OnSendCallback on_send);
  ~ChatUI();
  ChatUI(const ChatUI&) = delete;
  ChatUI(ChatUI&&) = delete;
  ChatUI& operator=(const ChatUI&) = delete;
  ChatUI& operator=(ChatUI&&) = delete;

  [[nodiscard]] ftxui::Component Build();

  void SetOnSend(OnSendCallback on_send);
  void SetOnCommand(OnCommandCallback on_command);
  void SetUiTaskRunner(UiTaskRunner ui_task_runner);
  MessageId AddMessage(Sender sender, std::string content,
                       MessageStatus status = MessageStatus::Complete);
  MessageId AddMessageWithId(MessageId id, Sender sender, std::string content,
                             MessageStatus status = MessageStatus::Complete);
  MessageId StartAgentMessage();
  MessageId StartAgentMessage(MessageId id);
  void AppendToAgentMessage(MessageId id, std::string delta);
  void SetMessageStatus(MessageId id, MessageStatus status);
  void AddToolCallMessage(::yac::tool_call::ToolCallBlock block);
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetSlashCommands(SlashCommandRegistry registry);
  void SetProviderModel(std::string provider_id, std::string model);
  void SetTyping(bool typing);
  void SetToolExpanded(size_t index, bool expanded);
  void ClearMessages();

  [[nodiscard]] const std::vector<Message>& GetMessages() const;
  [[nodiscard]] bool HasMessage(MessageId id) const;
  [[nodiscard]] bool IsTyping() const;
  [[nodiscard]] std::string ProviderId() const;
  [[nodiscard]] std::string Model() const;
  [[nodiscard]] int CalculateInputHeight() const;
  [[nodiscard]] bool HandleInputEvent(const ftxui::Event& event);

 private:
  void SubmitMessage();
  void InsertNewline();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Element RenderMessages() const;
  void SyncMessageComponents();
  void ScrollUp(int lines);
  void ScrollDown(int lines);
  [[nodiscard]] int PageLines() const;
  [[nodiscard]] int ViewportHeight() const;
  [[nodiscard]] int MaxScrollOffset() const;
  [[nodiscard]] bool HasActiveAgentMessage() const;
  [[nodiscard]] bool HasPendingAgentMessage() const;
  void AdvanceThinkingFrame();
  void SyncThinkingAnimation();
  void StartThinkingAnimation();
  void StopThinkingAnimation();
  void ClampScrollOffset();
  void UpdateSlashMenuState();
  [[nodiscard]] bool HandleSlashMenuEvent(const ftxui::Event& event);
  void DispatchSlashMenuSelection();

  ChatSession session_;
  mutable MessageRenderCacheStore render_cache_;
  std::vector<ftxui::Component> message_components_;
  ComposerState composer_;
  OnSendCallback on_send_;
  OnCommandCallback on_command_;
  bool is_typing_ = false;
  int palette_level_ = -1;  // -1=hidden, 0=commands, 1=models
  bool show_palette_ = false;
  bool show_model_palette_ = false;
  std::vector<Command> commands_;
  std::vector<Command> model_commands_;
  SlashCommandRegistry slash_commands_;
  std::string provider_id_;
  std::string model_;

  int scroll_offset_y_ = 0;
  int content_height_ = 0;
  ftxui::Box visible_box_{};
  ftxui::Box scrollbar_box_{};
  bool scrollbar_dragging_ = false;
  bool follow_tail_ = true;
  size_t messages_seen_count_ = 0;
  ftxui::CapturedMouse captured_mouse_;
  mutable int last_terminal_width_ = -1;
  int thinking_frame_ = 0;
  struct AnimationState;
  UiTaskRunner ui_task_runner_;
  std::shared_ptr<AnimationState> animation_state_;
  std::mutex thinking_animation_mutex_;
  std::condition_variable_any thinking_animation_wake_;
  std::jthread thinking_animation_worker_;
};

}  // namespace yac::presentation
