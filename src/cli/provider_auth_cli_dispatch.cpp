#include "cli/provider_auth_cli_dispatch.hpp"

#include <iostream>
#include <string_view>

namespace yac::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage: yac auth <provider> <subcommand> [args]\n"
    "\n"
    "Providers:\n"
    "  openai\n"
    "\n"
    "OpenAI subcommands:\n"
    "  login                 Browser OAuth using localhost:1455 callback\n"
    "  login --device        Device OAuth for headless shells\n"
    "  set-api-key --stdin   Read one API key line from stdin and store it\n"
    "  status                Show secret-safe auth status\n"
    "  logout                Clear stored OpenAI auth\n"
    "\n"
    "Exit codes: 0 success, 1 user error (bad args), 2 system error\n";

void PrintSummary(const ProviderAuthStatusSummary& summary, std::ostream& out) {
  out << "configured provider: " << summary.configured_provider << "\n";
  if (summary.stored_credential.has_value()) {
    out << "stored credential: " << *summary.stored_credential << "\n";
  }
  if (summary.effective_auth.has_value()) {
    out << "effective auth: " << *summary.effective_auth << "\n";
  }
  if (summary.oauth_expiry.has_value()) {
    out << "oauth expiry: " << *summary.oauth_expiry << "\n";
  }
  if (summary.account_id.has_value()) {
    out << "account id: " << *summary.account_id << "\n";
  }
  for (const auto& warning : summary.warnings) {
    out << "warning: " << warning << "\n";
  }
}

int DoOpenAi(int argc, char** argv, ProviderAuthCommand& cmd, std::ostream& out,
             std::ostream& err) {
  if (argc == 0) {
    err << "Error: provider 'openai' requires a subcommand\n";
    return 1;
  }

  const std::string_view subcmd(argv[0]);
  if (subcmd == "login") {
    const bool device = argc == 2 && std::string_view(argv[1]) == "--device";
    if (argc > 1 && !device) {
      const std::string_view arg(argv[1]);
      if (!arg.empty() && arg.front() == '-') {
        err << "Error: unknown flag: " << arg << "\n";
      } else {
        err << "Error: 'login' takes no arguments\n";
      }
      return 1;
    }
    try {
      if (device) {
        bool printed_device_notice = false;
        const OpenAiLoginResult result = cmd.LoginOpenAiDevice(
            [&out, &printed_device_notice](
                const provider::OpenAiDeviceAuthorizationNotice& notice) {
              out << "Open this URL: " << notice.verification_url << "\n"
                  << "Enter code: " << notice.user_code << "\n";
              printed_device_notice = true;
            });
        if (!printed_device_notice && result.verification_url.has_value() &&
            result.user_code.has_value()) {
          out << "Open this URL: " << *result.verification_url << "\n"
              << "Enter code: " << *result.user_code << "\n";
        }
        out << "Authenticated successfully.\n";
        return 0;
      }
      bool printed_fallback_url = false;
      const OpenAiLoginResult result = cmd.LoginOpenAi(
          [&out, &printed_fallback_url](
              const provider::OpenAiAuthorizationNotice& notice) {
            if (!notice.browser_launched && !notice.authorization_url.empty()) {
              out << "warning: browser launch failed; open this URL manually: "
                  << notice.authorization_url << "\n";
              printed_fallback_url = true;
            }
          });
      if (!printed_fallback_url && !result.browser_launched &&
          result.authorization_url.has_value()) {
        out << "warning: browser launch failed; open this URL manually: "
            << *result.authorization_url << "\n";
      }
      out << "Authenticated successfully.\n";
      return 0;
    } catch (const std::exception& e) {
      err << "Error: " << e.what() << "\n";
      return 2;
    }
  }

  if (subcmd == "set-api-key") {
    if (argc != 2 || std::string_view(argv[1]) != "--stdin") {
      err << "Error: usage: yac auth openai set-api-key --stdin\n";
      return 1;
    }
    try {
      static_cast<void>(cmd.SetOpenAiApiKeyFromStdin());
      out << "Stored OpenAI API key.\n";
      return 0;
    } catch (const std::exception& e) {
      err << "Error: " << e.what() << "\n";
      return 2;
    }
  }

  if (subcmd == "status") {
    if (argc != 1) {
      err << "Error: usage: yac auth openai status\n";
      return 1;
    }
    try {
      PrintSummary(cmd.GetOpenAiStatus(), out);
      return 0;
    } catch (const std::exception& e) {
      err << "Error: " << e.what() << "\n";
      return 2;
    }
  }

  if (subcmd == "logout") {
    if (argc != 1) {
      err << "Error: usage: yac auth openai logout\n";
      return 1;
    }
    try {
      const ProviderAuthStatusSummary summary = cmd.LogoutOpenAi();
      out << "Logged out: openai\n";
      for (const auto& warning : summary.warnings) {
        out << "warning: " << warning << "\n";
      }
      return 0;
    } catch (const std::exception& e) {
      err << "Error: " << e.what() << "\n";
      return 2;
    }
  }

  err << "Error: unknown subcommand: " << subcmd
      << "\n  Run 'yac auth' for usage.\n";
  return 1;
}

}  // namespace

int RunProviderAuthCli(int argc, char** argv, ProviderAuthCliOptions opts) {
  std::ostream& out = opts.out != nullptr ? *opts.out : std::cout;
  std::ostream& err = opts.err != nullptr ? *opts.err : std::cerr;

  if (argc == 0) {
    out << kUsage;
    return 0;
  }

  const std::string_view provider(argv[0]);
  if (provider != "openai") {
    err << "Error: unknown provider: " << provider
        << "\n  Run 'yac auth' for usage.\n";
    return 1;
  }

  ProviderAuthCommand cmd(std::move(opts.command_options));
  return DoOpenAi(argc - 1, argv + 1, cmd, out, err);
}

}  // namespace yac::cli
