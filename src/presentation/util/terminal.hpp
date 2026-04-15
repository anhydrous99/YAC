#pragma once

#include <cstdio>
#include <string_view>

namespace yac::presentation::terminal {

inline void SetTitle(std::string_view title) {
  std::printf("\033]0;%.*s\007", static_cast<int>(title.size()),
              title.data());
  std::fflush(stdout);
}

inline void SendNotification(std::string_view summary) {
  std::printf("\033]777;notify;YAC;%.*s\007", static_cast<int>(summary.size()),
              summary.data());
  std::fflush(stdout);
}

}  // namespace yac::presentation::terminal
