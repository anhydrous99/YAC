#include "chat_ui.hpp"

#include "collapsible.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "theme.hpp"
#include "util/scroll_math.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <variant>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

namespace {

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
  if (percent < 60.0) {
    return ftxui::Color::Green;
  }
  if (percent < 85.0) {
    return ftxui::Color::Yellow;
  }
  return ftxui::Color::Red;
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
      return k_theme.chrome.dim_text;
    case UiSeverity::Warning:
      return ftxui::Color::Yellow;
    case UiSeverity::Error:
      return k_theme.role.error;
  }
  return k_theme.chrome.dim_text;
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

void ChatUI::SetUiTaskRunner(UiTaskRunner ui_task_runner) {
  thinking_animation_.SetUiTaskRunner(std::move(ui_task_runner));
  SyncThinkingAnimation();
}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  auto container = ftxui::Container::Stacked({message_list, input});

  auto main_ui = ftxui::Renderer(container, [this, message_list, input] {
    ftxui::Elements stats_left;
    const bool has_active_agent = HasActiveAgentMessage();
    if (is_typing_ && !has_active_agent) {
      stats_left.push_back(ftxui::text(" ● typing") |
                           ftxui::color(k_theme.role.agent) | ftxui::bold);
    }
    if (const auto& last_usage = overlay_state_.LastUsage()) {
      stats_left.push_back(
          ftxui::text(" ↑" + FormatTokens(last_usage->prompt_tokens) + " ↓" +
                      FormatTokens(last_usage->completion_tokens)) |
          ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);
    }
    if (overlay_state_.QueueDepth() > 0) {
      stats_left.push_back(
          ftxui::text(" queued " +
                      std::to_string(overlay_state_.QueueDepth())) |
          ftxui::color(ftxui::Color::Yellow) | ftxui::bold);
    }
    if (const auto& notice = overlay_state_.TransientStatus()) {
      stats_left.push_back(ftxui::text(" " + NoticeText(*notice)) |
                           ftxui::color(SeverityColor(notice->severity)));
    }

    ftxui::Elements stats_right;
    const int window = overlay_state_.ContextWindowTokens();
    const int total = overlay_state_.LastUsage()
                          ? overlay_state_.LastUsage()->total_tokens
                          : 0;
    if (window > 0 && total > 0) {
      const double percent =
          (static_cast<double>(total) / static_cast<double>(window)) * 100.0;
      stats_right.push_back(ftxui::text(" " + FormatTokens(total) + " / " +
                                        FormatTokens(window) + "  ") |
                            ftxui::color(k_theme.chrome.body_text));
      stats_right.push_back(ftxui::text(FormatPercent(percent)) |
                            ftxui::color(PercentColor(percent)) | ftxui::bold);
    } else {
      stats_right.push_back(ftxui::text(" — / —") |
                            ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);
    }
    if (!session_.Empty()) {
      auto count_label = "  [" + std::to_string(session_.MessageCount()) +
                         " message" + (session_.MessageCount() > 1 ? "s" : "") +
                         "]";
      stats_right.push_back(ftxui::text(count_label) |
                            ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);
    }
    if (!overlay_state_.ProviderId().empty() ||
        !overlay_state_.Model().empty()) {
      auto provider_model = overlay_state_.ProviderId();
      if (!provider_model.empty() && !overlay_state_.Model().empty()) {
        provider_model += " / ";
      }
      provider_model += overlay_state_.Model();
      stats_right.push_back(ftxui::text("  " + provider_model + " ") |
                            ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);
    }

    ftxui::Elements stats_row;
    for (auto& element : stats_left) {
      stats_row.push_back(std::move(element));
    }
    stats_row.push_back(ftxui::filler());
    for (auto& element : stats_right) {
      stats_row.push_back(std::move(element));
    }

    auto input_area = ftxui::hbox({
        ftxui::text(" > ") | ftxui::color(k_theme.chrome.prompt) | ftxui::bold,
        input->Render() | ftxui::flex,
        ftxui::text(" " +
                    std::to_string(composer_.CalculateHeight(kMaxInputLines)) +
                    "/" + std::to_string(kMaxInputLines) + " ") |
            ftxui::color(k_theme.chrome.dim_text) | ftxui::dim,
    });

    ftxui::Elements footer_rows;
    footer_rows.push_back(ftxui::hbox(std::move(stats_row)));
    footer_rows.push_back(
        ftxui::text(" Enter=Send \xe2\x94\x82 Ctrl+P=Commands \xe2\x94\x82 "
                    "\xe2\x87\xa7+Enter=Newline \xe2\x94\x82 "
                    "PgUp/PgDn \xe2\x94\x82 Home/End") |
        ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);

    auto footer_with_hints = ftxui::vbox(footer_rows);

    ftxui::Elements main_parts;
    main_parts.push_back(message_list->Render() | ftxui::flex);
    main_parts.push_back(footer_with_hints |
                         ftxui::bgcolor(k_theme.cards.agent_bg));
    if (composer_.IsSlashMenuActive() && !slash_commands_.Commands().empty()) {
      main_parts.push_back(
          input_controller_.RenderSlashMenu(ftxui::Terminal::Size().dimx));
    }
    main_parts.push_back(
        input_area | ftxui::bgcolor(k_theme.cards.user_bg) |
        ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, kMaxInputLines));

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
                                   std::string content, MessageStatus status) {
  auto added_id =
      session_.AddMessageWithId(id, sender, std::move(content), status);
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
  // Force a full component rebuild so Collapsible headers/summaries refresh
  // with the updated tool data (labels and summaries are captured by value).
  message_components_.clear();
  messages_synced_ = 0;
  SyncMessageComponents();
}

