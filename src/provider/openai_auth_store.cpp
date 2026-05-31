#include "provider/openai_auth_store.hpp"

#include "chat/config_paths.hpp"
#include "chat/sqlite_state_store.hpp"
#include "chat/state_store.hpp"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <keychain/keychain.h>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yac::provider {
namespace {

constexpr std::string_view kKeychainServiceId = "yac-provider-openai";
constexpr std::string_view kKeychainEntryId = "openai";
constexpr const char* kAuthStoreEnv = "YAC_OPENAI_AUTH_STORE";
constexpr const char* kKeychainDisabledEnv = "YAC_KEYCHAIN_DISABLED";
constexpr std::string_view kProviderId = "openai";
constexpr std::string_view kLegacyImportDisabledKey =
    "openai_auth_legacy_import_disabled";

[[nodiscard]] bool UseFileOnlyAuthStore() {
  if (std::getenv(kKeychainDisabledEnv) != nullptr) {
    return true;
  }
  const char* value = std::getenv(kAuthStoreEnv);
  if (value == nullptr) {
    return false;
  }

  const std::string_view storage(value);
  return storage == "file" || storage == "file-only";
}

class DisabledOpenAiKeychainAuthBackend final : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAI auth keychain disabled by YAC_OPENAI_AUTH_STORE");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAI auth keychain disabled by YAC_OPENAI_AUTH_STORE");
  }

  void Erase() override {
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAI auth keychain disabled by YAC_OPENAI_AUTH_STORE");
  }
};

[[nodiscard]] std::shared_ptr<IOpenAiAuthBackend> MakeDefaultKeychainBackend() {
  if (UseFileOnlyAuthStore()) {
    return std::make_shared<DisabledOpenAiKeychainAuthBackend>();
  }
  return std::make_shared<OpenAiKeychainAuthBackend>();
}

[[nodiscard]] std::string TimestampNow() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

}  // namespace

bool OpenAiKeychainAuthBackend::IsKeychainAvailable() {
  static const bool available = []() -> bool {
    const std::string pkg(kKeychainServiceId);
    const std::string svc("__probe__");
    const std::string usr("__probe__");
    const std::string val("yac-probe-1");

    keychain::Error err;
    keychain::setPassword(pkg, svc, usr, val, err);
    if (err) {
      return false;
    }

    const std::string got = keychain::getPassword(pkg, svc, usr, err);
    keychain::deletePassword(pkg, svc, usr, err);
    return got == val;
  }();
  return available;
}

std::optional<std::string> OpenAiKeychainAuthBackend::Get() const {
  keychain::Error error;
  // Avoid the availability probe on normal reads because it may prompt.
  const std::string password = keychain::getPassword(
      std::string(kKeychainServiceId), std::string(kKeychainEntryId),
      std::string(kKeychainEntryId), error);
  if (error.type == keychain::ErrorType::NotFound) {
    return std::nullopt;
  }
  if (error) {
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAiKeychainAuthBackend::Get: keychain error: " + error.message);
  }
  return password;
}

void OpenAiKeychainAuthBackend::Set(std::string_view auth_json) {
  keychain::Error error;
  keychain::setPassword(
      std::string(kKeychainServiceId), std::string(kKeychainEntryId),
      std::string(kKeychainEntryId), std::string(auth_json), error);
  if (error) {
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAiKeychainAuthBackend::Set: keychain error: " + error.message);
  }
}

void OpenAiKeychainAuthBackend::Erase() {
  keychain::Error error;
  keychain::deletePassword(std::string(kKeychainServiceId),
                           std::string(kKeychainEntryId),
                           std::string(kKeychainEntryId), error);
  if (error && error.type != keychain::ErrorType::NotFound) {
    throw OpenAiAuthKeychainUnavailableError(
        "OpenAiKeychainAuthBackend::Erase: keychain error: " + error.message);
  }
}

OpenAiFileAuthBackend::OpenAiFileAuthBackend() {
  const auto home = yac::chat::ResolveHomeDir();
  if (!home) {
    throw std::runtime_error(
        "OpenAiFileAuthBackend: cannot resolve home directory");
  }
  file_path_ =
      yac::chat::GetYacConfigDir(*home) / "provider" / "auth" / "openai.json";
}

OpenAiFileAuthBackend::OpenAiFileAuthBackend(std::filesystem::path file_path)
    : file_path_(std::move(file_path)) {}

void OpenAiFileAuthBackend::EnsureParentDir() const {
  const auto parent = file_path_.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    throw std::runtime_error("OpenAiFileAuthBackend: cannot create directory " +
                             parent.string() + ": " + ec.message());
  }

#ifndef _WIN32
  std::filesystem::permissions(parent, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    throw std::runtime_error(
        "OpenAiFileAuthBackend: cannot set permissions on " + parent.string() +
        ": " + ec.message());
  }
#endif
}

