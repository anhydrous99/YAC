#include "app/mcp_command_handlers.hpp"

#include "cli/mcp_admin_command.hpp"
#include "core_types/mcp_manager_interface.hpp"
#include "mcp/oauth/flow.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/mcp/mcp_slash_commands.hpp"
#include "presentation/slash_command_registry.hpp"
#include "util/log.hpp"

#include <exception>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>

namespace yac::app {
namespace {

std::pair<std::string, std::string> ParseMcpSubcmd(const std::string& args) {
  const auto sep = args.find(' ');
  if (sep == std::string::npos) {
    return {args, {}};
  }
  const auto rest_start = args.find_first_not_of(' ', sep);
  return {args.substr(0, sep), rest_start == std::string::npos
                                   ? std::string{}
                                   : args.substr(rest_start)};
}

std::string FormatMcpServerList(
    const std::vector<core_types::McpServerStatus>& servers) {
  if (servers.empty()) {
    return "(no MCP servers configured)";
  }
  std::string out;
  for (const auto& server : servers) {
    out += server.id + "  [" + server.state + "]  " + server.transport + "\n";
  }
  return out;
}

void ShowMcpCommandError(presentation::ChatUI& chat_ui, std::string title,
                         const std::exception& error) {
  chat_ui.SetTransientStatus(presentation::UiNotice{
      .severity = presentation::UiSeverity::Error,
      .title = std::move(title),
      .detail = error.what(),
  });
}

}  // namespace

void HandleMcpListCommand(
    presentation::ChatUI& chat_ui,
    const std::shared_ptr<cli::McpAdminCommand>& mcp_admin) {
  try {
    const auto servers = mcp_admin->ListServers();
    chat_ui.AddMessage(presentation::Sender::Agent,
                       FormatMcpServerList(servers));
  } catch (const std::exception& error) {
    ShowMcpCommandError(chat_ui, "mcp list failed", error);
  }
}

void ShowMcpAddUsage(presentation::ChatUI& chat_ui) {
  chat_ui.SetTransientStatus(presentation::UiNotice{
      .severity = presentation::UiSeverity::Info,
      .title = "Usage: /mcp add <id> <transport> ...",
      .detail = "transport: stdio (+ command) or http (+ url)",
  });
}

void RegisterMcpSlashCommandHandlers(
    presentation::SlashCommandRegistry& slash_registry,
    presentation::ChatUI& chat_ui, ftxui::App& screen,
    std::shared_ptr<cli::McpAdminCommand> mcp_admin) {
  presentation::RegisterMcpSlashCommands(slash_registry);

  // Holds the auth jthread so it RAII-joins when the slash handler is
  // destroyed, eliminating the UAF risk of the former detached thread.
  struct McpAuthRunner {
    std::jthread thread;
  };
  auto auth_runner = std::make_shared<McpAuthRunner>();

  slash_registry.SetArgumentsHandler("mcp", [&chat_ui, &screen, mcp_admin,
                                             auth_runner](std::string args) {
    const auto [subcmd, rest] = ParseMcpSubcmd(args);

    if (subcmd.empty()) {
      chat_ui.SetTransientStatus(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Usage: /mcp <subcommand>",
          .detail = "add | list | auth | logout | debug | resources",
      });
      return;
    }

    if (subcmd == "list") {
      HandleMcpListCommand(chat_ui, mcp_admin);
      return;
    }

    if (subcmd == "logout") {
      if (rest.empty()) {
        chat_ui.SetTransientStatus(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /mcp logout <server-id>",
        });
        return;
      }
      try {
        mcp_admin->Logout(rest);
        chat_ui.SetTransientStatus(presentation::UiNotice{
            .severity = presentation::UiSeverity::Info,
            .title = "Logged out",
            .detail = rest,
        });
      } catch (const std::exception& error) {
        ShowMcpCommandError(chat_ui, "mcp logout failed", error);
      }
      return;
    }

    if (subcmd == "debug") {
      if (rest.empty()) {
        chat_ui.SetTransientStatus(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /mcp debug <server-id>",
        });
        return;
      }
      try {
        const auto result = mcp_admin->Debug(rest);
        const std::string text =
            "=== MCP Debug: " + result.server_id + " ===\n" + "\nStatus:\n" +
            result.status + "\nAuth:\n" + result.auth +
            "\nConnectivity: " + result.connectivity + "\nLog:\n" + result.log;
        chat_ui.AddMessage(presentation::Sender::Agent, text);
      } catch (const std::exception& error) {
        ShowMcpCommandError(chat_ui, "mcp debug failed", error);
      }
      return;
    }

