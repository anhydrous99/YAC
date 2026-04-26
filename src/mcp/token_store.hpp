#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace yac::mcp {

class ITokenStore {
 public:
  virtual ~ITokenStore() = default;

  // Throws std::runtime_error on security violation (e.g. wrong file perms).
  [[nodiscard]] virtual std::optional<std::string> Get(
      std::string_view server_id) const = 0;

  virtual void Set(std::string_view server_id, std::string_view token_json) = 0;

  virtual void Erase(std::string_view server_id) = 0;
};

}  // namespace yac::mcp
