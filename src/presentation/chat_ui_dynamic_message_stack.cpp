#include "chat_ui_dynamic_message_stack.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"

namespace yac::presentation::detail {

namespace {

class DynamicMessageStack : public ftxui::ComponentBase {
 public:
  DynamicMessageStack(std::function<std::uint64_t()> get_generation,
                      std::function<ftxui::Components()> get_children,
                      std::function<DynamicMessageStackViewport()> get_viewport,
                      std::function<bool()> get_active_tail_dirty)
      : get_generation_(std::move(get_generation)),
        get_children_(std::move(get_children)),
        get_viewport_(std::move(get_viewport)),
        get_active_tail_dirty_(std::move(get_active_tail_dirty)) {}

  ftxui::Element OnRender() override {
    SyncChildren();

    const int current_width = ftxui::Terminal::Size().dimx;
    if (current_width != last_width_) {
      measured_heights_.assign(children_.size(), -1);
      last_width_ = current_width;
    } else if (measured_heights_.size() != children_.size()) {
      measured_heights_.resize(children_.size(), -1);
    }
    if (!children_.empty() && get_active_tail_dirty_ &&
        get_active_tail_dirty_()) {
      measured_heights_.back() = -1;
    }

    const DynamicMessageStackViewport vp =
        get_viewport_ ? get_viewport_() : DynamicMessageStackViewport{};
    constexpr int kOverscan = 8;
    const bool have_viewport = vp.viewport_height > 0;
    const int vp_top = vp.scroll_offset_y - kOverscan;
    const int vp_bottom = vp.scroll_offset_y + vp.viewport_height + kOverscan;

    constexpr int kGap = 1;
    ftxui::Elements elements;
    elements.reserve(children_.size() * 2);
    int y_cursor = 0;
    for (std::size_t i = 0; i < children_.size(); ++i) {
      if (i > 0) {
        elements.push_back(ftxui::text(" "));
        y_cursor += kGap;
      }

      const int known_h = measured_heights_[i];
      const bool have_measurement = known_h >= 0;
      const int span_top = y_cursor;
      const int span_bottom = span_top + (have_measurement ? known_h : 0);

      const bool off_screen = have_viewport && have_measurement &&
                              (span_bottom <= vp_top || span_top >= vp_bottom);

      if (off_screen) {
        elements.push_back(ftxui::filler() |
                           ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, known_h));
        y_cursor = span_bottom;
      } else {
        auto rendered = children_[i]->Render();
        rendered->ComputeRequirement();
        const int measured = rendered->requirement().min_y;
        measured_heights_[i] = measured;
        elements.push_back(std::move(rendered));
        y_cursor = span_top + measured;
      }
    }
    return ftxui::vbox(std::move(elements));
  }

  bool OnEvent(ftxui::Event event) override {
    SyncChildren();
    return ComponentBase::OnEvent(event);
  }

 private:
  void SyncChildren() {
    const std::uint64_t generation = get_generation_();
    if (seen_generation_.has_value() && *seen_generation_ == generation) {
      return;
    }

    auto children = get_children_();
    const auto child_count = ChildCount();
    bool rebuild = children.size() < child_count;
    if (!rebuild) {
      for (std::size_t i = 0; i < child_count; ++i) {
        if (children[i] != children_[i]) {
          rebuild = true;
          break;
        }
      }
    }

    if (rebuild) {
      DetachAllChildren();
      measured_heights_.clear();
    }
    const auto start = rebuild ? 0 : child_count;
    for (std::size_t i = start; i < children.size(); ++i) {
      Add(children[i]);
    }
    if (measured_heights_.size() != children_.size()) {
      measured_heights_.resize(children_.size(), -1);
    }
    seen_generation_ = generation;
  }

  std::function<std::uint64_t()> get_generation_;
  std::function<ftxui::Components()> get_children_;
  std::function<DynamicMessageStackViewport()> get_viewport_;
  std::function<bool()> get_active_tail_dirty_;
  std::optional<std::uint64_t> seen_generation_;
  std::vector<int> measured_heights_;
  int last_width_ = -1;
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

ftxui::Component MakeDynamicMessageStack(
    std::function<std::uint64_t()> get_generation,
    std::function<ftxui::Components()> get_children,
    std::function<DynamicMessageStackViewport()> get_viewport,
    std::function<bool()> get_active_tail_dirty) {
  return ftxui::Make<DynamicMessageStack>(
      std::move(get_generation), std::move(get_children),
      std::move(get_viewport), std::move(get_active_tail_dirty));
}

ftxui::Component MakeSlashMenuInputWrapper(
    ftxui::Component input,
    std::function<bool(const ftxui::Event&)> pre_handler,
    std::function<void()> post_handler) {
  return ftxui::Make<SlashMenuInputWrapper>(std::move(input),
                                            std::move(pre_handler),
                                            std::move(post_handler));
}

}  // namespace yac::presentation::detail
