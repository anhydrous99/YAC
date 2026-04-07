#pragma once

#include "markdown/ast.hpp"

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

  [[nodiscard]] std::string DisplayLabel() const;
};

}  // namespace yac::presentation
