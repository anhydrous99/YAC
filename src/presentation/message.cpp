#include "message.hpp"

namespace yac::presentation {

std::string Message::DisplayLabel() const {
  if (!role_label.empty()) {
    return role_label;
  }
  switch (sender) {
    case Sender::User:
      return "You";
    case Sender::Agent:
      return "Assistant";
    case Sender::Tool:
      return "Tool";
  }

  return "Unknown";
}

}  // namespace yac::presentation
