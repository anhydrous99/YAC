#include "chat_ui.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "chat_ui_composer_render.hpp"
#include "chat_ui_dynamic_message_stack.hpp"
#include "chat_ui_notice_format.hpp"
#include "chat_ui_render_plan.hpp"
#include "collapsible.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "message_renderer.hpp"
#include "tool_call/renderer.hpp"
#include "util/scroll_math.hpp"

namespace yac::presentation {

namespace {

using detail::ComposerInputWrapWidth;
using detail::DynamicMessageStackViewport;
using detail::IsWhitespaceOnly;
using detail::MakeDynamicMessageStack;
using detail::MakeSlashMenuInputWrapper;
using detail::NoticeLine;
using detail::RenderWrappedComposerInput;

}  // namespace

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
    const int wrap_width = ComposerInputWrapWidth(ftxui::Terminal::Size().dimx,
                                                  ChatUI::kMaxInputLines);
    auto element = state.is_placeholder
                       ? state.element
                       : ftxui::dbox({state.element, RenderWrappedComposerInput(
                                                         composer_, wrap_width,
                                                         state.focused) |
                                                         ftxui::clear_under});
    element |= ftxui::color(render_context_.Colors().chrome.body_text);
    if (state.is_placeholder) {
      element |=
          ftxui::dim | ftxui::color(render_context_.Colors().chrome.dim_text);
      if (state.focused) {
        element |= ftxui::focusCursorBarBlinking;
      }
    }
    return element;
  };

  auto input = ftxui::Input(&composer_.Content(), option);

  return MakeSlashMenuInputWrapper(
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
  auto message_stack = MakeDynamicMessageStack(
      [this] { return session_.PlanGeneration(); },
      [this] {
        SyncMessageComponents();
        return message_components_;
      },
      [this] {
        return DynamicMessageStackViewport{
            .scroll_offset_y = scroll_state_.ScrollOffsetY(),
            .viewport_height = scroll_state_.ViewportHeight()};
      },
      [this] { return session_.HasActiveAgent(); });

  auto content = ftxui::Renderer(message_stack, [this, message_stack] {
    SyncTerminalWidth();
    auto msg_element =
        session_.Empty() && !is_typing_
            ? RenderEmptyState()
            : message_stack->Render() |
                  ftxui::color(render_context_.Colors().chrome.dim_text);

    const int term_width = ftxui::Terminal::Size().dimx;
    const uint64_t plan_gen = session_.PlanGeneration();
    const uint64_t content_gen = session_.ContentGeneration();
    if (!content_height_cache_valid_ ||
        content_height_cache_plan_gen_ != plan_gen ||
        content_height_cache_content_gen_ != content_gen ||
        content_height_cache_width_ != term_width) {
      msg_element->ComputeRequirement();
      content_height_cache_value_ = msg_element->requirement().min_y;
      content_height_cache_plan_gen_ = plan_gen;
      content_height_cache_content_gen_ = content_gen;
      content_height_cache_width_ = term_width;
      content_height_cache_valid_ = true;
    }
    scroll_state_.SetContentHeight(content_height_cache_value_);
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

ftxui::Component ChatUI::BuildToolContentComponent(MessageId tool_id) {
  const auto* segment = session_.FindToolSegment(tool_id);
  if (segment == nullptr) {
    return ftxui::Renderer([] { return ftxui::text("Tool call unavailable"); });
  }
  const bool is_sub_agent =
      std::holds_alternative<::yac::tool_call::SubAgentCall>(segment->block);

  auto tool_renderer = ftxui::Renderer([this, tool_id] {
    const auto* seg = session_.FindToolSegment(tool_id);
    if (seg == nullptr) {
      return ftxui::text("Tool call unavailable");
    }
    auto& cache = render_cache_.ForTool(tool_id);
    if (cache.element && cache.terminal_width == last_terminal_width_) {
      return *cache.element;
    }
    auto elem = tool_call::ToolCallRenderer::Render(
        seg->block,
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
    cache.element = elem;
    cache.terminal_width = last_terminal_width_;
    return elem;
  });

  if (!is_sub_agent) {
    return tool_renderer;
  }

  ftxui::Components rows{tool_renderer};
  const MessageId parent_id = tool_id;
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

ftxui::Component ChatUI::BuildToolCollapsible(MessageId tool_id) {
  auto content = BuildToolContentComponent(tool_id);
  auto label_provider = [this, tool_id]() -> std::string {
    const auto* seg = session_.FindToolSegment(tool_id);
    return seg ? tool_call::ToolCallRenderer::BuildLabel(seg->block)
               : std::string{};
  };
  auto summary_provider = [this, tool_id]() -> std::string {
    const auto* seg = session_.FindToolSegment(tool_id);
    if (seg == nullptr) {
      return {};
    }
    if (seg->status == MessageStatus::Active &&
        std::get_if<::yac::tool_call::FileWriteCall>(&seg->block) != nullptr) {
      return "writing\xe2\x80\xa6";
    }
    return tool_call::ToolCallRenderer::BuildSummary(seg->block);
  };
  const auto* segment = session_.FindToolSegment(tool_id);
  const auto* block = segment != nullptr ? &segment->block : nullptr;
  const auto status =
      segment != nullptr ? segment->status : MessageStatus::Complete;
  return Collapsible(std::move(label_provider), std::move(content),
                     session_.ToolExpandedState(tool_id),
                     std::move(summary_provider), BuildToolPeek(block, status));
}

ftxui::Component ChatUI::BuildUserMessageComponent(size_t message_index) {
  return ftxui::Renderer([this, message_index] {
    const auto& message = session_.Messages()[message_index];
    return MessageRenderer::Render(
        message, render_cache_.For(message.id),
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  });
}

ftxui::Component ChatUI::BuildAgentMessageComponent(size_t message_index) {
  ftxui::Components children;
  children.push_back(ftxui::Renderer([this, message_index] {
    const auto& msg = session_.Messages()[message_index];
    auto& cache = render_cache_.For(msg.id);
    return MessageRenderer::RenderAgentHeader(
        msg, cache.relative_time,
        RenderContext{.terminal_width = last_terminal_width_,
                      .thinking_frame = thinking_animation_.Frame()});
  }));

  const auto& message = session_.Messages()[message_index];
  size_t text_segment_idx = 0;
  for (size_t i = 0; i < message.segments.size(); ++i) {
    const auto& segment = message.segments[i];
    if (std::holds_alternative<TextSegment>(segment)) {
      const size_t this_text_idx = text_segment_idx++;
      children.push_back(
          ftxui::Renderer([this, message_index, i, this_text_idx] {
            const auto& msg = session_.Messages()[message_index];
            if (i >= msg.segments.size()) {
              return ftxui::text("");
            }
            const auto* text_seg = std::get_if<TextSegment>(&msg.segments[i]);
            if (text_seg == nullptr) {
              return ftxui::text("");
            }
            bool is_streaming = false;
            if (msg.status == MessageStatus::Active) {
              for (size_t j = msg.segments.size(); j-- > 0;) {
                if (std::holds_alternative<TextSegment>(msg.segments[j])) {
                  is_streaming = j == i;
                  break;
                }
              }
            }
            auto& cache = render_cache_.For(msg.id);
            auto& seg_cache = cache.EnsureSegment(this_text_idx);
            return MessageRenderer::RenderTextSegment(
                text_seg->text, is_streaming, seg_cache,
                RenderContext{.terminal_width = last_terminal_width_,
                              .thinking_frame = thinking_animation_.Frame()});
          }));
    } else if (const auto* tool_seg = std::get_if<ToolSegment>(&segment)) {
      children.push_back(BuildToolCollapsible(tool_seg->id));
    }
  }

  auto stack = ftxui::Container::Vertical(std::move(children));
  return ftxui::Renderer(stack, [this, stack] {
    auto context = RenderContext{.terminal_width = last_terminal_width_,
                                 .thinking_frame = thinking_animation_.Frame()};
    const auto& theme_ref = context.Colors();
    auto inner = stack->Render();
    auto styled_card = MessageRenderer::CardSurface(
        std::move(inner), theme_ref.cards.agent_bg, context);
    return ftxui::hbox({styled_card | ftxui::xflex_shrink, ftxui::filler()});
  });
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
  plan_valid_ = false;
  SyncMessageComponents();
}

void ChatUI::SyncTerminalWidth() {
  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width == last_terminal_width_) {
    return;
  }
  for (const auto& msg : session_.Messages()) {
    render_cache_.ResetElement(msg.id);
  }
  last_terminal_width_ = current_width;
}

void ChatUI::SyncMessageComponents() {
  SyncTerminalWidth();

  const uint64_t gen = session_.PlanGeneration();
  if (plan_valid_ && gen == last_plan_generation_) {
    return;
  }

  render_plan_ = BuildMessageRenderPlan(session_.Messages());
  message_components_.clear();
  message_components_.reserve(render_plan_.size());
  for (const auto& item : render_plan_) {
    switch (item.kind) {
      case MessageRenderItem::Kind::User:
        message_components_.push_back(
            BuildUserMessageComponent(item.message_index));
        break;
      case MessageRenderItem::Kind::Agent:
        message_components_.push_back(
            BuildAgentMessageComponent(item.message_index));
        break;
    }
  }
  last_plan_generation_ = gen;
  plan_valid_ = true;
}

bool ChatUI::HasActiveAgentMessage() const {
  return session_.HasActiveAgent();
}

void ChatUI::SyncThinkingAnimation() {
  thinking_animation_.Sync([this] { return HasActiveAgentMessage(); });
}

}  // namespace yac::presentation
