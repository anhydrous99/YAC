#pragma once

#include "mcp/token_store.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yac::mcp {

class KeychainUnavailableError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class KeychainTokenStore : public ITokenStore {
 public:
  // Result is cached on first call; never throws.
  [[nodiscard]] static bool IsKeychainAvailable();

  // Returns nullopt for a missing entry. Throws KeychainUnavailableError on
  // backend failure.
  [[nodiscard]] std::optional<std::string> Get(
      std::string_view server_id) const override;

  // Throws KeychainUnavailableError on backend failure.
  void Set(std::string_view server_id, std::string_view token_json) override;

  // Missing entry is silently ignored. Throws KeychainUnavailableError on
  // backend failure.
  void Erase(std::string_view server_id) override;
};

}  // namespace yac::mcp
