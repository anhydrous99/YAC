#pragma once

#include "tool_call/types.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace yac::tool_call {

struct LspServerConfig {
  std::string command = "clangd";
  std::vector<std::string> args;
  std::filesystem::path workspace_root;
  std::chrono::milliseconds request_timeout{5000};
  std::chrono::milliseconds diagnostics_timeout{2000};
};

class ILspClient {
 public:
  ILspClient() = default;
  virtual ~ILspClient() = default;

  ILspClient(const ILspClient&) = delete;
  ILspClient& operator=(const ILspClient&) = delete;
  ILspClient(ILspClient&&) = delete;
  ILspClient& operator=(ILspClient&&) = delete;

  [[nodiscard]] virtual LspDiagnosticsCall Diagnostics(
      const std::string& file_path) = 0;
  [[nodiscard]] virtual LspReferencesCall References(
      const std::string& file_path, int line, int character,
      const std::string& symbol) = 0;
  [[nodiscard]] virtual LspGotoDefinitionCall GotoDefinition(
      const std::string& file_path, int line, int character,
      const std::string& symbol) = 0;
  [[nodiscard]] virtual LspRenameCall Rename(const std::string& file_path,
                                             int line, int character,
                                             const std::string& old_name,
                                             const std::string& new_name) = 0;
  [[nodiscard]] virtual LspSymbolsCall Symbols(
      const std::string& file_path) = 0;
};

class JsonRpcLspClient : public ILspClient {
 public:
  explicit JsonRpcLspClient(LspServerConfig config);
  ~JsonRpcLspClient() override;

  JsonRpcLspClient(const JsonRpcLspClient&) = delete;
  JsonRpcLspClient& operator=(const JsonRpcLspClient&) = delete;
  JsonRpcLspClient(JsonRpcLspClient&&) = delete;
  JsonRpcLspClient& operator=(JsonRpcLspClient&&) = delete;

  [[nodiscard]] LspDiagnosticsCall Diagnostics(
      const std::string& file_path) override;
  [[nodiscard]] LspReferencesCall References(
      const std::string& file_path, int line, int character,
      const std::string& symbol) override;
  [[nodiscard]] LspGotoDefinitionCall GotoDefinition(
      const std::string& file_path, int line, int character,
      const std::string& symbol) override;
  [[nodiscard]] LspRenameCall Rename(const std::string& file_path, int line,
                                     int character, const std::string& old_name,
                                     const std::string& new_name) override;
  [[nodiscard]] LspSymbolsCall Symbols(const std::string& file_path) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yac::tool_call
