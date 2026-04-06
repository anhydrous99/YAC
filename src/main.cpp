
#include "presentation/chat_ui.hpp"

#include <ftxui/component/app.hpp>

int main() {
  yac::presentation::ChatUI chat_ui;

  // Add test messages to demonstrate styling
  chat_ui.AddMessage(yac::presentation::Sender::User,
                     "Hello! Can you help me with some C++ code?");

  chat_ui.AddMessage(yac::presentation::Sender::Agent,
                     "# Code Example\n\n"
                     "Here's a simple **C++ function**:\n\n"
                     "```cpp\n"
                     "auto greet(std::string_view name) {\n"
                     "    return \"Hello, \" + std::string(name);\n"
                     "}\n"
                     "```\n\n"
                     "This function demonstrates:\n"
                     "- Using `std::string_view` for efficiency\n"
                     "- *Modern C++* features\n"
                     "- Clean code principles\n\n"
                     "> Note: This is a simplified example for demonstration.");

  auto component = chat_ui.Build();

  auto screen = ftxui::App::Fullscreen();
  screen.Loop(component);

  return 0;
}
