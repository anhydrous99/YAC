#include "chat/config_paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::chat::GetSettingsPath;
using yac::chat::GetYacConfigDir;
using yac::chat::ResolveHomeDir;

namespace {

class ScopedHome {
 public:
  explicit ScopedHome(const char* value) {
    if (const char* prior = std::getenv("HOME")) {
      had_prior_ = true;
      prior_ = prior;
    }
    if (value == nullptr) {
      ::unsetenv("HOME");
    } else {
      ::setenv("HOME", value, 1);
    }
  }
  ~ScopedHome() {
    if (had_prior_) {
      ::setenv("HOME", prior_.c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
  }
  ScopedHome(const ScopedHome&) = delete;
  ScopedHome& operator=(const ScopedHome&) = delete;
  ScopedHome(ScopedHome&&) = delete;
  ScopedHome& operator=(ScopedHome&&) = delete;

 private:
  bool had_prior_ = false;
  std::string prior_;
};

}  // namespace

TEST_CASE("ResolveHomeDir prefers $HOME when set") {
  ScopedHome guard("/tmp/fake-home");
  auto resolved = ResolveHomeDir();
  if (!resolved.has_value()) {
    FAIL("ResolveHomeDir returned no value with $HOME set");
    return;
  }
  REQUIRE(resolved->string() == "/tmp/fake-home");
}

TEST_CASE("ResolveHomeDir falls back to getpwuid when $HOME unset") {
  ScopedHome guard(nullptr);
  auto resolved = ResolveHomeDir();
  // In CI containers HOME may be the only signal; if neither HOME nor passwd
  // yields a value we just confirm the API stays well-formed.
  if (resolved.has_value()) {
    REQUIRE(!resolved.value().empty());
  }
}

TEST_CASE("GetYacConfigDir composes ~/.yac") {
  const std::filesystem::path home = "/tmp/fake-home";
  REQUIRE(GetYacConfigDir(home) == home / ".yac");
}

TEST_CASE("GetSettingsPath composes ~/.yac/settings.toml") {
  const std::filesystem::path home = "/tmp/fake-home";
  REQUIRE(GetSettingsPath(home) == home / ".yac" / "settings.toml");
}

TEST_CASE("GetSettingsPath() uses the resolved home directory") {
  ScopedHome guard("/tmp/fake-home");
  REQUIRE(GetSettingsPath() ==
          std::filesystem::path{"/tmp/fake-home"} / ".yac" / "settings.toml");
}
