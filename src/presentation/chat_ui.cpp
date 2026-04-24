#include "chat_ui.hpp"

#include "collapsible.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "message_renderer.hpp"
#include "theme.hpp"
#include "ui_spacing.hpp"
#include "util/scroll_math.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <variant>

namespace yac::presentation {

namespace {

constexpr size_t kToolGroupThreshold = 3;

std::string FormatTokens(int tokens) {
  if (tokens < 1000) {
    return std::to_string(tokens);
  }
  if (tokens < 10000) {
    const int whole = tokens / 1000;
    const int tenths = (tokens % 1000) / 100;
    return std::to_string(whole) + "." + std::to_string(tenths) + "k";
  }
  return std::to_string(tokens / 1000) + "k";
}

std::string FormatPercent(double percent) {
  const int whole = static_cast<int>(percent);
  const int tenths = static_cast<int>(percent * 10) % 10;
  return std::to_string(whole) + "." + std::to_string(tenths) + "%";
}

ftxui::Color PercentColor(double percent) {
  const auto& t = theme::CurrentTheme();
  if (percent <= 50.0) {
    return t.role.agent;
  }
  if (percent <= 80.0) {
    return ftxui::Color::Yellow;
  }
  return t.role.error;
}

bool IsWhitespaceOnly(const std::string& value) {
  return std::all_of(value.begin(), value.end(),
                     [](unsigned char ch) { return std::isspace(ch) != 0; });
}

std::string SeverityLabel(UiSeverity severity) {
  switch (severity) {
    case UiSeverity::Info:
      return "info";
    case UiSeverity::Warning:
      return "warning";
    case UiSeverity::Error:
      return "error";
  }
  return "info";
}

ftxui::Color SeverityColor(UiSeverity severity) {
  switch (severity) {
    case UiSeverity::Info:
      return theme::CurrentTheme().chrome.dim_text;
    case UiSeverity::Warning:
      return ftxui::Color::Yellow;
    case UiSeverity::Error:
      return theme::CurrentTheme().role.error;
  }
  return theme::CurrentTheme().chrome.dim_text;
}

std::string NoticeText(const UiNotice& notice) {
  if (notice.detail.empty()) {
    return notice.title;
  }
  return notice.title + ": " + notice.detail;
}

ftxui::Element NoticeLine(const UiNotice& notice) {
  return ftxui::paragraph("  " + SeverityLabel(notice.severity) + ": " +
                          NoticeText(notice)) |
         ftxui::color(SeverityColor(notice.severity));
}

class DynamicMessageStack : public ftxui::ComponentBase {
 public:
  explicit DynamicMessageStack(std::function<ftxui::Components()> get_children)
      : get_children_(std::move(get_children)) {}

  ftxui::Element OnRender() override {
    SyncChildren();

    ftxui::Elements elements;
    for (const auto& child : children_) {
      if (!elements.empty()) {
        elements.push_back(ftxui::text(" "));
      }
      elements.push_back(child->Render());
    }
    return ftxui::vbox(std::move(elements));
  }

  bool OnEvent(ftxui::Event event) override {
    SyncChildren();
    return ComponentBase::OnEvent(event);
  }

 private:
  void SyncChildren() {
    auto children = get_children_();
    const auto child_count = ChildCount();
    bool rebuild = children.size() < child_count;
    if (!rebuild) {
      for (size_t i = 0; i < child_count; ++i) {
        if (children[i] != children_[i]) {
          rebuild = true;
          break;
        }
      }
    }

    if (rebuild) {
      DetachAllChildren();
    }
    const auto start = rebuild ? 0 : child_count;
    for (size_t i = start; i < children.size(); ++i) {
      Add(children[i]);
    }
  }

  std::function<ftxui::Components()> get_children_;
};

class SlashMenuInputWrapper : public ftxui::ComponentBase {
 public:
  SlashMenuInputWrapper(ftxui::Component input,
                        std::function<bool(const ftxui::Event&)> pre_handler,
                        std::function<void()> post_handler)
      : pre_handler_(std::move(pre_handler)),
        post_handler_(std::move(post_handler)) {
    Add(std::move(input));
  }

  bool OnEvent(ftxui::Event event) override {
    if (pre_handler_(event)) {
      return true;
    }
    bool handled = children_.front()->OnEvent(event);
    post_handler_();
    return handled;
  }

