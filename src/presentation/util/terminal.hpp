#pragma once

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace yac::presentation::terminal {

inline void SetTitle(std::string_view title) {
  std::printf("\033]0;%.*s\007", static_cast<int>(title.size()), title.data());
  std::fflush(stdout);
}

inline void SendNotification(std::string_view summary) {
  std::printf("\033]777;notify;YAC;%.*s\007", static_cast<int>(summary.size()),
              summary.data());
  std::fflush(stdout);
}

// OSC 11 — ask the terminal to paint its background in the given 24-bit RGB
// color. Terminals that don't recognize the sequence silently ignore it.
inline void SetBackgroundColor(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue) {
  std::printf("\033]11;rgb:%02x/%02x/%02x\007", red, green, blue);
  std::fflush(stdout);
}

// OSC 111 — restore the terminal's default background color.
inline void ResetBackgroundColor() {
  std::printf("\033]111\007");
  std::fflush(stdout);
}

// RAII wrapper: emits OSC 11 on construction and OSC 111 on destruction so
// the terminal's theme is restored when the app exits its scope.
class BackgroundGuard {
 public:
  BackgroundGuard(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    SetBackgroundColor(red, green, blue);
  }
  ~BackgroundGuard() { ResetBackgroundColor(); }

  BackgroundGuard(const BackgroundGuard&) = delete;
  BackgroundGuard& operator=(const BackgroundGuard&) = delete;
  BackgroundGuard(BackgroundGuard&&) = delete;
  BackgroundGuard& operator=(BackgroundGuard&&) = delete;
};

}  // namespace yac::presentation::terminal
