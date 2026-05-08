#include "chat_ui.hpp"

#include "chat_ui_composer_render.hpp"
#include "chat_ui_notice_format.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "ui_spacing.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yac::presentation {

namespace {

using detail::ComposerInputWrapWidth;
using detail::FormatPercent;
using detail::FormatTokens;
using detail::kComposerPrompt;
using detail::NoticeText;
using detail::PercentColor;
using detail::SeverityColor;

}  // namespace

ChatUI::ChatUI() : ChatUI(default_actions_) {}

ChatUI::ChatUI(IChatActions& actions)
    : input_controller_(composer_, slash_commands_), actions_(actions) {
  overlay_state_.SetOnCommand(
      [this](const std::string& command) { actions_.OnCommand(command); });
  overlay_state_.SetOnToolApproval(
      [this](const ::yac::ApprovalId& approval_id, bool approved) {
        actions_.OnToolApproval(approval_id, approved);
      });
  overlay_state_.SetOnAskUserSubmit(
      [this](::yac::ApprovalId approval_id, std::string response) {
        actions_.OnAskUserResponse(std::move(approval_id), std::move(response));
      });
  overlay_state_.SetOnAskUserCancel([this](::yac::ApprovalId approval_id) {
    actions_.OnAskUserCancel(std::move(approval_id));
  });
  input_controller_.SetOnModeToggle([this] { actions_.OnModeToggle(); });
}

ChatUI::~ChatUI() = default;

void ChatUI::SetAgentMode(chat::AgentMode mode) {
  agent_mode_ = mode;
}

void ChatUI::SetUiTaskRunner(UiTaskRunner ui_task_runner) {
  clock_ticker_.Start(ui_task_runner);
  thinking_animation_.SetUiTaskRunner(std::move(ui_task_runner));
  SyncThinkingAnimation();
}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();
  auto mcp_panel = McpStatusPanelComponent(mcp_status_);

  auto container = ftxui::Container::Stacked({message_list, input, mcp_panel});

  auto main_ui = ftxui::Renderer(container, [this, message_list, input,
                                             mcp_panel] {
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
    }

    auto mcp_element = mcp_panel->Render();
    mcp_element->ComputeRequirement();
    if (mcp_element->requirement().min_y > 0) {
      const auto servers = mcp_status_.GetSnapshot();
      if (!servers.empty()) {
        rail_center.push_back(mcp_element);
      }
    }

    // Right: context %, prompt-token count over window, help chip. We use
    // `prompt_tokens` (not total) so the indicator matches the auto-compact
    // trigger's signal — what fraction of the window the *next* request
    // will consume.
    ftxui::Elements rail_right;
    {
      const int window = overlay_state_.ContextWindowTokens();
      const int prompt = overlay_state_.LastUsage()
                             ? overlay_state_.LastUsage()->prompt_tokens
                             : 0;
      if (window > 0 && prompt > 0) {
        const double pct =
            (static_cast<double>(prompt) / static_cast<double>(window)) * 100.0;
        rail_right.push_back(ftxui::text(FormatPercent(pct) + " ") |
                             ftxui::color(PercentColor(pct)) | ftxui::bold);
        rail_right.push_back(
            ftxui::text(FormatTokens(prompt) + "/" + FormatTokens(window)) |
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
    const int input_wrap_width =
        ComposerInputWrapWidth(term_width, kMaxInputLines);
    const int line_count =
        composer_.CalculateHeight(kMaxInputLines, input_wrap_width);
    auto composer_content = ftxui::hbox({
        ftxui::text(std::string(layout::kComposerPadX, ' ')),
        ftxui::text(std::string(kComposerPrompt)) |
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
                         ftxui::bgcolor(colors.chrome.canvas_bg));

    if (composer_.IsSlashMenuActive() && !slash_commands_.Commands().empty()) {
      main_parts.push_back(input_controller_.RenderSlashMenu(term_width));
    }

    main_parts.push_back(
        composer_surface | ftxui::bgcolor(colors.chrome.canvas_bg) |
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
  if (auto trailing = session_.TrailingTextSegmentIndex(id);
      trailing.has_value()) {
    render_cache_.ResetTextSegment(id, *trailing);
  }
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

MessageId ChatUI::AddToolCallMessage(::yac::tool_call::ToolCallBlock block) {
  auto id = session_.AddToolCallSegment(std::move(block));
  render_cache_.ResetToolElement(id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
  return id;
}

void ChatUI::AddToolCallSegment(MessageId tool_id,
                                ::yac::tool_call::ToolCallBlock block,
                                MessageStatus status) {
  session_.AddToolCallSegment(tool_id, std::move(block), status);
  render_cache_.ResetToolElement(tool_id);
  scroll_state_.OnMessagesChanged();
  SyncMessageComponents();
}

void ChatUI::UpdateToolCallMessage(MessageId tool_id,
                                   ::yac::tool_call::ToolCallBlock block,
                                   MessageStatus status) {
  session_.UpdateToolCallSegment(tool_id, std::move(block), status);
  render_cache_.ResetToolElement(tool_id);
  // Do NOT rebuild message_components_ here. Collapsible now reads its
  // label and summary through providers that re-query the session each
  // frame, so the existing component tree picks up the update without
  // losing FTXUI focus on the card the user may have expanded.
}

void ChatUI::UpdateSubAgentToolCallMessage(
    MessageId parent_id, ::yac::ToolCallId tool_call_id, std::string tool_name,
    ::yac::tool_call::ToolCallBlock block, MessageStatus status) {
  const bool inserted = session_.UpsertSubAgentToolCall(
      parent_id, std::move(tool_call_id), std::move(tool_name),
      std::move(block), status);
  render_cache_.ResetToolElement(parent_id);
  scroll_state_.OnMessagesChanged();
  if (inserted) {
    RebuildMessageComponents();
  }
}

void ChatUI::ShowToolApproval(
    ::yac::ApprovalId approval_id, std::string tool_name, std::string prompt,
    std::optional<::yac::tool_call::ToolCallBlock> preview) {
  overlay_state_.ShowToolApproval(std::move(approval_id), std::move(tool_name),
                                  std::move(prompt), std::move(preview));
}

void ChatUI::ShowAskUserDialog(::yac::ApprovalId approval_id,
                               std::string question,
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

void ChatUI::SetProviderModel(::yac::ProviderId provider_id,
                              ::yac::ModelId model) {
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

void ChatUI::SetToolExpanded(MessageId tool_id, bool expanded) {
  session_.SetToolExpanded(tool_id, expanded);
}

void ChatUI::ClearMessages() {
  session_.ClearMessages();
  render_cache_.Clear();
  render_plan_.clear();
  message_components_.clear();
  plan_valid_ = false;
  scroll_state_.Clear();
  SyncThinkingAnimation();
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return session_.Messages();
}

bool ChatUI::HasMessage(MessageId id) const {
  return session_.HasMessage(id);
}

bool ChatUI::HasToolSegment(MessageId tool_id) const {
  return session_.HasToolSegment(tool_id);
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

int ChatUI::ContextWindowTokens() const {
  return overlay_state_.ContextWindowTokens();
}

}  // namespace yac::presentation
