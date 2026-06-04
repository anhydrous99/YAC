#pragma once

#include "provider/openai_auth.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yac::chat {
enum class StateCredentialSource;
class StateStore;
}  // namespace yac::chat

namespace yac::provider {

class OpenAiAuthKeychainUnavailableError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class IOpenAiAuthBackend {
 public:
  IOpenAiAuthBackend() = default;
  virtual ~IOpenAiAuthBackend() = default;
  IOpenAiAuthBackend(const IOpenAiAuthBackend&) = delete;
  IOpenAiAuthBackend& operator=(const IOpenAiAuthBackend&) = delete;
  IOpenAiAuthBackend(IOpenAiAuthBackend&&) = delete;
  IOpenAiAuthBackend& operator=(IOpenAiAuthBackend&&) = delete;

  [[nodiscard]] virtual std::optional<std::string> Get() const = 0;
  virtual void Set(std::string_view auth_json) = 0;
  virtual void Erase() = 0;
};

class OpenAiKeychainAuthBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] static bool IsKeychainAvailable();

  [[nodiscard]] std::optional<std::string> Get() const override;
  void Set(std::string_view auth_json) override;
  void Erase() override;
};

class OpenAiFileAuthBackend : public IOpenAiAuthBackend {
 public:
  explicit OpenAiFileAuthBackend(std::filesystem::path file_path);
  OpenAiFileAuthBackend();

  [[nodiscard]] std::optional<std::string> Get() const override;
  void Set(std::string_view auth_json) override;
  void Erase() override;

 private:
  void EnsureParentDir() const;

  std::filesystem::path file_path_;
};

class OpenAiAuthStore {
 public:
  struct Dependencies {
    std::shared_ptr<IOpenAiAuthBackend> keychain_backend;
    std::shared_ptr<IOpenAiAuthBackend> file_backend;
    std::shared_ptr<chat::StateStore> state_store;
  };

  OpenAiAuthStore();
  explicit OpenAiAuthStore(Dependencies dependencies);

  [[nodiscard]] std::optional<StoredOpenAiAuth> Load() const;
  [[nodiscard]] OpenAiAuthStorageSource Save(const OpenAiAuth& auth) const;
  void Erase() const;

 private:
  [[nodiscard]] static Dependencies BuildDefaultDependencies();
  [[nodiscard]] std::optional<StoredOpenAiAuth> LoadStateStoreAuth() const;
  [[nodiscard]] std::optional<StoredOpenAiAuth> LoadLegacyAuth() const;
  [[nodiscard]] StoredOpenAiAuth SaveStateStoreAuth(
      const OpenAiAuth& auth, chat::StateCredentialSource source) const;
  [[nodiscard]] bool IsLegacyImportDisabled() const;
  void DisableLegacyImport() const;
  void StoreCache(std::optional<StoredOpenAiAuth> auth) const;

  Dependencies dependencies_;
  mutable std::mutex cache_mutex_;
  mutable std::optional<std::optional<StoredOpenAiAuth>> cached_auth_;
};

}  // namespace yac::provider
