#include "app/bootstrap.hpp"

#include "app/chat_event_bridge.hpp"
#include "app/model_context_windows.hpp"
#include "app/model_discovery.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
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
    const chat::ChatConfigResult& config_result) {
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
  return "Shortcuts\n"
         "Enter sends. Shift+Enter, Ctrl+Enter, and Alt+Enter insert a "
         "newline.\n"
         "Ctrl+P opens commands. PageUp/PageDown scroll. Home/End jumps.\n\n"
         "Slash commands\n"
         "/help opens this panel. /clear starts fresh. /cancel stops the "
         "active response. /quit exits.\n\n"
         "Current session\n"
         "Provider/model: " +
         startup.provider_id + " / " + startup.model +
         "\nWorkspace: " + startup.workspace_root +
         "\nAPI key env: " + startup.api_key_env + " (" +
         (startup.api_key_configured ? "configured" : "missing") + ")" +
         "\nLSP: " + startup.lsp_command + " (" +
         (startup.lsp_available ? "found" : "not found") +
         ")\n\n"
         "Tool permissions\n"
         "Writes and renames ask before changing the workspace. Review the "
         "target and preview, then approve with Enter/Y or reject with N/Esc.";
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

void ConfigureChatUiCallbacks(const std::vector<chat::ModelInfo>& models,
                              chat::ChatService& chat_service,
                              presentation::ChatUI& chat_ui) {
  chat_ui.SetOnSend([&chat_service](const std::string& message) {
    chat_service.SubmitUserMessage(message);
  });

  chat_ui.SetOnCommand([&chat_service, &chat_ui](const std::string& command) {
    if (command == "new_chat" || command == "clear_messages") {
      chat_service.ResetConversation();
    } else if (command == "cancel_response") {
      chat_service.CancelActiveResponse();
    } else if (command == "help") {
      chat_ui.ShowHelp();
    } else if (command.starts_with(presentation::kSwitchModelPrefix)) {
      chat_service.SetModel(
          command.substr(std::string(presentation::kSwitchModelPrefix).size()));
    }
  });
  chat_ui.SetOnToolApproval(
      [&chat_service](const std::string& approval_id, bool approved) {
        chat_service.ResolveToolApproval(approval_id, approved);
      });

  chat_ui.SetCommands(BuildCommands(models));
  chat_ui.SetModelCommands(BuildModelCommands(models));
}

presentation::SlashCommandRegistry BuildSlashCommandRegistry(
    std::function<void()> exit_loop, chat::ChatService& chat_service,
    presentation::ChatUI& chat_ui) {
  presentation::SlashCommandRegistry slash_registry;
  presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", std::move(exit_loop));
  slash_registry.SetHandler(
      "clear", [&chat_service] { chat_service.ResetConversation(); });
  slash_registry.SetHandler(
      "cancel", [&chat_service] { chat_service.CancelActiveResponse(); });
  slash_registry.SetHandler("help", [&chat_ui] { chat_ui.ShowHelp(); });
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
        static_cast<void>(manager.SpawnBackground(args));
      });
  return slash_registry;
}

}  // namespace

int RunApp() {
  auto config_result = chat::LoadChatConfigResult();
  auto config = config_result.config;
  auto provider = BuildProvider(config);

  auto screen = ftxui::App::Fullscreen();
  presentation::ChatUI chat_ui;
  ConfigureUiTaskRunner(screen, chat_ui);
  auto startup_status = BuildStartupStatus(config_result);
  chat_ui.SetStartupStatus(startup_status);
  chat_ui.SetHelpText(BuildHelpText(startup_status));
  chat_ui.SetContextWindowTokens(LookupContextWindow(config.model));
  chat_ui.SetProviderModel(config.provider_id, config.model);

  ChatEventBridge bridge(chat_ui);

  provider::ProviderRegistry registry;
  registry.Register(provider);
  chat::ChatService chat_service(std::move(registry), config);

  ConfigureServiceEventCallback(screen, bridge, chat_service);
  ConfigureChatUiCallbacks({}, chat_service, chat_ui);

  chat_ui.SetSlashCommands(BuildSlashCommandRegistry(screen.ExitLoopClosure(),
                                                     chat_service, chat_ui));

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
