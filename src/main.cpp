#include "app/bootstrap.hpp"
#include "app/headless.hpp"

#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
  if (argc >= 3 && std::string_view(argv[1]) == "run") {
    std::string prompt;
    bool auto_approve = false;
    for (int i = 2; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--auto-approve") {
        auto_approve = true;
      } else {
        if (!prompt.empty()) {
          prompt += ' ';
        }
        prompt += argv[i];
      }
    }
    return yac::app::RunHeadless(prompt, auto_approve);
  }
  return yac::app::RunApp();
}
