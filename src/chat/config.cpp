#include "chat/config.hpp"

#include <cstdlib>
#include <string>

namespace yac::chat {

namespace {

std::optional<std::string> GetEnv(const char* name) {
  if (const char* value = std::getenv(name)) {
    return std::string(value);
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

}  // namespace

ChatConfig LoadChatConfigFromEnv() {
  ChatConfig config;

  if (auto val = GetEnv("YAC_PROVIDER")) {
    config.provider_id = std::move(*val);
  }
  if (auto val = GetEnv("YAC_MODEL")) {
    config.model = std::move(*val);
  }
  if (auto val = GetEnv("YAC_BASE_URL")) {
    config.base_url = std::move(*val);
  }
  if (auto val = GetEnv("YAC_TEMPERATURE")) {
    config.temperature = ParseTemperature(*val);
  }
  if (auto val = GetEnv("YAC_SYSTEM_PROMPT")) {
    config.system_prompt = std::move(*val);
  }

  return config;
}

}  // namespace yac::chat
