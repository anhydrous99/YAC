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

}  // namespace yac::provider
