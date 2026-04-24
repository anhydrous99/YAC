#include "app/bootstrap.hpp"

#include "app/chat_event_bridge.hpp"
#include "app/model_context_windows.hpp"
#include "app/model_discovery.hpp"
#include "app/prompt_slash_commands.hpp"
#include "chat/agent_mode.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/prompt_library.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "presentation/theme.hpp"
#include "presentation/util/terminal.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/app.hpp>

namespace yac::app {
namespace {

std::shared_ptr<provider::OpenAiChatProvider> BuildProvider(
    const chat::ChatConfig& config) {
  return std::make_shared<provider::OpenAiChatProvider>(chat::ProviderConfig{
      .id = config.provider_id,
      .model = config.model,
      .api_key = config.api_key,
      .api_key_env = config.api_key_env,
      .base_url = config.base_url,
  });
}

presentation::UiSeverity SeverityFor(chat::ConfigIssueSeverity severity) {
  switch (severity) {
    case chat::ConfigIssueSeverity::Info:
      return presentation::UiSeverity::Info;
    case chat::ConfigIssueSeverity::Warning:
      return presentation::UiSeverity::Warning;
    case chat::ConfigIssueSeverity::Error:
      return presentation::UiSeverity::Error;
  }
  return presentation::UiSeverity::Info;
}

bool IsExecutableAvailable(const std::string& command) {
  if (command.empty()) {
    return false;
  }
  std::filesystem::path command_path(command);
  if (command_path.has_parent_path()) {
    return std::filesystem::exists(command_path);
  }
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return false;
  }
#ifdef _WIN32
  constexpr char kSeparator = ';';
#else
  constexpr char kSeparator = ':';
#endif
  std::istringstream stream(path_env);
  std::string entry;
  while (std::getline(stream, entry, kSeparator)) {
    if (entry.empty()) {
      continue;
    }
    if (std::filesystem::exists(std::filesystem::path(entry) / command)) {
      return true;
    }
  }
  return false;
}

presentation::StartupStatus BuildStartupStatus(
    const chat::ChatConfigResult& config_result,
    const std::vector<chat::ConfigIssue>& extra_issues) {
  const auto& config = config_result.config;
  presentation::StartupStatus status{
      .provider_id = config.provider_id,
      .model = config.model,
      .workspace_root = config.workspace_root,
      .api_key_env = config.api_key_env,
      .api_key_configured = !config.api_key.empty(),
      .lsp_command = config.lsp_clangd_command,
      .lsp_available = IsExecutableAvailable(config.lsp_clangd_command),
  };
  for (const auto& issue : config_result.issues) {
    status.notices.push_back(presentation::UiNotice{
        .severity = SeverityFor(issue.severity),
        .title = issue.message,
        .detail = issue.detail,
    });
  }
  for (const auto& issue : extra_issues) {
    status.notices.push_back(presentation::UiNotice{
        .severity = SeverityFor(issue.severity),
        .title = issue.message,
        .detail = issue.detail,
    });
  }
  if (!status.lsp_command.empty() && !status.lsp_available) {
    status.notices.push_back(presentation::UiNotice{
        .severity = presentation::UiSeverity::Warning,
        .title = "Language server not found",
        .detail = "Set YAC_LSP_CLANGD_COMMAND if clangd is installed outside "
                  "PATH.",
    });
  }
  return status;
}

std::string BuildHelpText(const presentation::StartupStatus& startup) {
  return "Navigation\n"
         "  Ctrl+P        Command palette\n"
         "  /             Slash command menu\n"
         "  Esc           Close palette or menu\n"
         "  Up / Down     Move selection\n\n"
         "Composer\n"
         "  Enter         Send message\n"
         "  Shift+Enter   Insert newline\n"
         "  Ctrl+Enter    Insert newline\n"
         "  Alt+Enter     Insert newline\n\n"
         "Scroll\n"
         "  PageUp        Scroll transcript up\n"
         "  PageDown      Scroll transcript down\n"
         "  Home          Jump to top of history\n"
         "  End           Jump to bottom\n\n"
         "Slash commands\n"
         "  /help         This panel\n"
         "  /clear        Start fresh\n"
         "  /cancel       Stop active response\n"
         "  /task <desc>  Start background sub-agent\n"
         "  /init         Run init prompt\n"
         "  /review       Run review prompt\n"
         "  /quit         Exit\n\n"
         "Predefined prompts\n"
         "  Each ~/.yac/prompts/*.toml file becomes /<filename>.\n"
         "  Use description = \"...\" and prompt = \"\"\"...\"\"\".\n"
         "  Command arguments replace $ARGUMENTS.\n\n"
         "Current session\n"
         "  Provider/model: " +
         startup.provider_id + " / " + startup.model +
         "\n  Workspace: " + startup.workspace_root +
         "\n  API key env: " + startup.api_key_env + " (" +
         (startup.api_key_configured ? "configured" : "missing") + ")" +
         "\n  LSP: " + startup.lsp_command + " (" +
         (startup.lsp_available ? "found" : "not found") +
         ")\n\n"
         "Tool permissions\n"
         "  Writes and renames ask before changing the workspace.\n"
         "  Review the target and preview, then approve with Enter/Y\n"
         "  or reject with N/Esc.";
}

void ApplyModelDiscoveryResult(const ModelDiscoveryResult& result,
                               presentation::ChatUI& chat_ui) {
  chat_ui.SetCommands(BuildCommands(result.models));
  chat_ui.SetModelCommands(BuildModelCommands(result.models));
  if (result.status == ModelDiscoveryStatus::Fallback) {
    chat_ui.SetTransientStatus(
        presentation::UiNotice{.severity = presentation::UiSeverity::Warning,
                               .title = "Using fallback model list",
                               .detail = result.message});
  } else if (result.status == ModelDiscoveryStatus::Failed &&
             !result.message.empty()) {
    chat_ui.SetTransientStatus(
        presentation::UiNotice{.severity = presentation::UiSeverity::Warning,
                               .title = "Model discovery failed",
                               .detail = result.message});
  }
}

void ConfigureUiTaskRunner(ftxui::App& screen, presentation::ChatUI& chat_ui) {
  chat_ui.SetUiTaskRunner([&screen](presentation::ChatUI::UiTask task) {
    screen.Post([&screen, task = std::move(task)]() mutable {
      task();
      screen.PostEvent(ftxui::Event::Custom);
    });
  });
}

void ConfigureServiceEventCallback(ftxui::App& screen, ChatEventBridge& bridge,
                                   chat::ChatService& chat_service) {
  chat_service.SetEventCallback([&bridge, &screen](chat::ChatEvent event) {
    screen.Post([&bridge, &screen, event = std::move(event)] {
      bridge.HandleEvent(event);
      screen.PostEvent(ftxui::Event::Custom);
    });
  });
}

void ConfigureChatUiCallbacks(
    const std::vector<chat::ModelInfo>& models, const chat::ChatConfig& config,
    const std::shared_ptr<
        std::optional<presentation::terminal::BackgroundGuard>>&
        terminal_bg_guard,
    ftxui::App& screen, chat::ChatService& chat_service,
    presentation::ChatUI& chat_ui) {
  chat_ui.SetOnSend([&chat_service](const std::string& message) {
    chat_service.SubmitUserMessage(message);
  });

  chat_ui.SetOnCommand([&chat_service, &chat_ui, &config, terminal_bg_guard,
                        &screen](const std::string& command) {
    if (command == "new_chat" || command == "clear_messages") {
      chat_service.ResetConversation();
    } else if (command == "cancel_response") {
      chat_service.CancelActiveResponse();
    } else if (command == "help") {
      chat_ui.ShowHelp();
    } else if (command.starts_with(presentation::kSwitchModelPrefix)) {
      chat_service.SetModel(
          command.substr(std::string(presentation::kSwitchModelPrefix).size()));
    } else if (command.starts_with(presentation::kSwitchThemePrefix)) {
      auto theme_name =
          command.substr(std::string(presentation::kSwitchThemePrefix).size());
      auto theme = presentation::theme::GetTheme(theme_name);
      if (config.theme_density == "compact") {
        theme.density = presentation::theme::ThemeDensity::Compact;
      }
      presentation::theme::ReinitializeTheme(std::move(theme));
      if (config.sync_terminal_background) {
        if (theme_name == "system") {
          terminal_bg_guard->reset();
        } else {
          const auto rgb = presentation::theme::CurrentCanvasRgb();
          terminal_bg_guard->reset();
          if (rgb.r != 0 || rgb.g != 0 || rgb.b != 0) {
            terminal_bg_guard->emplace(rgb.r, rgb.g, rgb.b);
          }
        }
      }
      screen.PostEvent(ftxui::Event::Custom);
    }
  });
  chat_ui.SetOnToolApproval(
      [&chat_service](const std::string& approval_id, bool approved) {
        chat_service.ResolveToolApproval(approval_id, approved);
      });

  chat_ui.SetOnAskUserCallbacks(
      [&chat_service](std::string approval_id, std::string response) {
        chat_service.ResolveAskUser(std::move(approval_id),
                                    std::move(response));
      },
      [&chat_service](std::string approval_id) {
        chat_service.ResolveToolApproval(std::move(approval_id), false);
      });

  chat_ui.SetOnModeToggle([&chat_service, &screen] {
    screen.Post([&chat_service] {
      auto current = chat_service.GetAgentMode();
      auto next = (current == chat::AgentMode::Build) ? chat::AgentMode::Plan
                                                      : chat::AgentMode::Build;
      chat_service.SetAgentMode(next);
    });
  });

  chat_ui.SetCommands(BuildCommands(models));
  chat_ui.SetModelCommands(BuildModelCommands(models));
  chat_ui.SetThemeCommands(BuildThemeCommands());
}

presentation::SlashCommandRegistry BuildSlashCommandRegistry(
    std::function<void()> exit_loop, chat::ChatService& chat_service,
    presentation::ChatUI& chat_ui,
    const std::vector<chat::PromptDefinition>& prompts,
    std::vector<chat::ConfigIssue>& startup_issues) {
  presentation::SlashCommandRegistry slash_registry;
  presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", std::move(exit_loop));
  slash_registry.SetHandler(
      "clear", [&chat_service] { chat_service.ResetConversation(); });
  slash_registry.SetHandler(
      "cancel", [&chat_service] { chat_service.CancelActiveResponse(); });
  slash_registry.SetHandler("help", [&chat_ui] { chat_ui.ShowHelp(); });
  slash_registry.SetHandler("compact", [&chat_service, &chat_ui] {
    if (chat_service.IsBusy()) {
      chat_ui.SetTransientStatus(presentation::UiNotice{
          .severity = presentation::UiSeverity::Warning,
          .title = "Cannot compact while a response is active",
      });
      return;
    }
    chat_service.CompactConversation();
  });
  slash_registry.SetArgumentsHandler("init", [&chat_service](std::string args) {
    std::string context = "=== Repo Context ===\n";
    try {
      const auto root = std::filesystem::current_path();
      context += "Workspace: " + root.string() + "\nTop-level entries:\n";
      for (const auto& entry : std::filesystem::directory_iterator(root)) {
        context += "  " + entry.path().filename().string();
        if (entry.is_directory()) {
          context += "/";
        }
        context += "\n";
      }
    } catch (...) {
    }
    context += "\n";
    const std::string prompt =
        context +
        "Create or update AGENTS.md for this repository with build commands, "
        "architecture overview, and coding conventions.\n" +
        (args.empty() ? "" : "\nFocus: " + args);
    chat_service.SubmitUserMessage(prompt);
  });
  slash_registry.Define("task", "task",
                        "Spawn a background sub-agent with a task");
  slash_registry.SetArgumentsHandler(
      "task", [&chat_service, &chat_ui](std::string args) {
        if (args.empty()) {
          chat_ui.SetTransientStatus(presentation::UiNotice{
              .severity = presentation::UiSeverity::Warning,
              .title = "Usage: /task <description>",
          });
          return;
        }
        auto& manager = chat_service.GetSubAgentManager();
        if (manager.IsAtCapacity()) {
          chat_ui.SetTransientStatus(presentation::UiNotice{
              .severity = presentation::UiSeverity::Warning,
              .title = "Max sub-agents reached",
              .detail = "Wait for existing sub-agents to complete.",
          });
          return;
        }
        static_cast<void>(chat_service.SpawnBackgroundSubAgent(args));
      });
  RegisterPromptSlashCommands(
      slash_registry, prompts,
      [&chat_service](std::string prompt) {
        chat_service.SubmitUserMessage(std::move(prompt));
      },
      startup_issues);
  return slash_registry;
}

}  // namespace

