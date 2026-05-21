#include <cstdlib>
#include <string>

#include "keychain.h"

namespace keychain {
namespace {

bool DbusUnavailable() {
  return std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr;
}

void ClearError(Error& err) {
  err.type = ErrorType::NoError;
  err.message.clear();
  err.code = 0;
}

}  // namespace

std::string getPassword(const std::string&, const std::string&,
                        const std::string&, Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return {};
  }

  err.type = ErrorType::NotFound;
  err.message.clear();
  err.code = 0;
  return {};
}

void setPassword(const std::string&, const std::string&, const std::string&,
                 const std::string&, Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return;
  }

  ClearError(err);
}

void deletePassword(const std::string&, const std::string&, const std::string&,
                    Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return;
  }

  ClearError(err);
}

}  // namespace keychain
