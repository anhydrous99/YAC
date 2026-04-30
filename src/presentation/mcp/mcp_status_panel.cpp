#include "mcp_status_panel.hpp"

#include "../theme.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"

#include <array>
#include <string>
#include <string_view>

namespace yac::presentation {

namespace {

constexpr char kIconReady[] = "\xe2\x9c\x93";
constexpr char kIconFailed[] = "\xe2\x9c\x97";
constexpr char kIconReconnecting[] = "\xe2\x9f\xb3";
constexpr char kArrowCollapsed[] = " \xe2\x96\xb6 ";
constexpr char kArrowExpanded[] = " \xe2\x96\xbc ";

enum class StateColorRole {
  Ready,
  Failed,
  Pending,
  Unknown,
};

struct StateMetadata {
  std::string_view state;
  std::string_view icon;
  StateColorRole color;
};

constexpr std::array<StateMetadata, 4> kStateTable = {{
    {.state = "ready", .icon = kIconReady, .color = StateColorRole::Ready},
    {.state = "failed", .icon = kIconFailed, .color = StateColorRole::Failed},
    {.state = "reconnecting",
     .icon = kIconReconnecting,
     .color = StateColorRole::Pending},
    {.state = "connecting",
     .icon = kIconReconnecting,
     .color = StateColorRole::Pending},
}};

[[nodiscard]] const StateMetadata* FindState(std::string_view state) {
  for (const auto& entry : kStateTable) {
    if (entry.state == state) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string StateIcon(const std::string& state) {
  if (const auto* meta = FindState(state)) {
    return std::string(meta->icon);
  }
  return "?";
}

[[nodiscard]] ftxui::Color StateColor(const std::string& state) {
  const auto& t = theme::CurrentTheme();
  if (const auto* meta = FindState(state)) {
    switch (meta->color) {
      case StateColorRole::Ready:
        return t.role.agent;
      case StateColorRole::Failed:
        return t.role.error;
      case StateColorRole::Pending:
        return ftxui::Color::Yellow;
      case StateColorRole::Unknown:
        break;
    }
  }
  return t.semantic.text_muted;
}

}  // namespace

McpStatusPanel::McpStatusPanel(const McpStatusSink& sink) : sink_(sink) {}

ftxui::Element McpStatusPanel::OnRender() {
  const auto servers = sink_.GetSnapshot();
  const auto& t = theme::CurrentTheme();

  int active = 0;
  int errors = 0;
  for (const auto& srv : servers) {
    if (srv.state == "ready") {
      ++active;
    }
    if (srv.state == "failed") {
      ++errors;
    }
  }

  const std::string arrow = expanded_ ? kArrowExpanded : kArrowCollapsed;
  const std::string header_text = "MCP (" + std::to_string(active) +
                                  " active, " + std::to_string(errors) +
                                  " error" + (errors != 1 ? "s" : "") + ")";

  auto header = ftxui::hbox({
      ftxui::text(arrow) | ftxui::color(t.semantic.text_muted),
      ftxui::text(header_text) | ftxui::color(t.semantic.text_weak),
  });

  ftxui::Elements rows;
  rows.push_back(header | ftxui::reflect(header_box_));

  if (expanded_) {
    for (const auto& srv : servers) {
      auto icon = ftxui::text("  " + StateIcon(srv.state) + " ") |
                  ftxui::color(StateColor(srv.state));
      auto label = ftxui::text(srv.id) | ftxui::color(t.semantic.text_weak);
      ftxui::Element row = ftxui::hbox({icon, label});
      if (srv.state == "failed" && !srv.error.empty()) {
        auto err = ftxui::text(" " + srv.error) |
                   ftxui::color(t.semantic.text_muted) | ftxui::dim;
        row = ftxui::hbox({icon, label, err});
      }
      rows.push_back(row);
    }
  }

  return ftxui::vbox(std::move(rows));
}

bool McpStatusPanel::OnEvent(ftxui::Event event) {
  if (event.is_mouse()) {
    const auto& mouse = event.mouse();
    if (mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      if (header_box_.x_min <= mouse.x && mouse.x <= header_box_.x_max &&
          header_box_.y_min <= mouse.y && mouse.y <= header_box_.y_max) {
        expanded_ = !expanded_;
        return true;
      }
    }
  }

  if (event == ftxui::Event::Return || event == ftxui::Event::Character("m")) {
    expanded_ = !expanded_;
    return true;
  }

  return false;
}

ftxui::Component McpStatusPanelComponent(const McpStatusSink& sink) {
  return ftxui::Make<McpStatusPanel>(sink);
}

}  // namespace yac::presentation
