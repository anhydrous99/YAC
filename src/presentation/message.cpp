#include "message.hpp"

namespace yac::presentation {

std::string Message::DisplayLabel() const {
  if (!role_label.empty()) {
    return role_label;
  }
  return (sender == Sender::User) ? "You" : "Assistant";
}

}  // namespace yac::presentation
