#pragma once

#include "chat_event_sink.hpp"
#include "chat_session.hpp"
#include "chat_ui_input_controller.hpp"
#include "chat_ui_overlay_state.hpp"
#include "chat_ui_scroll_state.hpp"
#include "chat_ui_thinking_animation.hpp"
#include "command_palette.hpp"
#include "composer_state.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "message.hpp"
#include "message_renderer.hpp"
#include "slash_command_registry.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatUI : public ChatEventSink {
 public:
  using OnSendCallback = std::function<void(const std::string&)>;
  using OnCommandCallback = std::function<void(const std::string&)>;
  using OnToolApprovalCallback = std::function<void(const std::string&, bool)>;
  using UiTask = std::function<void()>;
  using UiTaskRunner = std::function<void(UiTask)>;

  static constexpr int kMaxInputLines = 3;

  ChatUI();
  explicit ChatUI(OnSendCallback on_send);
  ~ChatUI() override;
  ChatUI(const ChatUI&) = delete;
  ChatUI(ChatUI&&) = delete;
  ChatUI& operator=(const ChatUI&) = delete;
  ChatUI& operator=(ChatUI&&) = delete;

  [[nodiscard]] ftxui::Component Build();

  void SetOnSend(OnSendCallback on_send);
  void SetOnCommand(OnCommandCallback on_command);
  void SetOnToolApproval(OnToolApprovalCallback on_tool_approval);
  void SetUiTaskRunner(UiTaskRunner ui_task_runner);
  MessageId AddMessage(Sender sender, std::string content,
                       MessageStatus status = MessageStatus::Complete);
  MessageId AddMessageWithId(MessageId id, Sender sender, std::string content,
                             MessageStatus status = MessageStatus::Complete,
                             std::string role_label = "") override;
  MessageId StartAgentMessage();
  MessageId StartAgentMessage(MessageId id) override;
  void AppendToAgentMessage(MessageId id, std::string delta) override;
  void SetMessageStatus(MessageId id, MessageStatus status) override;
  void AddToolCallMessage(::yac::tool_call::ToolCallBlock block);
  void AddToolCallMessageWithId(MessageId id,
                                ::yac::tool_call::ToolCallBlock block,
                                MessageStatus status) override;
  void UpdateToolCallMessage(MessageId id,
                             ::yac::tool_call::ToolCallBlock block,
                             MessageStatus status) override;
  void UpdateSubAgentToolCallMessage(MessageId parent_id,
                                     std::string tool_call_id,
                                     std::string tool_name,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) override;
  void ShowToolApproval(std::string approval_id, std::string tool_name,
                        std::string prompt,
                        std::optional<::yac::tool_call::ToolCallBlock> preview =
                            std::nullopt) override;
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetSlashCommands(SlashCommandRegistry registry);
  void SetProviderModel(std::string provider_id, std::string model) override;
  void SetLastUsage(UsageStats usage) override;
  void SetContextWindowTokens(int tokens) override;
  void SetStartupStatus(StartupStatus status);
  void SetQueueDepth(int queue_depth) override;
  void SetTransientStatus(UiNotice notice) override;
  void SetHelpText(std::string help_text);
  void ShowHelp();
  void SetTyping(bool typing) override;
  void SetToolExpanded(size_t index, bool expanded);
  void ClearMessages() override;

  [[nodiscard]] const std::vector<Message>& GetMessages() const;
  [[nodiscard]] bool HasMessage(MessageId id) const override;
  [[nodiscard]] bool IsTyping() const;
  [[nodiscard]] std::string ProviderId() const;
  [[nodiscard]] std::string Model() const override;
  [[nodiscard]] int CalculateInputHeight() const;
  [[nodiscard]] bool HandleInputEvent(const ftxui::Event& event);

 private:
  void SubmitMessage();
  void InsertNewline();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Component BuildToolContentComponent(
      size_t message_index);
  [[nodiscard]] ftxui::Component BuildToolCollapsible(size_t message_index,
                                                      size_t tool_state_index);
  [[nodiscard]] ftxui::Component BuildSubAgentToolCollapsible(
      MessageId parent_id, size_t child_index);
  [[nodiscard]] ftxui::Element BuildToolPeek(
      const ::yac::tool_call::ToolCallBlock* block, MessageStatus status) const;
  [[nodiscard]] ftxui::Element RenderMessages() const;
  [[nodiscard]] ftxui::Element RenderEmptyState() const;
  void RebuildMessageComponents();
  void SyncMessageComponents();
  [[nodiscard]] bool HasActiveAgentMessage() const;
  void SyncThinkingAnimation();

  ChatSession session_;
  mutable MessageRenderCacheStore render_cache_;
  std::vector<ftxui::Component> message_components_;
  size_t messages_synced_ = 0;  // Tracks how many messages have been processed
                                // into components (may differ from
                                // message_components_.size() due to grouping).
  ComposerState composer_;
  ChatUiInputController input_controller_;
  ChatUiOverlayState overlay_state_;
  ChatUiScrollState scroll_state_;
  ChatUiThinkingAnimation thinking_animation_;
  OnSendCallback on_send_;
  bool is_typing_ = false;
  SlashCommandRegistry slash_commands_;
  mutable int last_terminal_width_ = -1;
};

}  // namespace yac::presentation
