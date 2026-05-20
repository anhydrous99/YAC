#pragma once

#include "cli/provider_auth_command.hpp"

#include <ostream>

namespace yac::cli {

struct ProviderAuthCliOptions {
  ProviderAuthCommand::Options command_options;
  std::ostream* out = nullptr;
  std::ostream* err = nullptr;
};

// argv[0] is the provider id; argc==0 prints usage.
// Returns 0 success, 1 user error (bad args), 2 system error.
[[nodiscard]] int RunProviderAuthCli(int argc, char** argv,
                                     ProviderAuthCliOptions opts = {});

}  // namespace yac::cli
