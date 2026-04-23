#include "dialog.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

#include <utility>

namespace yac::presentation {

namespace {
constexpr int kDialogMaxWidth = 72;

ftxui::Element DialogTitle(const std::string& title) {
  return ftxui::hbox({
             ftxui::text(" "),
             ftxui::text(title) | ftxui::bold,
             ftxui::filler(),
          }) |
          ftxui::color(theme::CurrentTheme().dialog.selected_fg) |
          ftxui::bgcolor(theme::CurrentTheme().dialog.selected_bg);
}

}  // namespace

ftxui::Component DialogModal(ftxui::Component main, ftxui::Component modal,
                             const bool* show) {
  class Impl : public ftxui::ComponentBase {
   public:
    Impl(ftxui::Component main, ftxui::Component modal, const bool* show)
        : main_(std::move(main)), modal_(std::move(modal)), show_(show) {
      Add(ftxui::Container::Tab({main_, modal_}, &selector_));
    }

    ftxui::Element OnRender() override {
      selector_ = IsShown() ? 1 : 0;
      auto document = main_->Render();
      if (!IsShown()) {
        return document;
      }

      auto backdrop =
          document | ftxui::dim |
          ftxui::bgcolor(theme::CurrentTheme().dialog.overlay_bg);
      return ftxui::dbox({
          backdrop,
          modal_->Render() | ftxui::clear_under | ftxui::center,
      });
    }

    bool OnEvent(ftxui::Event event) override {
      selector_ = IsShown() ? 1 : 0;
      return ComponentBase::OnEvent(event);
    }

   private:
    [[nodiscard]] bool IsShown() const { return show_ != nullptr && *show_; }

    ftxui::Component main_;
    ftxui::Component modal_;
    const bool* show_;
    int selector_ = 0;
  };

  return ftxui::Make<Impl>(std::move(main), std::move(modal), show);
}

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
          DialogTitle(title_),
          ftxui::text(""),
          ftxui::hbox({ftxui::text("  "),
                       inner_content_->Render() | ftxui::flex,
                       ftxui::text("  ")}),
          ftxui::text(""),
      });

      return panel | ftxui::bgcolor(theme::CurrentTheme().dialog.input_bg) |
             ftxui::borderStyled(ftxui::HEAVY,
                                 theme::CurrentTheme().dialog.selected_bg) |
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

    bool Focusable() const override { return show_ != nullptr && *show_; }

   private:
    std::string title_;
    ftxui::Component inner_content_;
    bool* show_;
  };

  return ftxui::Make<Impl>(title, std::move(inner_content), show);
}

}  // namespace yac::presentation
