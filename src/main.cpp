
#include "presentation/chat_ui.hpp"

#include <ftxui/component/app.hpp>

int main() {
  yac::presentation::ChatUI chat_ui;
  auto component = chat_ui.Build();

  auto screen = ftxui::App::Fullscreen();
  screen.Loop(component);

  return 0;
}