 private:
  std::function<bool(const ftxui::Event&)> pre_handler_;
  std::function<void()> post_handler_;
};

}  // namespace

ChatUI::ChatUI()
    : input_controller_(composer_, slash_commands_),
      on_send_([](const std::string&) {}) {}

ChatUI::ChatUI(OnSendCallback on_send)
    : input_controller_(composer_, slash_commands_),
      on_send_(std::move(on_send)) {}

ChatUI::~ChatUI() = default;

void ChatUI::SetOnSend(OnSendCallback on_send) {
  on_send_ = std::move(on_send);
}

void ChatUI::SetOnCommand(OnCommandCallback on_command) {
  overlay_state_.SetOnCommand(std::move(on_command));
}

void ChatUI::SetOnToolApproval(OnToolApprovalCallback on_tool_approval) {
  overlay_state_.SetOnToolApproval(std::move(on_tool_approval));
}

void ChatUI::SetOnAskUserCallbacks(OnAskUserResponseCallback on_response,
                                   OnAskUserCancelCallback on_cancel) {
  on_ask_user_response_ = std::move(on_response);
  on_ask_user_cancel_ = std::move(on_cancel);
  overlay_state_.SetOnAskUserSubmit(on_ask_user_response_);
  overlay_state_.SetOnAskUserCancel(on_ask_user_cancel_);
}

void ChatUI::SetOnModeToggle(std::function<void()> on_mode_toggle) {
  on_mode_toggle_ = on_mode_toggle;
  input_controller_.SetOnModeToggle(std::move(on_mode_toggle));
}

void ChatUI::SetAgentMode(chat::AgentMode mode) {
  agent_mode_ = mode;
}

void ChatUI::SetUiTaskRunner(UiTaskRunner ui_task_runner) {
  thinking_animation_.SetUiTaskRunner(std::move(ui_task_runner));
  SyncThinkingAnimation();
}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  auto container = ftxui::Container::Stacked({message_list, input});

