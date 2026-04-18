#pragma once

#include "chat/types.hpp"

namespace yac::chat {

[[nodiscard]] ChatConfigResult LoadChatConfigResultFromEnv();
[[nodiscard]] ChatConfig LoadChatConfigFromEnv();

}  // namespace yac::chat
