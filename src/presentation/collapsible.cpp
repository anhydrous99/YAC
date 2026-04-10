#include "collapsible.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

#include <utility>

namespace yac::presentation {

namespace {

inline const auto& k_theme = theme::Theme::Instance();

}  // namespace

ftxui::Component Collapsible(std::string header_text, ftxui::Component content,
                             bool* expanded) {
  class Impl : public ftxui::ComponentBase {
   public:
    Impl(std::string header_text, ftxui::Component content, bool* expanded)
        : header_text_(std::move(header_text)),
          content_(std::move(content)),
          expanded_(expanded) {
      Add(content_);
    }

    ftxui::Element OnRender() override {
      auto indicator = *expanded_ ? ftxui::text("\xe2\x96\xbc")
                                  : ftxui::text("\xe2\x96\xb6");
      indicator |= ftxui::color(k_theme.tool.icon_fg);

      auto header_elem = ftxui::hbox({
                             indicator,
                             ftxui::text(" "),
                             ftxui::text(header_text_) | ftxui::bold |
                                 ftxui::color(k_theme.chrome.body_text),
                         }) |
                         ftxui::bgcolor(k_theme.tool.header_bg);

      header_elem |= ftxui::reflect(header_box_);

      if (*expanded_) {
        return ftxui::vbox({
            header_elem,
            content_->Render() | ftxui::color(k_theme.chrome.body_text),
        });
      }
      return header_elem;
    }

    bool OnEvent(ftxui::Event event) override {
      if (!event.is_mouse()) {
        return false;
      }

      if (event.mouse().button == ftxui::Mouse::Left &&
          event.mouse().motion == ftxui::Mouse::Pressed) {
        if (header_box_.Contain(event.mouse().x, event.mouse().y)) {
          *expanded_ = !*expanded_;
          return true;
        }
      }

      return *expanded_ && ComponentBase::OnEvent(event);
    }

   private:
    std::string header_text_;
    ftxui::Component content_;
    bool* expanded_;
    ftxui::Box header_box_{};
  };

  return ftxui::Make<Impl>(std::move(header_text), std::move(content),
                           expanded);
}

}  // namespace yac::presentation
