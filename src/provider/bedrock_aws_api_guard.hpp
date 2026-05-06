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

// Idempotent process-wide installer. The first call constructs a function-
// local static AwsApiGuard (Aws::InitAPI runs on the calling thread); the
// guard lives until process exit, at which point Aws::ShutdownAPI runs.
// Subsequent calls are no-ops. Safe to call from any TU that may need
// Bedrock — call this before constructing BedrockChatProvider.
void EnsureAwsApiGuardInstalled();

}  // namespace yac::provider
