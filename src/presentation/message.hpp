#pragma once

#include <string>

namespace yac::presentation {

enum class Sender { User, Agent };

struct Message {
  Sender sender = Sender::User;
  std::string content;
  std::string role_label;  // Display name (e.g. "You", "Assistant"). Empty =
                           // derive from Sender.
  std::string timestamp;   // ISO-8601 or empty. Reserved for future use.

  /// Convenience: returns the display label, falling back to a default.
  [[nodiscard]] std::string DisplayLabel() const;
};

}  // namespace yac::presentation