  auto main_ui = ftxui::Renderer(container, [this, message_list, input] {
    const auto& colors = render_context_.Colors();
    const int term_width = ftxui::Terminal::Size().dimx;

    // ── Status rail (single compact line) ────────────────────────
    // Left: provider/model (abbreviated when narrow)
    ftxui::Elements rail_left;
    {
      const auto& pid = overlay_state_.ProviderId();
      const auto& mdl = overlay_state_.Model();
      std::string label;
      if (term_width < 100) {
        label = mdl;
      } else {
        label = pid;
        if (!label.empty() && !mdl.empty()) {
          label += "/";
        }
        label += mdl;
      }
      if (!label.empty()) {
        rail_left.push_back(ftxui::text(" " + label + " ") |
                            ftxui::color(colors.semantic.text_weak));
      }
      {
        const bool is_plan = (agent_mode_ == chat::AgentMode::Plan);
        const std::string mode_label = is_plan ? "[PLAN]" : "[BUILD]";
        const ftxui::Color mode_color =
            is_plan ? ftxui::Color::Yellow : colors.semantic.accent_primary;
        rail_left.push_back(ftxui::text(mode_label) | ftxui::color(mode_color) |
                            ftxui::bold);
      }
    }

    // Center: live status indicators (only when active)
    ftxui::Elements rail_center;
    {
      const bool has_active_agent = HasActiveAgentMessage();
      if (is_typing_ && !has_active_agent) {
        rail_center.push_back(ftxui::text(" \xe2\x97\x8f typing") |
                              ftxui::color(colors.semantic.accent_primary) |
                              ftxui::bold);
      }
      if (const auto& last_usage = overlay_state_.LastUsage()) {
        rail_center.push_back(
            ftxui::text(
                " \xe2\x86\x91" + FormatTokens(last_usage->prompt_tokens) +
                " \xe2\x86\x93" + FormatTokens(last_usage->completion_tokens)) |
            ftxui::color(colors.semantic.text_muted));
      }
      if (overlay_state_.QueueDepth() > 0) {
        rail_center.push_back(
            ftxui::text(" queued " +
                        std::to_string(overlay_state_.QueueDepth())) |
            ftxui::color(ftxui::Color::Yellow) | ftxui::bold);
      }
      if (const auto& notice = overlay_state_.TransientStatus()) {
        rail_center.push_back(ftxui::text(" " + NoticeText(*notice)) |
                              ftxui::color(SeverityColor(notice->severity)));
      }
    }

    // Right: context %, token count, help chip
    ftxui::Elements rail_right;
    {
      const int window = overlay_state_.ContextWindowTokens();
      const int total = overlay_state_.LastUsage()
                            ? overlay_state_.LastUsage()->total_tokens
                            : 0;
      if (window > 0 && total > 0) {
        const double pct =
            (static_cast<double>(total) / static_cast<double>(window)) * 100.0;
        rail_right.push_back(ftxui::text(FormatPercent(pct) + " ") |
                             ftxui::color(PercentColor(pct)) | ftxui::bold);
        rail_right.push_back(ftxui::text(FormatTokens(total)) |
                             ftxui::color(colors.semantic.text_muted));
      }
    }
    rail_right.push_back(ftxui::text(" [? help]") |
                         ftxui::color(colors.semantic.text_muted));

    ftxui::Elements status_rail;
    for (auto& el : rail_left) {
      status_rail.push_back(std::move(el));
    }
    status_rail.push_back(ftxui::filler());
    for (auto& el : rail_center) {
      status_rail.push_back(std::move(el));
    }
    status_rail.push_back(ftxui::filler());
    for (auto& el : rail_right) {
      status_rail.push_back(std::move(el));
    }

    // ── Composer surface ─────────────────────────────────────────
    const int line_count = composer_.CalculateHeight(kMaxInputLines);
    auto composer_content = ftxui::hbox({
        ftxui::text(std::string(layout::kComposerPadX, ' ')),
        ftxui::text(" \xe2\x9d\xaf ") |
            ftxui::color(colors.semantic.accent_primary) | ftxui::bold,
        input->Render() | ftxui::flex,
        ftxui::text(" " + std::to_string(line_count) + "/" +
                    std::to_string(kMaxInputLines) + " ") |
            ftxui::color(colors.semantic.text_muted),
        ftxui::text(std::string(layout::kComposerPadX, ' ')),
    });

    ftxui::Elements composer_surface_rows;
    for (int i = 0; i < layout::kComposerPadY; ++i) {
      composer_surface_rows.push_back(ftxui::text(""));
    }
    composer_surface_rows.push_back(std::move(composer_content));
    for (int i = 0; i < layout::kComposerPadY; ++i) {
      composer_surface_rows.push_back(ftxui::text(""));
    }
    auto composer_surface = ftxui::vbox(std::move(composer_surface_rows));

    // ── Assembly ─────────────────────────────────────────────────
    ftxui::Elements main_parts;
    main_parts.push_back(message_list->Render() | ftxui::flex |
                         ftxui::bgcolor(colors.chrome.canvas_bg));

    // Separator: strong border between transcript and chrome
    main_parts.push_back(ftxui::separator() |
                         ftxui::color(colors.semantic.border_strong));

    main_parts.push_back(ftxui::hbox(std::move(status_rail)) |
                         ftxui::bgcolor(colors.semantic.surface_panel));

    if (composer_.IsSlashMenuActive() && !slash_commands_.Commands().empty()) {
      main_parts.push_back(input_controller_.RenderSlashMenu(term_width));
    }

    main_parts.push_back(
        composer_surface | ftxui::bgcolor(colors.semantic.surface_panel) |
        ftxui::size(ftxui::HEIGHT, ftxui::EQUAL,
                    kMaxInputLines + (2 * layout::kComposerPadY)));

    return ftxui::vbox(std::move(main_parts));
  });

  return overlay_state_.Wrap(main_ui);
}

MessageId ChatUI::AddMessage(Sender sender, std::string content,
                             MessageStatus status) {
  auto id = session_.AddMessage(sender, std::move(content), status);
  render_cache_.ResetContent(id);
  if (sender == Sender::Agent && status == MessageStatus::Active) {
    thinking_animation_.ResetFrame();
  }
  scroll_state_.OnMessagesChanged(sender == Sender::User);
  SyncThinkingAnimation();
  return id;
}

MessageId ChatUI::AddMessageWithId(MessageId id, Sender sender,
                                   std::string content, MessageStatus status,
                                   std::string role_label) {
  auto added_id = session_.AddMessageWithId(id, sender, std::move(content),
                                            status, std::move(role_label));
  render_cache_.ResetContent(added_id);
  if (sender == Sender::Agent && status == MessageStatus::Active) {
    thinking_animation_.ResetFrame();
  }
  scroll_state_.OnMessagesChanged(sender == Sender::User);
  SyncMessageComponents();
  SyncThinkingAnimation();
  return added_id;
}