void ChatUI::ShowToolApproval(
    std::string approval_id, std::string tool_name, std::string prompt,
    std::optional<::yac::tool_call::ToolCallBlock> preview) {
  overlay_state_.ShowToolApproval(std::move(approval_id), std::move(tool_name),
                                  std::move(prompt), std::move(preview));
}

void ChatUI::SetCommands(std::vector<Command> commands) {
  overlay_state_.SetCommands(std::move(commands));
}

void ChatUI::SetModelCommands(std::vector<Command> commands) {
  overlay_state_.SetModelCommands(std::move(commands));
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
  message_components_.clear();
  messages_synced_ = 0;
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
  option.transform = [](ftxui::InputState state) {
    state.element |= ftxui::color(k_theme.chrome.body_text);
    if (state.is_placeholder) {
      state.element |= ftxui::dim | ftxui::color(k_theme.chrome.dim_text);
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
            : message_stack->Render() | ftxui::color(k_theme.chrome.dim_text);

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
          track_rows.push_back(ftxui::text(" ") |
                               ftxui::bgcolor(k_theme.chrome.dim_text));
        } else {
          track_rows.push_back(ftxui::text(" ") |
                               ftxui::bgcolor(k_theme.cards.agent_bg));
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
      auto fab = ftxui::text(label) | ftxui::color(k_theme.chrome.dim_text) |
                 ftxui::bgcolor(k_theme.dialog.input_bg) |
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
  const auto& startup = overlay_state_.Startup();
  ftxui::Elements rows;
  rows.push_back(ftxui::text("YAC setup") | ftxui::bold |
                 ftxui::color(k_theme.markdown.heading));
  rows.push_back(ftxui::text(""));
  rows.push_back(ftxui::paragraph("Provider: " + startup.provider_id + " / " +
                                  startup.model) |
                 ftxui::color(k_theme.chrome.body_text));
  if (!startup.workspace_root.empty()) {
    rows.push_back(ftxui::paragraph("Workspace: " + startup.workspace_root) |
                   ftxui::color(k_theme.chrome.dim_text));
  }
  if (!startup.api_key_env.empty()) {
    const auto key_state = startup.api_key_configured
                               ? std::string{"configured"}
                               : std::string{"missing"};
    rows.push_back(
        ftxui::paragraph("API key: " + startup.api_key_env + " " + key_state) |
        ftxui::color(startup.api_key_configured ? k_theme.role.agent
                                                : ftxui::Color::Yellow));
  }
  if (!startup.lsp_command.empty()) {
    rows.push_back(
        ftxui::paragraph("LSP: " + startup.lsp_command + " " +
                         (startup.lsp_available ? "found" : "not found")) |
        ftxui::color(startup.lsp_available ? k_theme.role.agent
                                           : ftxui::Color::Yellow));
  }

  rows.push_back(ftxui::text(""));
  if (startup.notices.empty()) {
    rows.push_back(ftxui::paragraph("Type a message below to start, or press "
                                    "Ctrl+P and choose Help.") |
                   ftxui::color(k_theme.chrome.dim_text));
  } else {
    for (const auto& notice : startup.notices) {
      rows.push_back(NoticeLine(notice));
    }
    rows.push_back(ftxui::text(""));
    rows.push_back(ftxui::paragraph("Fix setup warnings when needed, then type "
                                    "a message below.") |
                   ftxui::color(k_theme.chrome.dim_text));
  }

  auto panel = ftxui::vbox(std::move(rows));
  return ftxui::center(MessageRenderer::CardSurface(
             std::move(panel), k_theme.cards.agent_bg,
             RenderContext{.terminal_width = last_terminal_width_})) |
         ftxui::flex;
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

  const auto& messages = session_.Messages();
  while (messages_synced_ < messages.size()) {
    const auto index = messages_synced_;
    const auto& msg = messages[index];

    if (msg.sender == Sender::Tool) {
      // Count tool index for expanded state tracking.
      size_t current_tool_index = 0;
      for (size_t i = 0; i <= index; ++i) {
        if (messages[i].sender == Sender::Tool) {
          current_tool_index = current_tool_index + 1;
        }
      }

      // Check if the preceding message was an Agent — if so, we need to group
      // this tool call into the agent's card.
      bool has_preceding_agent =
          index > 0 && messages[index - 1].sender != Sender::User;
      // Walk back to find the agent that starts this group.
      size_t group_start = index;
      if (has_preceding_agent) {
        // Find the agent message that starts this group by walking back past
        // any preceding tool messages.
        group_start = index - 1;
        while (group_start > 0 &&
               messages[group_start].sender == Sender::Tool) {
          group_start--;
        }
        has_preceding_agent = messages[group_start].sender == Sender::Agent;
      }

      if (has_preceding_agent) {
        // Pop the existing group/agent component — we'll rebuild it with the
        // new tool message included.
        message_components_.pop_back();

        // Collect all message indices in this group: [agent, tool, tool, ...]
        const size_t agent_index = group_start;

        // Build interactive tool collapsible components.
        ftxui::Components group_children;

        // Agent renderer (non-interactive).
        group_children.push_back(ftxui::Renderer([this, agent_index] {
          const auto& agent_msg = session_.Messages()[agent_index];
          return MessageRenderer::RenderAgentMessageContent(
              agent_msg, render_cache_.For(agent_msg.id),
              RenderContext{.terminal_width = last_terminal_width_,
                            .thinking_frame = thinking_animation_.Frame()});
        }));

        // Tool collapsible components (interactive — handle mouse clicks).
        for (size_t t = agent_index + 1; t <= index; ++t) {
          if (messages[t].sender != Sender::Tool) {
            continue;
          }
          // Compute this tool's expanded-state index.
          size_t ti = 0;
          for (size_t i = 0; i <= t; ++i) {
            if (messages[i].sender == Sender::Tool) {
              ti++;
            }
          }
          auto t_idx = t;
          auto tool_content = ftxui::Renderer([this, t_idx] {
            const auto* tool_call = session_.Messages()[t_idx].ToolCall();
            if (tool_call == nullptr) {
              return ftxui::text("Tool call unavailable");
            }
            return tool_call::ToolCallRenderer::Render(
                *tool_call,
                RenderContext{.terminal_width = last_terminal_width_,
                              .thinking_frame = thinking_animation_.Frame()});
          });
          const auto* block = messages[t].ToolCall();
          auto summary = block != nullptr
                             ? ::yac::presentation::tool_call::
                                   ToolCallRenderer::BuildSummary(*block)
                             : std::string{};
          ftxui::Element peek;
          if (block != nullptr && messages[t].status == MessageStatus::Active) {
            if (const auto* write =
                    std::get_if<::yac::tool_call::FileWriteCall>(block)) {
              summary = "writing\xe2\x80\xa6";
              peek = tool_call::ToolCallRenderer::BuildWritePeek(
                  *write,
                  RenderContext{.terminal_width = last_terminal_width_,
                                .thinking_frame = thinking_animation_.Frame()});
            }
          }
          group_children.push_back(
              Collapsible(messages[t].DisplayLabel(), std::move(tool_content),
                          session_.ToolExpandedState(ti - 1),
                          std::move(summary), std::move(peek)));
        }

        // Wrap the vertical container in a single CardSurface with agent_bg.
        auto group_container = ftxui::Container::Vertical(group_children);
        message_components_.push_back(
            ftxui::Renderer(group_container, [this, group_container] {
              auto ctx =
                  RenderContext{.terminal_width = last_terminal_width_,
                                .thinking_frame = thinking_animation_.Frame()};
              const auto& theme_ref = ctx.Colors();
              auto inner = group_container->Render();
              auto styled_card = MessageRenderer::CardSurface(
                  std::move(inner), theme_ref.cards.agent_bg, ctx);
              return ftxui::hbox(
                  {styled_card | ftxui::xflex_shrink, ftxui::filler()});
            }));

        messages_synced_++;
        continue;
      }

      // Standalone tool message (no preceding agent) — render as before.
      auto content = ftxui::Renderer([this, index] {
        const auto* tool_call = session_.Messages()[index].ToolCall();
        if (tool_call == nullptr) {
          return ftxui::text("Tool call unavailable");
        }
        return tool_call::ToolCallRenderer::Render(
            *tool_call,
            RenderContext{.terminal_width = last_terminal_width_,
                          .thinking_frame = thinking_animation_.Frame()});
      });
      const auto* block = messages[index].ToolCall();
      auto summary =
          block != nullptr
              ? ::yac::presentation::tool_call::ToolCallRenderer::BuildSummary(
                    *block)
              : std::string{};
      ftxui::Element peek;
      if (block != nullptr && messages[index].status == MessageStatus::Active) {
        if (const auto* write =
                std::get_if<::yac::tool_call::FileWriteCall>(block)) {
          summary = "writing\xe2\x80\xa6";
          peek = tool_call::ToolCallRenderer::BuildWritePeek(
              *write,
              RenderContext{.terminal_width = last_terminal_width_,
                            .thinking_frame = thinking_animation_.Frame()});
        }
      }
      message_components_.push_back(
          Collapsible(messages[index].DisplayLabel(), std::move(content),
                      session_.ToolExpandedState(current_tool_index - 1),
                      std::move(summary), std::move(peek)));
      messages_synced_++;
      continue;
    }

    // Non-tool message (User or Agent) — render with full CardSurface.
    message_components_.push_back(ftxui::Renderer([this, index] {
      const auto& message = session_.Messages()[index];
      return MessageRenderer::Render(
          message, render_cache_.For(message.id),
          RenderContext{.terminal_width = last_terminal_width_,
                        .thinking_frame = thinking_animation_.Frame()});
    }));
    messages_synced_++;
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
