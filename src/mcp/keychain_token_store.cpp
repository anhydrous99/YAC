#include "mcp/keychain_token_store.hpp"

#include <cctype>
#include <keychain/keychain.h>
#include <optional>
#include <string>
#include <string_view>

namespace yac::mcp {

namespace {

constexpr std::string_view kKeychainServiceId = "yac-mcp";
constexpr std::string_view kUserAccount = "token";

std::string SanitizeServerId(std::string_view server_id) {
  std::string result;
  result.reserve(server_id.size());
  for (char c : server_id) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
        c == '_') {
      result += c;
    } else {
      result += '_';
    }
  }
  return result;
}

}  // namespace

bool KeychainTokenStore::IsKeychainAvailable() {
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

std::optional<std::string> KeychainTokenStore::Get(
    std::string_view server_id) const {
  if (!IsKeychainAvailable()) {
    throw KeychainUnavailableError(
        "KeychainTokenStore::Get: keychain backend unavailable");
  }

  keychain::Error error;
  const std::string password = keychain::getPassword(
      std::string(kKeychainServiceId), SanitizeServerId(server_id),
      std::string(kUserAccount), error);

  if (error.type == keychain::ErrorType::NotFound) {
    return std::nullopt;
  }
  if (error) {
    throw KeychainUnavailableError("KeychainTokenStore::Get: keychain error: " +
                                   error.message);
  }
  return password;
}

void KeychainTokenStore::Set(std::string_view server_id,
                             std::string_view token_json) {
  if (!IsKeychainAvailable()) {
    throw KeychainUnavailableError(
        "KeychainTokenStore::Set: keychain backend unavailable");
  }

  keychain::Error error;
  keychain::setPassword(std::string(kKeychainServiceId),
                        SanitizeServerId(server_id), std::string(kUserAccount),
                        std::string(token_json), error);

  if (error) {
    throw KeychainUnavailableError("KeychainTokenStore::Set: keychain error: " +
                                   error.message);
  }
}

void KeychainTokenStore::Erase(std::string_view server_id) {
  if (!IsKeychainAvailable()) {
    throw KeychainUnavailableError(
        "KeychainTokenStore::Erase: keychain backend unavailable");
  }

  keychain::Error error;
  keychain::deletePassword(std::string(kKeychainServiceId),
                           SanitizeServerId(server_id),
                           std::string(kUserAccount), error);

  if (error && error.type != keychain::ErrorType::NotFound) {
    throw KeychainUnavailableError(
        "KeychainTokenStore::Erase: keychain error: " + error.message);
  }
}

}  // namespace yac::mcp