std::optional<std::string> OpenAiFileAuthBackend::Get() const {
  std::error_code ec;
  if (!std::filesystem::exists(file_path_, ec) || ec) {
    return std::nullopt;
  }

#ifndef _WIN32
  const auto file_status = std::filesystem::status(file_path_, ec);
  if (ec) {
    throw std::runtime_error("OpenAiFileAuthBackend: cannot stat " +
                             file_path_.string() + ": " + ec.message());
  }
  constexpr auto kExpected =
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
  const auto file_perms =
      file_status.permissions() & std::filesystem::perms::all;
  if (file_perms != kExpected) {
    throw std::runtime_error(
        "OpenAiFileAuthBackend: refusing to read " + file_path_.string() +
        " — file permissions are too open (expected 0600). Fix with: chmod "
        "0600 " +
        file_path_.string());
  }
#endif

  std::ifstream file(file_path_);
  if (!file) {
    throw std::runtime_error("OpenAiFileAuthBackend: cannot open " +
                             file_path_.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void OpenAiFileAuthBackend::Set(std::string_view auth_json) {
  EnsureParentDir();
  const auto tmp_path = std::filesystem::path(file_path_.string() + ".tmp");

#ifndef _WIN32
  const char* const tmp_cstr = tmp_path.c_str();
  const int fd = ::open(tmp_cstr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0) {
    throw std::runtime_error("OpenAiFileAuthBackend: cannot open tmp file " +
                             tmp_path.string() + ": " + std::strerror(errno));
  }

  if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
    const std::string error(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error("OpenAiFileAuthBackend: fchmod failed on " +
                             tmp_path.string() + ": " + error);
  }

  std::size_t written = 0;
  while (written < auth_json.size()) {
    const auto bytes =
        ::write(fd, auth_json.data() + written, auth_json.size() - written);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      const std::string error(std::strerror(errno));
      ::close(fd);
      throw std::runtime_error("OpenAiFileAuthBackend: write failed on " +
                               tmp_path.string() + ": " + error);
    }
    written += static_cast<std::size_t>(bytes);
  }

  if (::fsync(fd) != 0) {
    const std::string error(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error("OpenAiFileAuthBackend: fsync failed on " +
                             tmp_path.string() + ": " + error);
  }

  if (::close(fd) != 0) {
    throw std::runtime_error("OpenAiFileAuthBackend: close failed on " +
                             tmp_path.string() + ": " + std::strerror(errno));
  }
#else
  {
    std::ofstream out(tmp_path, std::ios::trunc);
    if (!out) {
      throw std::runtime_error("OpenAiFileAuthBackend: cannot open tmp file " +
                               tmp_path.string());
    }
    out << auth_json;
    out.close();
    if (!out) {
      throw std::runtime_error("OpenAiFileAuthBackend: write failed on " +
                               tmp_path.string());
    }
  }
#endif

  std::error_code ec;
  std::filesystem::rename(tmp_path, file_path_, ec);
  if (ec) {
    std::filesystem::remove(tmp_path);
    throw std::runtime_error("OpenAiFileAuthBackend: rename failed: " +
                             ec.message());
  }
}

void OpenAiFileAuthBackend::Erase() {
  std::error_code ec;
  std::filesystem::remove(file_path_, ec);
}

OpenAiAuthStore::OpenAiAuthStore()
    : OpenAiAuthStore(BuildDefaultDependencies()) {}

OpenAiAuthStore::OpenAiAuthStore(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

OpenAiAuthStore::Dependencies OpenAiAuthStore::BuildDefaultDependencies() {
  return Dependencies{
      .keychain_backend = MakeDefaultKeychainBackend(),
      .file_backend = std::make_shared<OpenAiFileAuthBackend>(),
      .state_store = std::make_shared<chat::SQLiteStateStore>()};
}

void OpenAiAuthStore::StoreCache(std::optional<StoredOpenAiAuth> auth) const {
  cached_auth_ = std::move(auth);
}

std::optional<StoredOpenAiAuth> OpenAiAuthStore::LoadStateStoreAuth() const {
  if (dependencies_.state_store == nullptr) {
    return std::nullopt;
  }
  const auto credential = dependencies_.state_store->LoadProviderCredential(
      ProviderId{std::string(kProviderId)}, std::nullopt,
      chat::StateCredentialType::OpenAiAuth);
  if (!credential.has_value()) {
    return std::nullopt;
  }
  try {
    return StoredOpenAiAuth{.auth = ParseOpenAiAuth(credential->secret_json),
                            .source = OpenAiAuthStorageSource::StateStore};
  } catch (const std::exception& error) {
    (void)error;
    throw std::runtime_error(
        "OpenAiAuthStore: SQLite auth credential is invalid. Run `yac auth "
        "openai login` or `yac auth openai set-api-key --stdin` to refresh "
        "it.");
  }
}

StoredOpenAiAuth OpenAiAuthStore::SaveStateStoreAuth(
    const OpenAiAuth& auth, chat::StateCredentialSource source) const {
  const std::string auth_json = SerializeOpenAiAuth(auth);
  const std::string timestamp = TimestampNow();
  try {
    dependencies_.state_store->SaveProviderCredential(chat::ProviderCredential{
        .provider_id = ProviderId{std::string(kProviderId)},
        .credential_type = chat::StateCredentialType::OpenAiAuth,
        .secret_json = auth_json,
        .source = source,
        .created_at = timestamp,
        .updated_at = timestamp,
    });
  } catch (const std::exception& error) {
    throw std::runtime_error("OpenAiAuthStore: SQLite auth write failed: " +
                             std::string(error.what()));
  }
  const auto readback = dependencies_.state_store->LoadProviderCredential(
      ProviderId{std::string(kProviderId)}, std::nullopt,
      chat::StateCredentialType::OpenAiAuth);
  if (!readback.has_value() || readback->secret_json != auth_json) {
    throw std::runtime_error(
        "OpenAiAuthStore: SQLite auth write verification failed");
  }
  static_cast<void>(ParseOpenAiAuth(readback->secret_json));
  dependencies_.state_store->DeleteAppState(kLegacyImportDisabledKey);
  return StoredOpenAiAuth{.auth = auth,
                          .source = OpenAiAuthStorageSource::StateStore};
}

bool OpenAiAuthStore::IsLegacyImportDisabled() const {
  return dependencies_.state_store != nullptr &&
         dependencies_.state_store->LoadAppState(kLegacyImportDisabledKey)
             .has_value();
}

void OpenAiAuthStore::DisableLegacyImport() const {
  if (dependencies_.state_store == nullptr) {
    return;
  }
  dependencies_.state_store->SaveAppState(chat::AppStateEntry{
      .key = std::string(kLegacyImportDisabledKey),
      .value = "1",
      .updated_at = TimestampNow(),
  });
}

std::optional<StoredOpenAiAuth> OpenAiAuthStore::LoadLegacyAuth() const {
  try {
    if (const auto auth_json = dependencies_.keychain_backend->Get();
        auth_json.has_value()) {
      StoredOpenAiAuth stored{.auth = ParseOpenAiAuth(*auth_json),
                              .source = OpenAiAuthStorageSource::Keychain};
      if (dependencies_.state_store != nullptr) {
        try {
          return SaveStateStoreAuth(
              stored.auth, chat::StateCredentialSource::MigrationKeychain);
        } catch (const std::exception&) {
          return stored;
        }
      }
      return stored;
    }
  } catch (const OpenAiAuthKeychainUnavailableError& error) {
    (void)error;
  }

  if (const auto auth_json = dependencies_.file_backend->Get();
      auth_json.has_value()) {
    StoredOpenAiAuth stored{.auth = ParseOpenAiAuth(*auth_json),
                            .source = OpenAiAuthStorageSource::File};
    if (dependencies_.state_store != nullptr) {
      try {
        return SaveStateStoreAuth(
            stored.auth, chat::StateCredentialSource::MigrationLegacyFile);
      } catch (const std::exception&) {
        return stored;
      }
    }
    return stored;
  }
  return std::nullopt;
}

std::optional<StoredOpenAiAuth> OpenAiAuthStore::Load() const {
  std::scoped_lock lock(cache_mutex_);
  // Hold the first read under lock and memoize present or absent auth.
  if (cached_auth_.has_value()) {
    return *cached_auth_;
  }

  if (const auto stored = LoadStateStoreAuth(); stored.has_value()) {
    StoreCache(stored);
    return stored;
  }
  if (!IsLegacyImportDisabled()) {
    if (const auto stored = LoadLegacyAuth(); stored.has_value()) {
      StoreCache(stored);
      return stored;
    }
  }
  StoreCache(std::nullopt);
  return std::nullopt;
}

OpenAiAuthStorageSource OpenAiAuthStore::Save(const OpenAiAuth& auth) const {
  std::scoped_lock lock(cache_mutex_);
  if (dependencies_.state_store != nullptr) {
    StoredOpenAiAuth stored =
        SaveStateStoreAuth(auth, chat::StateCredentialSource::AuthCli);
    StoreCache(stored);
    return stored.source;
  }

  const std::string auth_json = SerializeOpenAiAuth(auth);
  try {
    dependencies_.keychain_backend->Set(auth_json);
    StoreCache(StoredOpenAiAuth{.auth = auth,
                                .source = OpenAiAuthStorageSource::Keychain});
    return OpenAiAuthStorageSource::Keychain;
  } catch (const OpenAiAuthKeychainUnavailableError&) {
    dependencies_.file_backend->Set(auth_json);
    StoreCache(StoredOpenAiAuth{.auth = auth,
                                .source = OpenAiAuthStorageSource::File});
    return OpenAiAuthStorageSource::File;
  }
}

void OpenAiAuthStore::Erase() const {
  std::scoped_lock lock(cache_mutex_);
  if (dependencies_.state_store != nullptr) {
    dependencies_.state_store->DeleteProviderCredential(
        ProviderId{std::string(kProviderId)}, std::nullopt,
        chat::StateCredentialType::OpenAiAuth);
    DisableLegacyImport();
    StoreCache(std::nullopt);
    return;
  }
  try {
    dependencies_.keychain_backend->Erase();
  } catch (const OpenAiAuthKeychainUnavailableError& error) {
    (void)error;
  }
  dependencies_.file_backend->Erase();
  StoreCache(std::nullopt);
}

}  // namespace yac::provider
