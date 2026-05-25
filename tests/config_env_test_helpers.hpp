#pragma once

#include "../src/chat/settings_registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yac::testing {

inline std::vector<std::string> RegistryUserFacingEnvVars() {
  std::vector<std::string> env_vars;
  for (const auto& record : yac::chat::SettingsRegistryRecords()) {
    if (record.classification != yac::chat::SettingClassification::UserFacing ||
        record.env_var.empty()) {
      continue;
    }
    env_vars.emplace_back(record.env_var);
  }

  std::sort(env_vars.begin(), env_vars.end());
  env_vars.erase(std::unique(env_vars.begin(), env_vars.end()), env_vars.end());
  return env_vars;
}

class ScopedEnvClear {
 public:
  ScopedEnvClear() : ScopedEnvClear({}) {}

  ScopedEnvClear(std::initializer_list<std::string_view> extra_env_vars) {
    auto env_vars = RegistryUserFacingEnvVars();
    for (std::string_view extra_env_var : extra_env_vars) {
      env_vars.emplace_back(extra_env_var);
    }

    std::sort(env_vars.begin(), env_vars.end());
    env_vars.erase(std::unique(env_vars.begin(), env_vars.end()),
                   env_vars.end());

    for (const auto& env_var : env_vars) {
      if (const char* value = std::getenv(env_var.c_str())) {
        saved_.emplace_back(env_var, value);
        unsetenv(env_var.c_str());
      }
    }
  }

  ~ScopedEnvClear() {
    for (const auto& [name, value] : saved_) {
      setenv(name.c_str(), value.c_str(), 1);
    }
  }

  ScopedEnvClear(const ScopedEnvClear&) = delete;
  ScopedEnvClear& operator=(const ScopedEnvClear&) = delete;
  ScopedEnvClear(ScopedEnvClear&&) = delete;
  ScopedEnvClear& operator=(ScopedEnvClear&&) = delete;

 private:
  std::vector<std::pair<std::string, std::string>> saved_;
};

}  // namespace yac::testing
