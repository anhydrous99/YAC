#include "collapsible.hpp"

#include "ftxui/component/animation.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yac::presentation {

namespace {

constexpr auto kAnimationDuration = std::chrono::milliseconds(150);

class CollapsibleImpl : public ftxui::ComponentBase {
 public:
  CollapsibleImpl(std::function<std::string()> header_provider,
                  ftxui::Component content, bool* expanded,
                  std::function<std::string()> summary_provider,
                  ftxui::Element peek)
      : header_provider_(std::move(header_provider)),
        content_(std::move(content)),
        expanded_(expanded),
        summary_provider_(std::move(summary_provider)),
        peek_(std::move(peek)),
        progress_(*expanded ? 1.0F : 0.0F),
        last_expanded_(*expanded) {
    Add(content_);
  }

  ftxui::Element OnRender() override {
    if (*expanded_ != last_expanded_) {
      last_expanded_ = *expanded_;
      StartAnimation();
    }

    auto indicator =
        *expanded_ ? ftxui::text("\xe2\x96\xbc") : ftxui::text("\xe2\x96\xb6");
    indicator |= ftxui::color(theme::CurrentTheme().tool.icon_fg);

    const std::string header_text =
        header_provider_ ? header_provider_() : std::string{};
    const std::string summary =
        summary_provider_ ? summary_provider_() : std::string{};

    ftxui::Elements header_parts;
    header_parts.push_back(indicator);
    header_parts.push_back(ftxui::text(" "));
    header_parts.push_back(ftxui::text(header_text) | ftxui::bold |
                           ftxui::color(theme::CurrentTheme().chrome.body_text));
    if (!summary.empty()) {
      header_parts.push_back(ftxui::filler());
      header_parts.push_back(ftxui::text(" ") |
                             ftxui::color(theme::CurrentTheme().chrome.dim_text));
      header_parts.push_back(ftxui::text(summary) |
                             ftxui::color(theme::CurrentTheme().chrome.dim_text) |
                             ftxui::dim);
    }
    auto header_elem = ftxui::hbox(std::move(header_parts)) |
                       ftxui::bgcolor(theme::CurrentTheme().tool.header_bg);

    header_elem |= ftxui::reflect(header_box_);

    if (progress_ <= 0.0F) {
      if (peek_) {
        return ftxui::vbox({header_elem, peek_});
      }
      return header_elem;
    }

    auto rendered_content =
        content_->Render() | ftxui::color(theme::CurrentTheme().chrome.body_text);

    if (progress_ < 1.0F) {
      int max_height =
          std::max(1, static_cast<int>(std::ceil(progress_ * 50.0F)));
      rendered_content =
          rendered_content |
          ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, max_height);
    }

    if (peek_) {
      return ftxui::vbox({header_elem, peek_, rendered_content});
    }
    return ftxui::vbox({
        header_elem,
        rendered_content,
    });
  }

  bool OnEvent(ftxui::Event event) override {
    if (!event.is_mouse()) {
      return false;
    }

    if (event.mouse().button == ftxui::Mouse::Left &&
        event.mouse().motion == ftxui::Mouse::Pressed) {
      if (header_box_.Contain(event.mouse().x, event.mouse().y)) {
        *expanded_ = !*expanded_;
        last_expanded_ = *expanded_;
        StartAnimation();
        return true;
      }
    }

    return *expanded_ && ComponentBase::OnEvent(event);
  }

  void OnAnimation(ftxui::animation::Params& params) override {
    if (animator_) {
      animator_->OnAnimation(params);
      float target = *expanded_ ? 1.0F : 0.0F;
      if (std::abs(progress_ - target) > 0.001F) {
        ftxui::animation::RequestAnimationFrame();
      } else {
        animator_.reset();
        progress_ = target;
      }
    }
  }

 private:
  void StartAnimation() {
    float target = *expanded_ ? 1.0F : 0.0F;
    animator_ = std::make_unique<ftxui::animation::Animator>(
        &progress_, target, kAnimationDuration,
        ftxui::animation::easing::QuadraticInOut);
    ftxui::animation::RequestAnimationFrame();
  }

  std::function<std::string()> header_provider_;
  ftxui::Component content_;
  bool* expanded_;
  std::function<std::string()> summary_provider_;
  ftxui::Element peek_;
  float progress_;
  bool last_expanded_;
  ftxui::Box header_box_{};
  std::unique_ptr<ftxui::animation::Animator> animator_;
};

}  // namespace

ftxui::Component Collapsible(std::string header_text, ftxui::Component content,
                             bool* expanded, std::string summary,
                             ftxui::Element peek) {
  auto header_provider = [header_text = std::move(header_text)] {
    return header_text;
  };
  auto summary_provider = [summary = std::move(summary)] { return summary; };
  return ftxui::Make<CollapsibleImpl>(
      std::move(header_provider), std::move(content), expanded,
      std::move(summary_provider), std::move(peek));
}

ftxui::Component Collapsible(std::function<std::string()> header_provider,
                             ftxui::Component content, bool* expanded,
                             std::function<std::string()> summary_provider,
                             ftxui::Element peek) {
  return ftxui::Make<CollapsibleImpl>(
      std::move(header_provider), std::move(content), expanded,
      std::move(summary_provider), std::move(peek));
}

}  // namespace yac::presentation
