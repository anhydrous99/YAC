#pragma once

#include <memory>

namespace yac::provider {

// RAII guard for Aws::InitAPI / Aws::ShutdownAPI.
// Construct at process scope, only when provider.id == "bedrock".
// Must outlive all BedrockRuntimeClient instances.
class AwsApiGuard {
 public:
  AwsApiGuard();
  ~AwsApiGuard();
  AwsApiGuard(const AwsApiGuard&) = delete;
  AwsApiGuard& operator=(const AwsApiGuard&) = delete;
  AwsApiGuard(AwsApiGuard&&) = delete;
  AwsApiGuard& operator=(AwsApiGuard&&) = delete;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yac::provider
