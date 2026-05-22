#pragma once

#include <string>

namespace yac::app {

[[nodiscard]] int RunHeadless(const std::string& prompt, bool auto_approve,
                              int cancel_after_ms = 0, bool plan_mode = false);

}  // namespace yac::app
