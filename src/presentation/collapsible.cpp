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

inline const auto& k_theme = theme::Theme::Instance();

constexpr auto kAnimationDuration = std::chrono::milliseconds(150);

}  // namespace

ftxui::Component Collapsible(std::string header_text, ftxui::Component content,
                             bool* expanded, std::string summary,
                             ftxui::Element peek) {
  class Impl : public ftxui::ComponentBase {
   public:
    Impl(std::string header_text, ftxui::Component content, bool* expanded,
         std::string summary, ftxui::Element peek)
        : header_text_(std::move(header_text)),
          content_(std::move(content)),
          expanded_(expanded),
          summary_(std::move(summary)),
          peek_(std::move(peek)),
          progress_(*expanded ? 1.0F : 0.0F),
          last_expanded_(*expanded) {
      Add(content_);
    }

    ftxui::Element OnRender() override {
      // Detect external state changes (e.g., programmatic toggle).
      if (*expanded_ != last_expanded_) {
        last_expanded_ = *expanded_;
        StartAnimation();
      }

      auto indicator = *expanded_ ? ftxui::text("\xe2\x96\xbc")
                                  : ftxui::text("\xe2\x96\xb6");
      indicator |= ftxui::color(k_theme.tool.icon_fg);

      ftxui::Elements header_parts;
      header_parts.push_back(indicator);
      header_parts.push_back(ftxui::text(" "));
      header_parts.push_back(ftxui::text(header_text_) | ftxui::bold |
                             ftxui::color(k_theme.chrome.body_text));
      if (!summary_.empty()) {
        header_parts.push_back(ftxui::filler());
        header_parts.push_back(ftxui::text(" ") |
                               ftxui::color(k_theme.chrome.dim_text));
        header_parts.push_back(ftxui::text(summary_) |
                               ftxui::color(k_theme.chrome.dim_text) |
                               ftxui::dim);
      }
      auto header_elem = ftxui::hbox(std::move(header_parts)) |
                         ftxui::bgcolor(k_theme.tool.header_bg);

      header_elem |= ftxui::reflect(header_box_);

      // Fully collapsed — show header + peek (if any).
      if (progress_ <= 0.0F) {
        if (peek_) {
          return ftxui::vbox({header_elem, peek_});
        }
        return header_elem;
      }

      auto rendered_content =
          content_->Render() | ftxui::color(k_theme.chrome.body_text);

      // Animating — clip content height.
      if (progress_ < 1.0F) {
        // Interpolate visible height. We use a generous upper bound since
        // FTXUI's LESS_THAN only constrains, never expands. Content shorter
        // than max_height renders at its natural size.
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
        // Keep requesting frames while animating.
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

    std::string header_text_;
    ftxui::Component content_;
    bool* expanded_;
    std::string summary_;
    ftxui::Element peek_;
    float progress_;
    bool last_expanded_;
    ftxui::Box header_box_{};
    std::unique_ptr<ftxui::animation::Animator> animator_;
  };

  return ftxui::Make<Impl>(std::move(header_text), std::move(content), expanded,
                           std::move(summary), std::move(peek));
}

}  // namespace yac::presentation
