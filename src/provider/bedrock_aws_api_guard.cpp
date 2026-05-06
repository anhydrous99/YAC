#include "provider/bedrock_aws_api_guard.hpp"

#include "provider/bedrock_logger.hpp"

#include <aws/core/Aws.h>

namespace yac::provider {

struct AwsApiGuard::Impl {
  Aws::SDKOptions options;
};

AwsApiGuard::AwsApiGuard() : impl_(std::make_unique<Impl>()) {
  InstallBedrockFileLogger();
  impl_->options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Off;
  Aws::InitAPI(impl_->options);
}

AwsApiGuard::~AwsApiGuard() {
  Aws::ShutdownAPI(impl_->options);
}

void EnsureAwsApiGuardInstalled() {
  // Meyers singleton: thread-safe initialization, lifetime extends to process
  // exit so it outlives any BedrockRuntimeClient created via the provider.
  static AwsApiGuard guard;
  (void)guard;
}

}  // namespace yac::provider
