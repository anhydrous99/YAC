#pragma once

#include "chat/types.hpp"

namespace yac::chat {

[[nodiscard]] ChatConfig LoadChatConfigFromEnv();

}  // namespace yac::chat
