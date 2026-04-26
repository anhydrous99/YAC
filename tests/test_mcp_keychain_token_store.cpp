#include "mcp/keychain_token_store.hpp"

#include <cstdlib>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

bool IsDbusAvailable() {
  return std::getenv("DBUS_SESSION_BUS_ADDRESS") != nullptr;
}

}  // namespace

TEST_CASE("round_trip") {
  if (!IsDbusAvailable() ||
      !yac::mcp::KeychainTokenStore::IsKeychainAvailable()) {
    SUCCEED("keychain unavailable in this env");
    return;
  }

  yac::mcp::KeychainTokenStore store;
  const std::string server_id = "yac-test-round-trip";
  const std::string token_json =
      R"({"access_token":"test-tok","token_type":"Bearer"})";

  store.Erase(server_id);

  store.Set(server_id, token_json);
  const auto result = store.Get(server_id);

  REQUIRE(result == std::optional<std::string>{token_json});

  store.Erase(server_id);
  REQUIRE_FALSE(store.Get(server_id).has_value());
}

TEST_CASE("detection_when_unavailable") {
  const bool available = yac::mcp::KeychainTokenStore::IsKeychainAvailable();

  if (!IsDbusAvailable()) {
    REQUIRE_FALSE(available);
  } else {
    SUCCEED("DBus present; availability check completed without throwing");
  }
}
