#pragma once

#include "mcp/token_store.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace yac::mcp {

class FileTokenStore : public ITokenStore {
 public:
  explicit FileTokenStore(std::filesystem::path base_dir);
  FileTokenStore();

  [[nodiscard]] std::optional<std::string> Get(
      std::string_view server_id) const override;

  void Set(std::string_view server_id, std::string_view token_json) override;

  void Erase(std::string_view server_id) override;

 private:
  [[nodiscard]] std::filesystem::path TokenPath(
      std::string_view server_id) const;

  void EnsureBaseDir() const;

  std::filesystem::path base_dir_;
};

}  // namespace yac::mcp
