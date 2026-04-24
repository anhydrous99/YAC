#pragma once

#include <string>

namespace yac::app {

[[nodiscard]] int RunHeadless(const std::string& prompt, bool auto_approve);

}  // namespace yac::app
