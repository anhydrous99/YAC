#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("sqlite dependency opens and closes memory database") {
  sqlite3* database = nullptr;

  REQUIRE(sqlite3_open(":memory:", &database) == SQLITE_OK);
  REQUIRE(database != nullptr);
  REQUIRE(sqlite3_close(database) == SQLITE_OK);
}
