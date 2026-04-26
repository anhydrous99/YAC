#include "mcp/file_token_store.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using Catch::Matchers::ContainsSubstring;
using yac::mcp::FileTokenStore;

namespace {

class TempDir {
 public:
  TempDir() {
#ifndef _WIN32
    std::string tmpl =
        (std::filesystem::temp_directory_path() / "yac_test_token_XXXXXX")
            .string();
    const char* result = ::mkdtemp(tmpl.data());
    if (result == nullptr) {
      throw std::runtime_error("TempDir: mkdtemp failed");
    }
    path_ = result;
#else
    path_ = std::filesystem::temp_directory_path() / "yac_test_mcp_token";
    std::filesystem::create_directories(path_);
#endif
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

constexpr const char* kServerId = "test-server";
constexpr const char* kTokenJson =
    R"({"access_token":"test-token","refresh_token":"test-refresh",)"
    R"("expires_at":9999999999,"token_type":"Bearer","scope":"openid"})";

}  // namespace

TEST_CASE("round_trip", "[mcp_file_token_store]") {
  TempDir tmp;
  FileTokenStore store(tmp.Path());

  store.Set(kServerId, kTokenJson);
  const auto result = store.Get(kServerId);

  REQUIRE(result == std::optional<std::string>{kTokenJson});

#ifndef _WIN32
  const auto file_path = tmp.Path() / "test-server.json";
  struct ::stat st {};
  REQUIRE(::stat(file_path.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0600);
#endif
}

TEST_CASE("rejects_bad_perms", "[mcp_file_token_store]") {
#ifndef _WIN32
  TempDir tmp;
  FileTokenStore store(tmp.Path());

  const auto file_path = tmp.Path() / "test-server.json";
  {
    std::ofstream out(file_path);
    out << kTokenJson;
  }
  ::chmod(file_path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  REQUIRE_THROWS_WITH(store.Get(kServerId), ContainsSubstring("permissions"));
#endif
}

TEST_CASE("missing_file_returns_nullopt", "[mcp_file_token_store]") {
  TempDir tmp;
  FileTokenStore store(tmp.Path());

  const auto result = store.Get("nonexistent-server");
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("erase_deletes_file", "[mcp_file_token_store]") {
  TempDir tmp;
  FileTokenStore store(tmp.Path());

  store.Set(kServerId, kTokenJson);
  REQUIRE(store.Get(kServerId).has_value());

  store.Erase(kServerId);
  REQUIRE_FALSE(store.Get(kServerId).has_value());

  const auto file_path = tmp.Path() / "test-server.json";
  REQUIRE_FALSE(std::filesystem::exists(file_path));
}

TEST_CASE("atomic_write", "[mcp_file_token_store]") {
  TempDir tmp;
  FileTokenStore store(tmp.Path());

  store.Set(kServerId, kTokenJson);

  const auto tmp_path = tmp.Path() / "test-server.json.tmp";
  REQUIRE_FALSE(std::filesystem::exists(tmp_path));

  const auto final_path = tmp.Path() / "test-server.json";
  REQUIRE(std::filesystem::exists(final_path));
}
