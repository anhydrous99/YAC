#pragma once

#include "chat/types.hpp"

#include <optional>
#include <string>

namespace yac::chat {

[[nodiscard]] ChatConfig LoadChatConfigFromEnv();

}  // namespace yac::chat
