#pragma once

#include "markdown/ast.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

enum class Sender { User, Agent };

struct Message {
  Sender sender = Sender::User;
  std::string content;
  std::string role_label;
  std::string timestamp;
  std::optional<std::vector<markdown::BlockNode>> cached_blocks;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();

  [[nodiscard]] std::string DisplayLabel() const;
};

}  // namespace yac::presentation
