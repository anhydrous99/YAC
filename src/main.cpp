
#include "presentation/chat_ui.hpp"

#include <ftxui/component/app.hpp>

int main() {
  yac::presentation::ChatUI chat_ui;

  chat_ui.SetCommands({
      {"New Chat", "Start a fresh conversation"},
      {"Clear Messages", "Remove all messages from the view"},
      {"Toggle Theme", "Switch between light and dark themes"},
      {"Export Chat", "Save the current transcript"},
      {"Help", "Show keyboard shortcuts and tips"},
  });

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

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::BashCall{
      .command = "ls src",
      .output = "main.cpp\npresentation/",
      .exit_code = 0,
      .is_error = false,
  });

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::FileReadCall{
      .filepath = "src/main.cpp",
      .lines_loaded = 33,
      .excerpt = "Current demo only seeds a user question and an agent "
                 "markdown reply.",
  });

  chat_ui.AddMessage(
      yac::presentation::Sender::Agent,
      "I found the entrypoint. Next I’m checking the presentation layer "
      "and keeping the sample flow compact.");

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::GrepCall{
      .pattern = "AddToolCallMessage",
      .match_count = 1,
      .matches = {{.filepath = "src/presentation/chat_ui.hpp",
                   .line = 30,
                   .content = "void "
                              "AddToolCallMessage(::yac::presentation::tool_"
                              "call::ToolCallBlock "
                              "block);"}},
  });

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::GlobCall{
      .pattern = "src/presentation/**/*.hpp",
      .matched_files = {"src/presentation/chat_ui.hpp",
                        "src/presentation/command_palette.hpp",
                        "src/presentation/tool_call/types.hpp"},
  });

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::WebSearchCall{
      .query = "FTXUI command palette component patterns",
      .results = {{.title = "FTXUI component patterns",
                   .url = "https://github.com/ArthurSonzogni/FTXUI",
                   .snippet =
                       "Terminal UI components, inputs, and modal dialogs."}},
  });

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::WebFetchCall{
      .url = "https://github.com/ArthurSonzogni/FTXUI",
      .title = "FTXUI",
      .excerpt = "Component and modal APIs used for terminal UIs.",
  });

  chat_ui.AddMessage(
      yac::presentation::Sender::Agent,
      "The structure is small, so I can keep the demo concise while still "
      "covering every tool type.");

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::FileEditCall{
      .filepath = "src/main.cpp",
      .diff =
          {{.type = yac::presentation::tool_call::DiffLine::Context,
            .content =
                "  chat_ui.AddMessage(yac::presentation::Sender::Agent, ...);"},
           {.type = yac::presentation::tool_call::DiffLine::Add,
            .content = "  chat_ui.SetCommands(...);"}},
  });

  chat_ui.AddToolCallMessage(yac::presentation::tool_call::BashCall{
      .command = "cmake --build build",
      .output = "[1/2] Building CXX object src/CMakeFiles/yac.dir/main.cpp.o\n"
                "[2/2] Linking CXX executable yac",
      .exit_code = 0,
      .is_error = false,
  });

  chat_ui.AddMessage(
      yac::presentation::Sender::Agent,
      "Done — the demo now shows command palette entries, every tool type, "
      "and a realistic back-and-forth.");

  auto component = chat_ui.Build();

  auto screen = ftxui::App::Fullscreen();
  screen.Loop(component);

  return 0;
}
