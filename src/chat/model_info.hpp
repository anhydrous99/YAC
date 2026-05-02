#pragma once

#include <string>

namespace yac::chat {

struct ModelInfo {
  std::string id;
  std::string display_name;
  // Context-window size in tokens advertised by the provider's /models
  // endpoint (e.g. OpenRouter's `context_length`, Anthropic's
  // `max_input_tokens`). 0 = not advertised; resolution falls through to the
  // provider's built-in table or the cross-provider lookup table.
  int context_window = 0;
};

}  // namespace yac::chat