int RunApp() {
  auto config_result = chat::LoadChatConfigResult();
  auto config = config_result.config;
  auto prompt_result = chat::LoadPromptLibrary(/*seed_defaults=*/true);
  auto startup_issues = prompt_result.issues;
  auto provider = BuildProvider(config);

  {
    auto theme = presentation::theme::GetTheme(config.theme_name);
    if (theme.name != config.theme_name && !config.theme_name.empty()) {
      config_result.issues.push_back(
          {.severity = chat::ConfigIssueSeverity::Warning,
           .message = "Unknown theme: '" + config.theme_name + "'",
           .detail = "Falling back to default theme 'opencode'."});
    }
    if (config.theme_density == "compact") {
      theme.density = presentation::theme::ThemeDensity::Compact;
    }
    presentation::theme::InitializeTheme(theme);
  }

  auto screen = ftxui::App::Fullscreen();

  auto terminal_bg_guard = std::make_shared<
      std::optional<presentation::terminal::BackgroundGuard>>();
  if (config.sync_terminal_background && config.theme_name != "system") {
    const auto rgb = presentation::theme::CurrentCanvasRgb();
    if (rgb.r != 0 || rgb.g != 0 || rgb.b != 0) {
      terminal_bg_guard->emplace(rgb.r, rgb.g, rgb.b);
    }
  }

  presentation::ChatUI chat_ui;
  ConfigureUiTaskRunner(screen, chat_ui);
  chat_ui.SetContextWindowTokens(LookupContextWindow(config.model));
  chat_ui.SetProviderModel(config.provider_id, config.model);

  ChatEventBridge bridge(chat_ui);

  provider::ProviderRegistry registry;
  registry.Register(provider);
  chat::ChatService chat_service(std::move(registry), config);

  ConfigureServiceEventCallback(screen, bridge, chat_service);
  ConfigureChatUiCallbacks({}, config, terminal_bg_guard, screen, chat_service,
                           chat_ui);

  chat_ui.SetSlashCommands(
      BuildSlashCommandRegistry(screen.ExitLoopClosure(), chat_service, chat_ui,
                                prompt_result.prompts, startup_issues));

  auto startup_status = BuildStartupStatus(config_result, startup_issues);
  chat_ui.SetStartupStatus(startup_status);
  chat_ui.SetHelpText(BuildHelpText(startup_status));

  std::jthread model_discovery_worker(
      [provider, config, &screen, &chat_ui](std::stop_token stop_token) {
        auto result = DiscoverModelsWithStatus(*provider, config);
        if (stop_token.stop_requested()) {
          return;
        }
        screen.Post([&screen, &chat_ui, result = std::move(result)]() mutable {
          ApplyModelDiscoveryResult(result, chat_ui);
          screen.PostEvent(ftxui::Event::Custom);
        });
      });

  auto component = chat_ui.Build();
  screen.Loop(component);
  return 0;
}

}  // namespace yac::app
