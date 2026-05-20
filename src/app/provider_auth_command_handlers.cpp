#include "app/provider_auth_command_handlers.hpp"

#include "cli/provider_auth_command.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/provider_auth_slash_commands.hpp"
#include "presentation/slash_command_registry.hpp"
#include "util/log.hpp"

#include <exception>
#include <memory>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

namespace yac::app {
namespace {

constexpr std::string_view kSetApiKeyGuidance =
    "Use OPENAI_API_KEY or run: yac auth openai set-api-key --stdin";

std::pair<std::string, std::string> ParseLeadingToken(
    const std::string& input) {
  const auto sep = input.find(' ');
  if (sep == std::string::npos) {
    return {input, {}};
  }
  const auto rest_start = input.find_first_not_of(' ', sep);
  return {input.substr(0, sep), rest_start == std::string::npos
                                    ? std::string{}
                                    : input.substr(rest_start)};
}

std::string FormatProviderAuthStatus(
    const cli::ProviderAuthStatusSummary& summary) {
  std::ostringstream out;
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
  return out.str();
}

void ShowProviderAuthCommandError(presentation::ChatUI& chat_ui,
                                  std::string title,
                                  const std::exception& error) {
  chat_ui.AppendNotice(presentation::UiNotice{
      .severity = presentation::UiSeverity::Error,
      .title = std::move(title),
      .detail = error.what(),
  });
}

void ShowProviderAuthWarnings(presentation::ChatUI& chat_ui,
                              const cli::ProviderAuthStatusSummary& summary) {
  for (const auto& warning : summary.warnings) {
    chat_ui.AppendNotice(presentation::UiNotice{
        .severity = presentation::UiSeverity::Warning,
        .title = warning,
    });
  }
}

void PostProviderAuthFallbackNotice(ftxui::App& screen,
                                    presentation::ChatUI& chat_ui,
                                    std::string authorization_url) {
  screen.Post([&chat_ui, &screen,
               authorization_url = std::move(authorization_url)]() mutable {
    try {
      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Warning,
          .title = "browser launch failed; open this URL manually",
          .detail = std::move(authorization_url),
      });
      screen.PostEvent(ftxui::Event::Custom);
    } catch (...) {
      yac::log::Error("app.provider_auth_command_handlers",
                      "auth-fallback UI post failed: {}",
                      yac::log::DescribeCurrentException());
    }
  });
}

}  // namespace

void RegisterProviderAuthSlashCommandHandlers(
    presentation::SlashCommandRegistry& slash_registry,
    presentation::ChatUI& chat_ui, ftxui::App& screen,
    std::shared_ptr<cli::ProviderAuthCommand> provider_auth_command) {
  presentation::RegisterProviderAuthSlashCommands(slash_registry);

  struct ProviderAuthRunner {
    std::jthread thread;
  };
  auto auth_runner = std::make_shared<ProviderAuthRunner>();

  slash_registry.SetArgumentsHandler("auth", [&chat_ui, &screen,
                                              provider_auth_command,
                                              auth_runner](std::string args) {
    const auto [provider_name, rest] = ParseLeadingToken(args);
    if (provider_name.empty()) {
      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Usage: /auth openai <subcommand>",
          .detail = "login | status | logout | set-api-key",
      });
      return;
    }

    if (provider_name != "openai") {
      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Warning,
          .title = "Unknown /auth provider: " + provider_name,
          .detail = "Available: openai",
      });
      return;
    }

    const auto [subcmd, subargs] = ParseLeadingToken(rest);
    if (subcmd.empty()) {
      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Usage: /auth openai <subcommand>",
          .detail = "login | status | logout | set-api-key",
      });
      return;
    }

    if (subcmd == "login") {
      if (!subargs.empty()) {
        chat_ui.AppendNotice(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /auth openai login",
        });
        return;
      }

      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Starting OpenAI auth...",
      });
      auth_runner->thread =
          std::jthread([provider_auth_command, &chat_ui,
                        &screen](std::stop_token /*stop_token*/) noexcept {
            try {
              static_cast<void>(provider_auth_command->LoginOpenAi(
                  [&screen, &chat_ui](
                      const provider::OpenAiAuthorizationNotice& notice) {
                    if (!notice.browser_launched &&
                        !notice.authorization_url.empty()) {
                      PostProviderAuthFallbackNotice(screen, chat_ui,
                                                     notice.authorization_url);
                    }
                  }));
              screen.Post([&chat_ui, &screen]() noexcept {
                try {
                  chat_ui.AppendNotice(presentation::UiNotice{
                      .severity = presentation::UiSeverity::Info,
                      .title = "OpenAI auth complete",
                  });
                  screen.PostEvent(ftxui::Event::Custom);
                } catch (...) {
                  yac::log::Error("app.provider_auth_command_handlers",
                                  "auth-success UI post failed: {}",
                                  yac::log::DescribeCurrentException());
                }
              });
            } catch (const std::exception& error) {
              std::string err = error.what();
              screen.Post(
                  [&chat_ui, &screen, err = std::move(err)]() mutable noexcept {
                    try {
                      chat_ui.AppendNotice(presentation::UiNotice{
                          .severity = presentation::UiSeverity::Error,
                          .title = "OpenAI auth failed",
                          .detail = err,
                      });
                      screen.PostEvent(ftxui::Event::Custom);
                    } catch (...) {
                      yac::log::Error("app.provider_auth_command_handlers",
                                      "auth-failure UI post failed: {}",
                                      yac::log::DescribeCurrentException());
                    }
                  });
            } catch (...) {
              yac::log::Error("app.provider_auth_command_handlers",
                              "unhandled exception in provider auth thread: {}",
                              yac::log::DescribeCurrentException());
            }
          });
      return;
    }

    if (subcmd == "status") {
      if (!subargs.empty()) {
        chat_ui.AppendNotice(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /auth openai status",
        });
        return;
      }
      try {
        chat_ui.AddMessage(
            presentation::Sender::Agent,
            FormatProviderAuthStatus(provider_auth_command->GetOpenAiStatus()));
      } catch (const std::exception& error) {
        ShowProviderAuthCommandError(chat_ui, "auth status failed", error);
      }
      return;
    }

    if (subcmd == "logout") {
      if (!subargs.empty()) {
        chat_ui.AppendNotice(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /auth openai logout",
        });
        return;
      }
      try {
        const auto summary = provider_auth_command->LogoutOpenAi();
        chat_ui.AppendNotice(presentation::UiNotice{
            .severity = presentation::UiSeverity::Info,
            .title = "Logged out: openai",
        });
        ShowProviderAuthWarnings(chat_ui, summary);
      } catch (const std::exception& error) {
        ShowProviderAuthCommandError(chat_ui, "auth logout failed", error);
      }
      return;
    }

    if (subcmd == "set-api-key") {
      chat_ui.AppendNotice(presentation::UiNotice{
          .severity = presentation::UiSeverity::Warning,
          .title = std::string(kSetApiKeyGuidance),
      });
      return;
    }

    chat_ui.AppendNotice(presentation::UiNotice{
        .severity = presentation::UiSeverity::Warning,
        .title = "Unknown /auth openai subcommand: " + subcmd,
        .detail = "Available: login, status, logout, set-api-key",
    });
  });
}

}  // namespace yac::app
