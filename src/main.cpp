#include "app/bootstrap.hpp"
#include "app/headless.hpp"
#include "cli/mcp_cli_dispatch.hpp"

#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "mcp") {
      return yac::cli::RunMcpCli(argc - 2, argv + 2);
    }
    if (argc >= 3 && std::string_view(argv[1]) == "run") {
      std::string prompt;
      bool auto_approve = false;
      int cancel_after_ms = 0;
      for (int i = 2; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--auto-approve") {
          auto_approve = true;
        } else if (arg.starts_with("--cancel-after-ms=")) {
          try {
            cancel_after_ms = std::stoi(std::string(arg.substr(18)));
          } catch (...) {
            std::cerr << "Error: --cancel-after-ms requires a valid integer\n";
            return 1;
          }
        } else if (arg == "--cancel-after-ms") {
          std::cerr << "Error: --cancel-after-ms requires a value\n";
          return 1;
        } else {
          if (!prompt.empty()) {
            prompt += ' ';
          }
          prompt += argv[i];
        }
      }
      return yac::app::RunHeadless(prompt, auto_approve, cancel_after_ms);
    }
    return yac::app::RunApp();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return 1;
  }
}
