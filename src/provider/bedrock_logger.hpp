#pragma once

namespace yac::provider {

// Installs an Aws::Utils::Logging::FormattedLogSystem writing to
// ~/.yac/logs/bedrock.log. MUST be called BEFORE Aws::InitAPI(). Idempotent.
void InstallBedrockFileLogger();

}  // namespace yac::provider