MessageId ChatUI::StartAgentMessage() {
  auto id = session_.AddMessage(Sender::Agent, "", MessageStatus::Active);
  thinking_animation_.ResetFrame();
  render_cache_.ResetContent(id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
  SyncThinkingAnimation();
  return id;
}

MessageId ChatUI::StartAgentMessage(MessageId id) {
  return AddMessageWithId(id, Sender::Agent, "", MessageStatus::Active);
}

void ChatUI::AppendToAgentMessage(MessageId id, std::string delta) {
  if (delta.empty()) {
    return;
  }
  session_.AppendToAgentMessage(id, std::move(delta));
  render_cache_.ResetContent(id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
  SyncThinkingAnimation();
}

void ChatUI::SetMessageStatus(MessageId id, MessageStatus status) {
  if (status == MessageStatus::Active) {
    thinking_animation_.ResetFrame();
  }
  session_.SetMessageStatus(id, status);
  render_cache_.ResetElement(id);
  SyncThinkingAnimation();
}

void ChatUI::AddToolCallMessage(::yac::tool_call::ToolCallBlock block) {
  auto id = session_.AddToolCallMessage(std::move(block));
  render_cache_.ResetContent(id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
}

void ChatUI::AddToolCallMessageWithId(MessageId id,
                                      ::yac::tool_call::ToolCallBlock block,
                                      MessageStatus status) {
  session_.AddToolCallMessageWithId(id, std::move(block), status);
  render_cache_.ResetContent(id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
}

void ChatUI::UpdateToolCallMessage(MessageId id,
                                   ::yac::tool_call::ToolCallBlock block,
                                   MessageStatus status) {
  session_.SetToolCallMessage(id, std::move(block), status);
  render_cache_.ResetContent(id);
  // Do NOT rebuild message_components_ here. Collapsible now reads its
  // label and summary through providers that re-query the session each
  // frame, so the existing component tree picks up the update without
  // losing FTXUI focus on the card the user may have expanded.
}

void ChatUI::UpdateSubAgentToolCallMessage(
    MessageId parent_id, std::string tool_call_id, std::string tool_name,
    ::yac::tool_call::ToolCallBlock block, MessageStatus status) {
  const bool inserted = session_.UpsertSubAgentToolCall(
      parent_id, std::move(tool_call_id), std::move(tool_name),
      std::move(block), status);
  render_cache_.ResetContent(parent_id);
  scroll_state_.OnMessagesChanged();
  if (inserted) {
    RebuildMessageComponents();
  }
}

void ChatUI::ShowToolApproval(
    std::string approval_id, std::string tool_name, std::string prompt,
    std::optional<::yac::tool_call::ToolCallBlock> preview) {
  overlay_state_.ShowToolApproval(std::move(approval_id), std::move(tool_name),
                                  std::move(prompt), std::move(preview));
}

void ChatUI::ShowAskUserDialog(std::string approval_id, std::string question,
                               std::vector<std::string> options) {
  overlay_state_.ShowAskUserDialog(std::move(approval_id), std::move(question),
                                   std::move(options));
}

void ChatUI::SetCommands(std::vector<Command> commands) {
  overlay_state_.SetCommands(std::move(commands));
}

void ChatUI::SetModelCommands(std::vector<Command> commands) {
  overlay_state_.SetModelCommands(std::move(commands));
}

void ChatUI::SetThemeCommands(std::vector<Command> commands) {
  overlay_state_.SetThemeCommands(std::move(commands));
}

void ChatUI::SetSlashCommands(SlashCommandRegistry registry) {
  slash_commands_ = std::move(registry);
}

void ChatUI::SetProviderModel(std::string provider_id, std::string model) {
  overlay_state_.SetProviderModel(std::move(provider_id), std::move(model));
}

void ChatUI::SetLastUsage(UsageStats usage) {
  overlay_state_.SetLastUsage(usage);
}

void ChatUI::SetContextWindowTokens(int tokens) {
  overlay_state_.SetContextWindowTokens(tokens);
}

void ChatUI::SetStartupStatus(StartupStatus status) {
  overlay_state_.SetStartupStatus(std::move(status));
}

void ChatUI::SetQueueDepth(int queue_depth) {
  overlay_state_.SetQueueDepth(queue_depth);
}

void ChatUI::SetTransientStatus(UiNotice notice) {
  overlay_state_.SetTransientStatus(std::move(notice));
}

void ChatUI::SetHelpText(std::string help_text) {
  overlay_state_.SetHelpText(std::move(help_text));
}

void ChatUI::ShowHelp() {
  overlay_state_.ShowHelp();
}

void ChatUI::SetTyping(bool typing) {
  is_typing_ = typing;
}

void ChatUI::SetToolExpanded(size_t index, bool expanded) {
  session_.SetToolExpanded(index, expanded);
}

void ChatUI::ClearMessages() {
  session_.ClearMessages();
  render_cache_.Clear();
  render_plan_.clear();
  message_components_.clear();
  scroll_state_.Clear();
  SyncThinkingAnimation();
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return session_.Messages();
}

bool ChatUI::HasMessage(MessageId id) const {
  return session_.HasMessage(id);
}

bool ChatUI::IsTyping() const {
  return is_typing_;
}

std::string ChatUI::ProviderId() const {
  return overlay_state_.ProviderId();
}

std::string ChatUI::Model() const {
  return overlay_state_.Model();
}

void ChatUI::SubmitMessage() {
  if (composer_.Empty() || IsWhitespaceOnly(composer_.Content())) {
    return;
  }
  std::string sent = composer_.Submit();
  if (slash_commands_.TryDispatch(sent)) {
    return;
  }
  on_send_(sent);
}

ftxui::Component ChatUI::BuildInput() {
  ftxui::InputOption option;
  option.multiline = true;
  option.placeholder = "Type a message...";
  option.cursor_position = composer_.CursorPosition();
  option.transform = [this](ftxui::InputState state) {
    state.element |= ftxui::color(render_context_.Colors().chrome.body_text);
    if (state.is_placeholder) {
      state.element |=
          ftxui::dim | ftxui::color(render_context_.Colors().chrome.dim_text);
    }
    if (state.focused) {
      state.element |= ftxui::focusCursorBarBlinking;
    }
    return state.element;
  };

  auto input = ftxui::Input(&composer_.Content(), option);

  return ftxui::Make<SlashMenuInputWrapper>(
      input,
      [this](const ftxui::Event& event) { return HandleInputEvent(event); },
      [this] { input_controller_.UpdateSlashMenuState(); });
}

int ChatUI::CalculateInputHeight() const {
  return composer_.CalculateHeight(kMaxInputLines);
}

void ChatUI::InsertNewline() {
  composer_.InsertNewline();
}

bool ChatUI::HandleInputEvent(const ftxui::Event& event) {
  return input_controller_.HandleEvent(
      event, [this] { SubmitMessage(); }, [this] { InsertNewline(); });
}

ftxui::Component ChatUI::BuildMessageList() {
  SyncMessageComponents();
  auto message_stack = ftxui::Make<DynamicMessageStack>([this] {
    SyncMessageComponents();
    return message_components_;
  });

  auto content = ftxui::Renderer(message_stack, [this, message_stack] {
    SyncMessageComponents();
    auto msg_element =
        session_.Empty() && !is_typing_
            ? RenderEmptyState()
            : message_stack->Render() |
                  ftxui::color(render_context_.Colors().chrome.dim_text);

    msg_element->ComputeRequirement();
    scroll_state_.SetContentHeight(msg_element->requirement().min_y);
    int viewport_height = scroll_state_.ViewportHeight();
    scroll_state_.ApplyMeasuredLayout();

    ftxui::Element focused_messages;
    if (scroll_state_.FollowTail()) {
      focused_messages = msg_element | ftxui::focusPositionRelative(0.0F, 1.0F);
    } else {
      int focus_y = util::CalculateFrameFocusY(scroll_state_.ScrollOffsetY(),
                                               viewport_height);
      focused_messages = msg_element | ftxui::focusPosition(0, focus_y);
    }
    auto messages = focused_messages | ftxui::frame |
                    ftxui::reflect(scroll_state_.VisibleBox()) | ftxui::flex;

    auto scrollbar = ftxui::emptyElement();
    if (util::ShouldShowScrollbar(scroll_state_.ContentHeight(),
                                  viewport_height)) {
      int track_height = viewport_height;
      int thumb_size = util::CalculateThumbSize(scroll_state_.ContentHeight(),
                                                viewport_height, track_height);
      int thumb_pos = util::CalculateThumbPosition(
          scroll_state_.ScrollOffsetY(), scroll_state_.ContentHeight(),
          viewport_height, track_height, thumb_size);
      ftxui::Elements track_rows;
      for (int i = 0; i < track_height; ++i) {
        if (i >= thumb_pos && i < thumb_pos + thumb_size) {
          track_rows.push_back(
              ftxui::text(" ") |
              ftxui::bgcolor(render_context_.Colors().chrome.dim_text));
        } else {
          track_rows.push_back(
              ftxui::text(" ") |
              ftxui::bgcolor(render_context_.Colors().cards.agent_bg));
        }
      }
      scrollbar = ftxui::vbox(std::move(track_rows)) |
                  ftxui::reflect(scroll_state_.ScrollbarBox());
    }

    auto message_area = ftxui::hbox({
        messages | ftxui::flex,
        scrollbar,
    });

    if (!scroll_state_.FollowTail() && session_.MessageCount() > 0) {
      auto new_count = scroll_state_.NewMessageCount(session_.MessageCount());
      std::string label =
          new_count > 0 ? " \xe2\x86\x93 " + std::to_string(new_count) + " new "
                        : " \xe2\x86\x93 ";
      auto fab = ftxui::text(label) |
                 ftxui::color(render_context_.Colors().chrome.dim_text) |
                 ftxui::bgcolor(render_context_.Colors().dialog.input_bg) |
                 ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 12);
      auto fab_spacer = ftxui::vbox({
          ftxui::filler(),
          fab | ftxui::align_right,
      });
      message_area = ftxui::dbox({message_area, fab_spacer | ftxui::flex});
    }

    return message_area;
  });

  return ftxui::CatchEvent(content, [this](ftxui::Event event) {
    return scroll_state_.HandleEvent(event, session_.MessageCount());
  });
}

ftxui::Element ChatUI::BuildToolPeek(
    const ::yac::tool_call::ToolCallBlock* block, MessageStatus status) const {
  if (block == nullptr || status != MessageStatus::Active) {
    return {};
  }

  if (const auto* write = std::get_if<::yac::tool_call::FileWriteCall>(block)) {
    return tool_call::ToolCallRenderer::BuildWritePeek(
        *write, RenderContext{.terminal_width = last_terminal_width_,
                              .thinking_frame = thinking_animation_.Frame()});
  }

  return {};
}

ftxui::Component ChatUI::BuildSubAgentToolCollapsible(MessageId parent_id,
                                                      size_t child_index) {
  bool* expanded = session_.SubAgentToolExpandedState(parent_id, child_index);
  if (expanded == nullptr) {
    return ftxui::Renderer([] { return ftxui::text("Tool call unavailable"); });
  }

  auto content = ftxui::Renderer([this, parent_id, child_index] {
    const auto* child = session_.SubAgentToolCall(parent_id, child_index);
    if (child == nullptr) {
      return ftxui::text("Tool call unavailable");
    }
    return tool_call::ToolCallRenderer::Render(
        child->block,
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  });

  auto label_provider = [this, parent_id, child_index]() -> std::string {
    const auto* child = session_.SubAgentToolCall(parent_id, child_index);
    if (child == nullptr) {
      return {};
    }
    return tool_call::ToolCallRenderer::BuildLabel(child->block);
  };
  auto summary_provider = [this, parent_id, child_index]() -> std::string {
    const auto* child = session_.SubAgentToolCall(parent_id, child_index);
    if (child == nullptr) {
      return {};
    }
    if (child->status == MessageStatus::Active &&
        std::get_if<::yac::tool_call::FileWriteCall>(&child->block) !=
            nullptr) {
      return "writing\xe2\x80\xa6";
    }
    return tool_call::ToolCallRenderer::BuildSummary(child->block);
  };

  const auto* child = session_.SubAgentToolCall(parent_id, child_index);
  ftxui::Element peek = child == nullptr
                            ? ftxui::Element{}
                            : BuildToolPeek(&child->block, child->status);
  return Collapsible(std::move(label_provider), std::move(content), expanded,
                     std::move(summary_provider), std::move(peek));
}

ftxui::Component ChatUI::BuildToolContentComponent(size_t message_index) {
  const auto& messages = session_.Messages();
  if (message_index >= messages.size()) {
    return ftxui::Renderer([] { return ftxui::text("Tool call unavailable"); });
  }

  const MessageId parent_id = messages[message_index].id;
  const bool is_sub_agent = messages[message_index].ToolCall() != nullptr &&
                            std::get_if<::yac::tool_call::SubAgentCall>(
                                messages[message_index].ToolCall()) != nullptr;

  auto tool_renderer = ftxui::Renderer([this, message_index] {
    const auto& msgs = session_.Messages();
    if (message_index >= msgs.size()) {
      return ftxui::text("Tool call unavailable");
    }
    const auto* tool_call = msgs[message_index].ToolCall();
    if (tool_call == nullptr) {
      return ftxui::text("Tool call unavailable");
    }
    return tool_call::ToolCallRenderer::Render(
        *tool_call,
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  });

  if (!is_sub_agent) {
    return tool_renderer;
  }

  ftxui::Components rows{tool_renderer};
  const auto& child_tools = session_.SubAgentToolCalls(parent_id);
  if (!child_tools.empty()) {
    rows.push_back(ftxui::Renderer([] { return ftxui::text(""); }));
    rows.push_back(ftxui::Renderer([this, parent_id] {
      const auto count = session_.SubAgentToolCalls(parent_id).size();
      return ftxui::text("Sub-agent tool calls (" + std::to_string(count) +
                         ")") |
             ftxui::color(render_context_.Colors().chrome.dim_text) |
             ftxui::dim;
    }));
    for (size_t child_index = 0; child_index < child_tools.size();
         ++child_index) {
      rows.push_back(BuildSubAgentToolCollapsible(parent_id, child_index));
    }
  }

  return ftxui::Container::Vertical(std::move(rows));
}

ftxui::Component ChatUI::BuildToolCollapsible(size_t message_index,
                                              size_t tool_state_index) {
  auto content = BuildToolContentComponent(message_index);
  auto label_provider = [this, message_index]() -> std::string {
    const auto& msgs = session_.Messages();
    if (message_index >= msgs.size()) {
      return {};
    }
    return msgs[message_index].DisplayLabel();
  };
  auto summary_provider = [this, message_index]() -> std::string {
    const auto& msgs = session_.Messages();
    if (message_index >= msgs.size()) {
      return {};
    }
    const auto* block_ptr = msgs[message_index].ToolCall();
    if (block_ptr == nullptr) {
      return {};
    }
    if (msgs[message_index].status == MessageStatus::Active &&
        std::get_if<::yac::tool_call::FileWriteCall>(block_ptr) != nullptr) {
      return "writing\xe2\x80\xa6";
    }
    return ::yac::presentation::tool_call::ToolCallRenderer::BuildSummary(
        *block_ptr);
  };

  const auto& messages = session_.Messages();
  const auto* block = message_index < messages.size()
                          ? messages[message_index].ToolCall()
                          : nullptr;
  const auto status = message_index < messages.size()
                          ? messages[message_index].status
                          : MessageStatus::Complete;
  return Collapsible(std::move(label_provider), std::move(content),
                     session_.ToolExpandedState(tool_state_index),
                     std::move(summary_provider), BuildToolPeek(block, status));
}

ftxui::Component ChatUI::BuildStandaloneMessageComponent(size_t message_index) {
  return ftxui::Renderer([this, message_index] {
    const auto& message = session_.Messages()[message_index];
    return MessageRenderer::Render(
        message, render_cache_.For(message.id),
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  });
}

ftxui::Component ChatUI::BuildAgentGroupComponent(
    const MessageRenderItem& item) {
  const size_t agent_index = item.message_index;
  auto agent_renderer = ftxui::Renderer([this, agent_index] {
    const auto& agent_message = session_.Messages()[agent_index];
    return MessageRenderer::RenderAgentMessageContent(
        agent_message, render_cache_.For(agent_message.id),
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  });

  ftxui::Components tool_children;
  tool_children.reserve(item.tools.size());
  for (const auto& tool : item.tools) {
    tool_children.push_back(
        BuildToolCollapsible(tool.message_index, tool.tool_state_index));
  }

  ftxui::Component tools_component;
  if (tool_children.size() >= kToolGroupThreshold) {
    auto tools_stack = ftxui::Container::Vertical(tool_children);
    const auto child_count = item.tools.size();
    const auto group_ordinal = item.group_ordinal;
    const auto any_tool_active = item.any_tool_active;
    const auto tool_refs = item.tools;
    auto label_provider = [child_count]() -> std::string {
      return std::string{"Tool calls ("} + std::to_string(child_count) + ")";
    };
    auto summary_provider = [this, tool_refs]() -> std::string {
      std::vector<const ::yac::tool_call::ToolCallBlock*> blocks;
      blocks.reserve(tool_refs.size());
      const auto& messages = session_.Messages();
      for (const auto& tool : tool_refs) {
        if (tool.message_index >= messages.size()) {
          continue;
        }
        if (const auto* block = messages[tool.message_index].ToolCall()) {
          blocks.push_back(block);
        }
      }
      return ::yac::presentation::tool_call::ToolCallRenderer::
          BuildGroupSummary(blocks);
    };
    tools_component =
        Collapsible(std::move(label_provider), tools_stack,
                    session_.GroupExpandedState(group_ordinal, any_tool_active),
                    std::move(summary_provider), ftxui::Element{});
  } else {
    tools_component = ftxui::Container::Vertical(tool_children);
  }

  ftxui::Components group_children{std::move(agent_renderer),
                                   std::move(tools_component)};
  auto group_container = ftxui::Container::Vertical(group_children);
  return ftxui::Renderer(group_container, [this, group_container] {
    auto context = RenderContext{.terminal_width = last_terminal_width_,
                                 .thinking_frame = thinking_animation_.Frame()};
    const auto& theme_ref = context.Colors();
    auto inner = group_container->Render();
    auto styled_card = MessageRenderer::CardSurface(
        std::move(inner), theme_ref.cards.agent_bg, context);
    return ftxui::hbox({styled_card | ftxui::xflex_shrink, ftxui::filler()});
  });
}

ftxui::Element ChatUI::RenderMessages() const {
  if (session_.Empty() && !is_typing_) {
    return RenderEmptyState();
  }

  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        render_cache_.ResetElement(msg.id);
      }
    }
    last_terminal_width_ = current_width;
  }

  return MessageRenderer::RenderAll(
      session_.Messages(), render_cache_,
      RenderContext{.terminal_width = current_width,
                    .thinking_frame = thinking_animation_.Frame()});
}

ftxui::Element ChatUI::RenderEmptyState() const {
  const auto& colors = render_context_.Colors();
  const auto& startup = overlay_state_.Startup();
  const auto& model = overlay_state_.Model();

  ftxui::Elements rows;

  rows.push_back(ftxui::text("Ready") | ftxui::bold |
                 ftxui::color(colors.semantic.text_strong));
  if (!model.empty()) {
    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::hbox({
        ftxui::text("Model  ") | ftxui::color(colors.semantic.text_weak),
        ftxui::text(model) | ftxui::color(colors.semantic.text_strong) |
            ftxui::bold,
    }));
  }

  if (!startup.notices.empty()) {
    rows.push_back(ftxui::text(""));
    for (const auto& notice : startup.notices) {
      rows.push_back(NoticeLine(notice));
    }
  }

  rows.push_back(ftxui::text(""));
  rows.push_back(ftxui::hbox({
      ftxui::text("Type a message to start.  ") |
          ftxui::color(colors.semantic.text_weak),
      ftxui::text("[? help]") | ftxui::color(colors.semantic.text_muted),
  }));

  auto panel = ftxui::vbox(std::move(rows));
  return ftxui::center(MessageRenderer::CardSurface(
             std::move(panel), colors.cards.agent_bg,
             RenderContext{.terminal_width = last_terminal_width_})) |
         ftxui::flex;
}

void ChatUI::RebuildMessageComponents() {
  render_plan_.clear();
  message_components_.clear();
  SyncMessageComponents();
}

void ChatUI::SyncMessageComponents() {
  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        render_cache_.ResetElement(msg.id);
      }
    }
    last_terminal_width_ = current_width;
  }

  auto next_plan = BuildMessageRenderPlan(session_.Messages());
  if (next_plan == render_plan_) {
    return;
  }

  render_plan_ = std::move(next_plan);
  message_components_.clear();
  message_components_.reserve(render_plan_.size());
  for (const auto& item : render_plan_) {
    switch (item.kind) {
      case MessageRenderItem::Kind::StandaloneMessage:
        message_components_.push_back(
            BuildStandaloneMessageComponent(item.message_index));
        break;
      case MessageRenderItem::Kind::StandaloneTool:
        message_components_.push_back(
            BuildToolCollapsible(item.message_index, item.tool_state_index));
        break;
      case MessageRenderItem::Kind::AgentGroup:
        message_components_.push_back(BuildAgentGroupComponent(item));
        break;
    }
  }
}

bool ChatUI::HasActiveAgentMessage() const {
  return std::any_of(session_.Messages().begin(), session_.Messages().end(),
                     [](const Message& message) {
                       return message.sender == Sender::Agent &&
                              message.status == MessageStatus::Active;
                     });
}

void ChatUI::SyncThinkingAnimation() {
  thinking_animation_.Sync([this] { return HasActiveAgentMessage(); });
}

}  // namespace yac::presentation
