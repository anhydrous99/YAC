#pragma once

#include "chat/types.hpp"
#include "presentation/chat_ui.hpp"

namespace yac::app {

class ChatEventBridge {
 public:
  explicit ChatEventBridge(presentation::ChatUI& chat_ui);

  void HandleEvent(chat::ChatEvent event);

 private:
  presentation::ChatUI* chat_ui_;
};

}  // namespace yac::app
