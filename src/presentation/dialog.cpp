#include "dialog.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

namespace yac::presentation {

namespace {

inline const auto& k_theme = theme::Theme::Instance();
constexpr int kDialogMaxWidth = 60;

}  // namespace

ftxui::Component DialogPanel(std::string title, ftxui::Component inner_content,
                             bool* show) {
  class Impl : public ftxui::ComponentBase {
   public:
    Impl(std::string title, ftxui::Component inner_content, bool* show)
        : title_(std::move(title)),
          inner_content_(std::move(inner_content)),
          show_(show) {
      Add(inner_content_);
    }

    ftxui::Element OnRender() override {
      if (show_ == nullptr || !*show_) {
        return ftxui::emptyElement();
      }

      auto panel = ftxui::vbox({
          ftxui::text(title_) | ftxui::bold,
          ftxui::separator() | ftxui::color(k_theme.dialog.border),
          inner_content_->Render(),
      });

      return panel | ftxui::borderRounded |
             ftxui::color(k_theme.dialog.border) |
             ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, kDialogMaxWidth) |
             ftxui::center;
    }

    bool OnEvent(ftxui::Event event) override {
      if (show_ == nullptr || !*show_) {
        return false;
      }
      if (event == ftxui::Event::Escape) {
        *show_ = false;
        return true;
      }
      return ComponentBase::OnEvent(event);
    }

   private:
    std::string title_;
    ftxui::Component inner_content_;
    bool* show_;
  };

  return ftxui::Make<Impl>(title, std::move(inner_content), show);
}

}  // namespace yac::presentation
