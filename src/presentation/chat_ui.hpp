#pragma once

#include "chat_event_sink.hpp"
#include "chat_session.hpp"
#include "chat_ui_actions.hpp"
#include "chat_ui_clock_ticker.hpp"
#include "chat_ui_input_controller.hpp"
#include "chat_ui_overlay_state.hpp"
#include "chat_ui_render_plan.hpp"
#include "chat_ui_scroll_state.hpp"
#include "chat_ui_thinking_animation.hpp"
#include "command_palette.hpp"
#include "composer_state.hpp"
#include "core_types/agent_mode.hpp"
#include "core_types/typed_ids.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "mcp/mcp_status_panel.hpp"
#include "message.hpp"
#include "message_render_cache.hpp"
#include "message_renderer.hpp"
#include "render_context.hpp"
#include "slash_command_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatUI : public ChatEventSink {
 public:
  using UiTask = std::function<void()>;
  using UiTaskRunner = std::function<void(UiTask)>;

  static constexpr int kMaxInputLines = 3;

  ChatUI();
  explicit ChatUI(IChatActions& actions);
  ~ChatUI() override;
  ChatUI(const ChatUI&) = delete;
  ChatUI(ChatUI&&) = delete;
  ChatUI& operator=(const ChatUI&) = delete;
  ChatUI& operator=(ChatUI&&) = delete;

  [[nodiscard]] ftxui::Component Build();

  void SetAgentMode(chat::AgentMode mode);
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
  MessageId AddToolCallMessage(::yac::tool_call::ToolCallBlock block);
  void AddToolCallSegment(MessageId tool_id,
                          ::yac::tool_call::ToolCallBlock block,
                          MessageStatus status) override;
  void UpdateToolCallMessage(MessageId tool_id,
                             ::yac::tool_call::ToolCallBlock block,
                             MessageStatus status) override;
  void UpdateSubAgentToolCallMessage(MessageId parent_id,
                                     ::yac::ToolCallId tool_call_id,
                                     std::string tool_name,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) override;
  void ShowToolApproval(::yac::ApprovalId approval_id, std::string tool_name,
                        std::string prompt,
                        std::optional<::yac::tool_call::ToolCallBlock> preview =
                            std::nullopt) override;
  void ShowAskUserDialog(::yac::ApprovalId approval_id, std::string question,
                         std::vector<std::string> options) override;
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetThemeCommands(std::vector<Command> commands);
  void SetSlashCommands(SlashCommandRegistry registry);
  void SetFileMentionProvider(
      ChatUiInputController::FileMentionProvider provider);
  void SetProviderModel(::yac::ProviderId provider_id,
                        ::yac::ModelId model) override;
  void SetLastUsage(UsageStats usage) override;
  void SetContextWindowTokens(int tokens) override;
  void SetStartupStatus(StartupStatus status);
  void SetQueueDepth(int queue_depth) override;
  MessageId AppendNotice(UiNotice notice) override;
  void SetHelpText(std::string help_text);
  void ShowHelp();
  void SetTyping(bool typing) override;
  void SetToolExpanded(MessageId tool_id, bool expanded);
  void ClearMessages() override;

  McpStatusSink& McpStatus() { return mcp_status_; }

  [[nodiscard]] const std::vector<Message>& GetMessages() const;
  [[nodiscard]] const std::vector<NoticeEntry>& GetNotices() const;
  [[nodiscard]] bool HasMessage(MessageId id) const override;
  [[nodiscard]] bool HasToolSegment(MessageId tool_id) const override;
  [[nodiscard]] bool IsTyping() const;
  [[nodiscard]] std::string ProviderId() const;
  [[nodiscard]] std::string Model() const override;
  [[nodiscard]] int ContextWindowTokens() const;
  [[nodiscard]] int CalculateInputHeight() const;
  [[nodiscard]] bool HandleInputEvent(const ftxui::Event& event);

 private:
  void SubmitMessage();
  void InsertNewline();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Component BuildUserMessageComponent(
      size_t message_index);
  [[nodiscard]] ftxui::Component BuildAgentMessageComponent(
      size_t message_index);
  [[nodiscard]] ftxui::Component BuildNoticeComponent(size_t notice_index);
  [[nodiscard]] ftxui::Component BuildToolContentComponent(MessageId tool_id);
  [[nodiscard]] ftxui::Component BuildToolCollapsible(MessageId tool_id);
  [[nodiscard]] ftxui::Component BuildSubAgentToolCollapsible(
      MessageId parent_id, size_t child_index);
  [[nodiscard]] ftxui::Element BuildToolPeek(
      const ::yac::tool_call::ToolCallBlock* block, MessageStatus status) const;
  [[nodiscard]] ftxui::Element RenderEmptyState() const;
  void RebuildMessageComponents();
  void SyncMessageComponents();
  void SyncTerminalWidth();
  [[nodiscard]] bool HasActiveAgentMessage() const;
  void SyncThinkingAnimation();

  RenderContext render_context_;
  ChatSession session_;
  mutable MessageRenderCacheStore render_cache_;
  std::vector<MessageRenderItem> render_plan_;
  std::vector<ftxui::Component> message_components_;
  uint64_t last_plan_generation_ = 0;
  bool plan_valid_ = false;
  ComposerState composer_;
  ChatUiInputController input_controller_;
  ChatUiOverlayState overlay_state_;
  ChatUiScrollState scroll_state_;
  ChatUiClockTicker clock_ticker_;
  ChatUiThinkingAnimation thinking_animation_;
  NoOpChatActions default_actions_;
  IChatActions& actions_;
  chat::AgentMode agent_mode_ = chat::AgentMode::Build;
  bool is_typing_ = false;
  SlashCommandRegistry slash_commands_;
  mutable int last_terminal_width_ = -1;
  // Cache for the per-frame msg_element->ComputeRequirement() walk that
  // feeds scroll_state_.SetContentHeight(). Walking the full N-message
  // element tree each frame is the dominant idle-CPU cost; we only need
  // to re-walk when structure, content, or width changes.
  mutable uint64_t content_height_cache_plan_gen_ = 0;
  mutable uint64_t content_height_cache_content_gen_ = 0;
  mutable int content_height_cache_width_ = -1;
  mutable int content_height_cache_value_ = 0;
  mutable bool content_height_cache_valid_ = false;
  McpStatusSink mcp_status_;
};

}  // namespace yac::presentation
