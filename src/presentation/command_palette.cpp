#include "command_palette.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"
#include "ui_spacing.hpp"
#include "util/string_util.hpp"

#include <memory>
#include <string>
#include <utility>

namespace yac::presentation {

namespace {

bool CommandsEqual(const std::vector<Command>& lhs,
                   const std::vector<Command>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].id != rhs[i].id || lhs[i].name != rhs[i].name ||
        lhs[i].description != rhs[i].description) {
      return false;
    }
  }
  return true;
}

}  // namespace

ftxui::Component CommandPalette(std::vector<Command> commands,
                                std::function<void(int)> on_select,
                                bool* show) {
  auto commands_store =
      std::make_shared<std::vector<Command>>(std::move(commands));
  return CommandPalette([commands_store] { return *commands_store; },
                        std::move(on_select), show);
}

ftxui::Component CommandPalette(std::function<std::vector<Command>()> commands,
                                std::function<void(int)> on_select,
                                bool* show) {
  class Impl : public ftxui::ComponentBase {
   public:
    Impl(std::function<std::vector<Command>()> commands,
         std::function<void(int)> on_select, bool* show)
        : commands_source_(std::move(commands)),
          on_select_(std::move(on_select)),
          show_(show) {
      SyncCommands();
      input_ = BuildInput();
      Add(input_);
      RefreshFiltered();
    }

    ftxui::Element OnRender() override {
      SyncCommands();
      ftxui::Elements children;
      children.push_back(
          input_->Render() |
          ftxui::color(theme::CurrentTheme().dialog.input_fg) |
          ftxui::borderStyled(ftxui::LIGHT,
                              theme::CurrentTheme().semantic.focus_ring));
      children.push_back(
          ftxui::text("") |
          ftxui::color(theme::CurrentTheme().semantic.border_subtle));

      if (filtered_indices_.empty()) {
        children.push_back(ftxui::text("No commands found") |
                           ftxui::color(theme::CurrentTheme().dialog.dim_text) |
                           ftxui::dim);
      } else {
        ftxui::Elements rows;
        rows.reserve(filtered_indices_.size());
        for (int i = 0; i < static_cast<int>(filtered_indices_.size()); ++i) {
          rows.push_back(RenderRow(i));
        }
        children.push_back(ftxui::vbox(std::move(rows)) | ftxui::yflex);
      }

      return ftxui::vbox(std::move(children));
    }

    bool OnEvent(ftxui::Event event) override {
      if (show_ != nullptr && !*show_) {
        return false;
      }
      SyncCommands();

      if (event == ftxui::Event::Escape) {
        if (show_ != nullptr) {
          *show_ = false;
        }
        return true;
      }

      if (event == ftxui::Event::ArrowUp) {
        MoveSelection(-1);
        return !filtered_indices_.empty();
      }

      if (event == ftxui::Event::ArrowDown) {
        MoveSelection(1);
        return !filtered_indices_.empty();
      }

      if (event == ftxui::Event::Return) {
        if (filtered_indices_.empty()) {
          return false;
        }
        on_select_(filtered_indices_[selected_index_]);
        if (show_ != nullptr) {
          *show_ = false;
        }
        return true;
      }

      if (input_->OnEvent(event)) {
        RefreshFiltered();
        return true;
      }

      return false;
    }

   private:
    ftxui::Component BuildInput() {
      ftxui::InputOption option;
      option.placeholder = "Type to filter...";
      option.cursor_position = &filter_cursor_;
      option.transform = [](ftxui::InputState state) {
        state.element |= ftxui::color(theme::CurrentTheme().dialog.input_fg);
        if (state.is_placeholder) {
          state.element |=
              ftxui::color(theme::CurrentTheme().dialog.dim_text) | ftxui::dim;
        }
        if (state.focused) {
          state.element |= ftxui::focusCursorBarBlinking;
        }
        return state.element;
      };

      return ftxui::Input(&filter_text_, option);
    }

    [[nodiscard]] ftxui::Element RenderRow(int filtered_index) const {
      const auto command_index =
          filtered_indices_.at(static_cast<size_t>(filtered_index));
      const auto& command = commands_.at(command_index);
      bool selected = filtered_index == selected_index_;

      auto name = ftxui::text(command.name) | ftxui::bold;
      auto desc = ftxui::text(std::string(layout::kCardPadX, ' ') +
                              command.description);

      if (selected) {
        auto bar =
            ftxui::text(" ") |
            ftxui::bgcolor(theme::CurrentTheme().semantic.accent_primary);
        name |= ftxui::color(theme::CurrentTheme().dialog.selected_fg);
        desc |= ftxui::color(theme::CurrentTheme().semantic.text_weak);
        return ftxui::hbox({bar, ftxui::text(std::string(layout::kRowGap, ' ')),
                            name, desc | ftxui::flex}) |
               ftxui::bgcolor(theme::CurrentTheme().semantic.selection_bg);
      }

      name |= ftxui::color(theme::CurrentTheme().dialog.input_fg);
      desc |= ftxui::color(theme::CurrentTheme().semantic.text_weak);
      return ftxui::hbox({ftxui::text(std::string(layout::kCardPadX, ' ')),
                          name, desc | ftxui::flex});
    }

    void RefreshFiltered() {
      int previous_original =
          filtered_indices_.empty() ? -1 : filtered_indices_[selected_index_];
      filtered_indices_.clear();

      auto lowered_filter = ::yac::util::ToLowerAscii(filter_text_);
      for (int i = 0; i < static_cast<int>(commands_.size()); ++i) {
        auto haystack = ::yac::util::ToLowerAscii(commands_[i].name + " " +
                                                  commands_[i].description);
        if (lowered_filter.empty() ||
            haystack.find(lowered_filter) != std::string::npos) {
          filtered_indices_.push_back(i);
        }
      }

      if (filtered_indices_.empty()) {
        selected_index_ = 0;
        return;
      }

      auto it = std::find(filtered_indices_.begin(), filtered_indices_.end(),
                          previous_original);
      if (it != filtered_indices_.end()) {
        selected_index_ =
            static_cast<int>(std::distance(filtered_indices_.begin(), it));
        return;
      }

      selected_index_ = 0;
    }

    void MoveSelection(int delta) {
      if (filtered_indices_.empty()) {
        selected_index_ = 0;
        return;
      }

      int size = static_cast<int>(filtered_indices_.size());
      selected_index_ = (selected_index_ + delta + size) % size;
    }

    void SyncCommands() {
      if (!commands_source_) {
        return;
      }
      auto next = commands_source_();
      if (CommandsEqual(commands_, next)) {
        return;
      }
      commands_ = std::move(next);
      RefreshFiltered();
    }

    std::function<std::vector<Command>()> commands_source_;
    std::vector<Command> commands_;
    std::function<void(int)> on_select_;
    bool* show_;
    std::string filter_text_;
    int filter_cursor_ = 0;
    int selected_index_ = 0;
    std::vector<int> filtered_indices_;
    ftxui::Component input_;
  };

  return ftxui::Make<Impl>(std::move(commands), std::move(on_select), show);
}

}  // namespace yac::presentation
