#pragma once

#include "chat/types.hpp"
#include "presentation/chat_event_sink.hpp"

#include <functional>

namespace yac::app {

class ChatEventBridge {
 public:
  explicit ChatEventBridge(presentation::ChatEventSink& chat_ui);

  void HandleEvent(chat::ChatEvent event);

 private:
  std::reference_wrapper<presentation::ChatEventSink> chat_ui_;
};

}  // namespace yac::app
