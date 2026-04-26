#include "keychain/keychain.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

bool IsDbusUnavailableEnv() {
  return std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr;
}

}  // namespace

TEST_CASE("keychain smoke handles missing credentials and unavailable dbus") {
  const std::string package = "com.example.yac.smoke";
  const std::string service = "missing-entry";
  const std::string user = "missing-user";

  keychain::Error error;
  const std::string password =
      keychain::getPassword(package, service, user, error);

  if (error && error.type == keychain::ErrorType::GenericError) {
    std::cout << "keychain unavailable in this env\n";
    return;
  }

  if (IsDbusUnavailableEnv()) {
    std::cout << "keychain unavailable in this env\n";
    return;
  }

  if (error) {
    REQUIRE(error.type == keychain::ErrorType::NotFound);
    return;
  }

  REQUIRE(password.empty());
}
