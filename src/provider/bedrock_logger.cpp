#include "provider/bedrock_logger.hpp"

#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/DefaultLogSystem.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>

namespace yac::provider {

namespace {

std::once_flag g_logger_initialized;

void InitializeBedrockLogger() {
  const char* home = std::getenv("HOME");
  if (!home || *home == '\0') {
    return;
  }

  std::filesystem::path log_dir = std::filesystem::path(home) / ".yac" / "logs";
  std::filesystem::create_directories(log_dir);

  std::filesystem::path log_file = log_dir / "bedrock";

  auto logger = std::make_shared<Aws::Utils::Logging::DefaultLogSystem>(
      Aws::Utils::Logging::LogLevel::Warn, log_file.string());
  Aws::Utils::Logging::InitializeAWSLogging(logger);
}

}  // namespace

void InstallBedrockFileLogger() {
  std::call_once(g_logger_initialized, InitializeBedrockLogger);
}

}  // namespace yac::provider
