#include "chat/config.hpp"

#include "chat/env_file.hpp"

#include <cstdlib>
#include <string>

namespace yac::chat {

namespace {

std::unordered_map<std::string, std::string> LoadEnvFile() {
  return EnvFile::FindAndParse();
}

std::optional<std::string> GetEnv(
    const std::unordered_map<std::string, std::string>& env_file,
    const char* name) {
  if (const char* value = std::getenv(name)) {
    return std::string(value);
  }
  auto it = env_file.find(name);
  if (it != env_file.end()) {
    return it->second;
  }
  return std::nullopt;
}

double ParseTemperature(const std::string& value) {
  constexpr double kMinTemp = 0.0;
  constexpr double kMaxTemp = 2.0;
  const double temp = std::stod(value);
  if (temp < kMinTemp || temp > kMaxTemp) {
    throw std::out_of_range("YAC_TEMPERATURE must be between 0.0 and 2.0");
  }
  return temp;
}

struct ProviderPreset {
  std::string model;
  std::string base_url;
  std::string api_key_env;
};

const std::unordered_map<std::string, ProviderPreset>& ProviderPresets() {
  static const std::unordered_map<std::string, ProviderPreset> kPresets = {
      {"zai",
       {.model = "glm-5.1",
        .base_url = "https://api.z.ai/api/coding/paas/v4",
        .api_key_env = "ZAI_API_KEY"}},
  };
  return kPresets;
}

void ApplyProviderDefaults(ChatConfig& config) {
  const auto& presets = ProviderPresets();
  auto it = presets.find(config.provider_id);
  if (it != presets.end()) {
    config.model = it->second.model;
    config.base_url = it->second.base_url;
    config.api_key_env = it->second.api_key_env;
  }
}

}  // namespace

ChatConfig LoadChatConfigFromEnv() {
  const auto env_file = LoadEnvFile();
  ChatConfig config;

  if (auto val = GetEnv(env_file, "YAC_PROVIDER")) {
    config.provider_id = std::move(*val);
  }
  ApplyProviderDefaults(config);
  if (auto val = GetEnv(env_file, "YAC_MODEL")) {
    config.model = std::move(*val);
  }
  if (auto val = GetEnv(env_file, "YAC_BASE_URL")) {
    config.base_url = std::move(*val);
  }
  if (auto val = GetEnv(env_file, "YAC_TEMPERATURE")) {
    config.temperature = ParseTemperature(*val);
  }
  if (auto val = GetEnv(env_file, "YAC_API_KEY_ENV")) {
    config.api_key_env = std::move(*val);
  }
  if (auto val = GetEnv(env_file, config.api_key_env.c_str())) {
    config.api_key = std::move(*val);
  }
  if (auto val = GetEnv(env_file, "YAC_SYSTEM_PROMPT")) {
    config.system_prompt = std::move(*val);
  }

  return config;
}

}  // namespace yac::chat