    if (subcmd == "resources") {
      if (rest.empty()) {
        chat_ui.SetTransientStatus(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /mcp resources <server-id>",
        });
        return;
      }
      chat_ui.SetTransientStatus(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "MCP resources: " + rest,
          .detail = "Requires an active server connection.",
      });
      return;
    }

    if (subcmd == "add") {
      ShowMcpAddUsage(chat_ui);
      return;
    }

    if (subcmd == "auth") {
      if (rest.empty()) {
        chat_ui.SetTransientStatus(presentation::UiNotice{
            .severity = presentation::UiSeverity::Warning,
            .title = "Usage: /mcp auth <server-id>",
        });
        return;
      }
      const std::string server_id = rest;
      chat_ui.SetTransientStatus(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Starting MCP auth...",
          .detail = server_id,
      });
      auth_runner->thread = std::jthread(
          [mcp_admin, server_id,  // NOLINT(bugprone-exception-escape)
           &chat_ui, &screen](std::stop_token /*st*/) noexcept {
            try {
              mcp_admin->Authenticate(server_id,
                                      mcp::oauth::OAuthInteractionMode{});
              screen.Post([&chat_ui,
                           &screen,  // NOLINT(bugprone-exception-escape)
                           server_id]() noexcept {
                try {
                  chat_ui.SetTransientStatus(presentation::UiNotice{
                      .severity = presentation::UiSeverity::Info,
                      .title = "MCP auth complete",
                      .detail = server_id,
                  });
                  screen.PostEvent(ftxui::Event::Custom);
                } catch (...) {
                  // SAFETY: noexcept Post lambda; exception cannot
                  // propagate across the FTXUI task boundary.
                  yac::log::Error("app.mcp_command_handlers",
                                  "auth-success UI post failed: {}",
                                  yac::log::DescribeCurrentException());
                }
              });
            } catch (const std::exception& error) {
              std::string err = error.what();
              screen.Post([&chat_ui,
                           &screen,  // NOLINT(bugprone-exception-escape)
                           server_id, err]() mutable noexcept {
                try {
                  chat_ui.SetTransientStatus(presentation::UiNotice{
                      .severity = presentation::UiSeverity::Error,
                      .title = "MCP auth failed: " + server_id,
                      .detail = err,
                  });
                  screen.PostEvent(ftxui::Event::Custom);
                } catch (...) {
                  // SAFETY: noexcept Post lambda; exception cannot
                  // propagate across the FTXUI task boundary.
                  yac::log::Error("app.mcp_command_handlers",
                                  "auth-failure UI post failed: {}",
                                  yac::log::DescribeCurrentException());
                }
              });
            } catch (...) {
              // SAFETY: jthread body is noexcept; exception cannot
              // propagate across the thread boundary.
              yac::log::Error("app.mcp_command_handlers",
                              "unhandled exception in mcp auth thread: {}",
                              yac::log::DescribeCurrentException());
            }
          });
      return;
    }

    chat_ui.SetTransientStatus(presentation::UiNotice{
        .severity = presentation::UiSeverity::Warning,
        .title = "Unknown /mcp subcommand: " + subcmd,
        .detail = "Available: add, list, auth, logout, debug, resources",
    });
  });
}

}  // namespace yac::app
