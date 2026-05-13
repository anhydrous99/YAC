#include "app/bootstrap.hpp"

#include "app/chat_event_bridge.hpp"
#include "app/mcp_command_handlers.hpp"
#include "app/model_discovery.hpp"
#include "app/prompt_slash_commands.hpp"
#include "app/streaming_coalescer.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/config_loader.hpp"
#include "chat/config_paths.hpp"
#include "chat/prompt_library.hpp"
#include "chat/settings_toml.hpp"
#include "cli/mcp_admin_command.hpp"
#include "mcp/mcp_manager.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/chat_ui_actions.hpp"
#include "presentation/file_mention_inliner.hpp"
#include "presentation/mcp/mcp_slash_commands.hpp"
#include "presentation/slash_command_registry.hpp"
#include "presentation/theme.hpp"
#include "presentation/util/terminal.hpp"
#include "provider/bedrock_aws_api_guard.hpp"
#include "provider/bedrock_chat_provider.hpp"
#include "provider/model_context_windows.hpp"
#include "provider/openai_compatible_chat_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/file_index.hpp"
#include "tool_call/workspace_filesystem.hpp"
#include "util/log.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/app.hpp>

namespace yac::app {
namespace {

std::shared_ptr<provider::OpenAiCompatibleChatProvider> BuildProvider(
    const chat::ChatConfig& config) {
  return std::make_shared<provider::OpenAiCompatibleChatProvider>(
      chat::ProviderConfig{
          .id = config.provider_id,
          .model = config.model,
          .api_key = config.api_key,
          .api_key_env = config.api_key_env,
          .base_url = config.base_url,
          .options = config.options,
          .context_window = config.context_window,
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

bool HasEnvValue(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0';
}

void ReportThemeSaveResult(presentation::ChatUI& chat_ui,
                           const std::string& theme_name,
                           const std::filesystem::path& settings_path,
                           bool saved,
                           const std::vector<chat::ConfigIssue>& issues) {
  if (!saved) {
    presentation::UiNotice notice{
        .severity = presentation::UiSeverity::Warning,
        .title = "Theme not saved",
        .detail = "Could not update " + settings_path.string() + "."};
    if (!issues.empty()) {
      notice.severity = SeverityFor(issues.front().severity);
      notice.detail = issues.front().message;
      if (!issues.front().detail.empty()) {
        notice.detail += ": " + issues.front().detail;
      }
    }
    chat_ui.AppendNotice(std::move(notice));
    return;
  }

  if (HasEnvValue("YAC_THEME_NAME")) {
    chat_ui.AppendNotice(
        {.severity = presentation::UiSeverity::Warning,
         .title = "Theme saved, env override active",
         .detail = "YAC_THEME_NAME is set, so restart will use that value "
                   "until it is unset."});
    return;
  }

  chat_ui.AppendNotice(
      {.severity = presentation::UiSeverity::Info,
       .title = "Theme saved",
       .detail = "Next launch will use '" + theme_name + "'."});
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
         "Mode\n"
         "  Shift+Tab     Toggle plan/build mode\n\n"
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
         startup.provider_id.value + " / " + startup.model.value +
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
  auto cmds = BuildCommands(result.models);
  auto mcp_cmds = presentation::BuildMcpPaletteCommands();
  cmds.insert(cmds.end(), mcp_cmds.begin(), mcp_cmds.end());
  chat_ui.SetCommands(std::move(cmds));
  chat_ui.SetModelCommands(BuildModelCommands(result.models));
  if (result.status == ModelDiscoveryStatus::Fallback) {
    chat_ui.AppendNotice(
        presentation::UiNotice{.severity = presentation::UiSeverity::Warning,
                               .title = "Using fallback model list",
                               .detail = result.message});
  } else if (result.status == ModelDiscoveryStatus::Failed &&
             !result.message.empty()) {
    chat_ui.AppendNotice(
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

void ConfigureServiceEventCallback(StreamingCoalescer& coalescer,
                                   chat::ChatService& chat_service) {
  chat_service.SetEventCallback([&coalescer](chat::ChatEvent event) {
    coalescer.Dispatch(std::move(event));
  });
}

class ChatActionsImpl : public presentation::IChatActions {
 public:
  ChatActionsImpl(
      chat::ChatService& chat_service, const chat::ChatConfig& config,
      std::shared_ptr<std::optional<presentation::terminal::BackgroundGuard>>
          terminal_bg_guard,
      ftxui::App& screen, std::shared_ptr<cli::McpAdminCommand> mcp_admin)
      : chat_service_(chat_service),
        config_(config),
        terminal_bg_guard_(std::move(terminal_bg_guard)),
        screen_(screen),
        mcp_admin_(std::move(mcp_admin)),
        workspace_fs_(config.workspace_root),
        file_index_(workspace_fs_) {}

  void SetChatUi(presentation::ChatUI& chat_ui) {
    chat_ui_ = &chat_ui;
    // Register the redraw hook *before* kicking off the warm so the very
    // first rebuild posts a Custom event and the composer paints the rows
    // as soon as they're available.
    file_index_.SetOnRebuildComplete([this] {
      screen_.Post([this] { screen_.PostEvent(ftxui::Event::Custom); });
    });
    file_index_.WarmAsync();
    chat_ui.SetFileMentionProvider(
        [this](std::string_view prefix) -> presentation::FileMentionResult {
          const auto state = file_index_.GetState();
          const bool indexing =
              state == tool_call::FileIndex::State::Warming ||
              state == tool_call::FileIndex::State::Cold;
          return {.rows = file_index_.Query(prefix, /*limit=*/50),
                  .is_indexing = indexing};
        });
  }

  void OnSend(const std::string& message) override {
    auto inlined = presentation::InlineFileMentions(message, workspace_fs_);
    chat_service_.SubmitUserMessage(std::move(inlined.text));
  }

  void OnCommand(const std::string& command) override {
    if (command == "new_chat" || command == "clear_messages") {
      chat_service_.ResetConversation();
    } else if (command == "cancel_response") {
      chat_service_.CancelActiveResponse();
    } else if (command == "help") {
      if (chat_ui_) {
        chat_ui_->ShowHelp();
      }
    } else if (command.starts_with(presentation::kSwitchModelPrefix)) {
      chat_service_.SetModel(::yac::ModelId{command.substr(
          std::string(presentation::kSwitchModelPrefix).size())});
    } else if (command.starts_with(presentation::kSwitchThemePrefix)) {
      ApplyThemeCommand(command);
    } else if (command == "mcp_list") {
      if (chat_ui_) {
        HandleMcpListCommand(*chat_ui_, mcp_admin_);
      }
    } else if (command == "mcp_add") {
      if (chat_ui_) {
        ShowMcpAddUsage(*chat_ui_);
      }
    }
  }

  void OnToolApproval(const ::yac::ApprovalId& approval_id,
                      bool approved) override {
    chat_service_.ResolveToolApproval(approval_id, approved);
  }

  void OnAskUserResponse(::yac::ApprovalId approval_id,
                         std::string response) override {
    chat_service_.ResolveAskUser(approval_id, std::move(response));
  }

  void OnAskUserCancel(::yac::ApprovalId approval_id) override {
    chat_service_.ResolveToolApproval(std::move(approval_id), false);
  }

  void OnModeToggle() override {
    screen_.Post([this] {
      auto current = chat_service_.GetAgentMode();
      auto next = (current == chat::AgentMode::Build) ? chat::AgentMode::Plan
                                                      : chat::AgentMode::Build;
      chat_service_.SetAgentMode(next);
    });
  }

 private:
  void ApplyThemeCommand(const std::string& command) {
    auto theme_name =
        command.substr(std::string(presentation::kSwitchThemePrefix).size());
    auto theme = presentation::theme::GetTheme(theme_name);
    if (config_.theme_density == "compact") {
      theme.density = presentation::theme::ThemeDensity::Compact;
    }
    presentation::theme::ReinitializeTheme(std::move(theme));
    if (config_.sync_terminal_background) {
      if (theme_name == "system") {
        (*terminal_bg_guard_).reset();
      } else {
        const auto rgb = presentation::theme::CurrentCanvasRgb();
        (*terminal_bg_guard_).reset();
        if (rgb.r != 0 || rgb.g != 0 || rgb.b != 0) {
          terminal_bg_guard_->emplace(rgb.r, rgb.g, rgb.b);
        }
      }
    }
    if (chat_ui_) {
      try {
        const auto settings_path = chat::GetSettingsPath();
        std::vector<chat::ConfigIssue> save_issues;
        const bool saved = chat::SaveThemeNameToSettingsToml(
            settings_path, theme_name, save_issues);
        ReportThemeSaveResult(*chat_ui_, theme_name, settings_path, saved,
                              save_issues);
      } catch (const std::exception& error) {
        chat_ui_->AppendNotice({.severity = presentation::UiSeverity::Warning,
                                .title = "Theme not saved",
                                .detail = error.what()});
      }
    }
    screen_.PostEvent(ftxui::Event::Custom);
  }

  chat::ChatService& chat_service_;
  const chat::ChatConfig& config_;
  std::shared_ptr<std::optional<presentation::terminal::BackgroundGuard>>
      terminal_bg_guard_;
  ftxui::App& screen_;
  std::shared_ptr<cli::McpAdminCommand> mcp_admin_;
  presentation::ChatUI* chat_ui_ = nullptr;
  tool_call::WorkspaceFilesystem workspace_fs_;
  tool_call::FileIndex file_index_;
};

void InstallChatUiCommandPalette(presentation::ChatUI& chat_ui,
                                 const std::vector<chat::ModelInfo>& models) {
  auto cmds = BuildCommands(models);
  auto mcp_cmds = presentation::BuildMcpPaletteCommands();
  cmds.insert(cmds.end(), mcp_cmds.begin(), mcp_cmds.end());
  chat_ui.SetCommands(std::move(cmds));
  chat_ui.SetModelCommands(BuildModelCommands(models));
  chat_ui.SetThemeCommands(BuildThemeCommands());
}

presentation::SlashCommandRegistry BuildSlashCommandRegistry(
    std::function<void()> exit_loop, chat::ChatService& chat_service,
    presentation::ChatUI& chat_ui,
    const std::vector<chat::PromptDefinition>& prompts,
    std::vector<chat::ConfigIssue>& startup_issues, ftxui::App& screen,
    std::shared_ptr<cli::McpAdminCommand> mcp_admin) {
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
      chat_ui.AppendNotice(presentation::UiNotice{
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
      // SAFETY: best-effort directory listing; missing/unreadable entries
      // don't belong in the prompt context, so the partial output is kept.
      yac::log::Warn("app.bootstrap", "/init repo-context listing failed: {}",
                     yac::log::DescribeCurrentException());
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
          chat_ui.AppendNotice(presentation::UiNotice{
              .severity = presentation::UiSeverity::Warning,
              .title = "Usage: /task <description>",
          });
          return;
        }
        auto& manager = chat_service.GetSubAgentManager();
        if (manager.IsAtCapacity()) {
          chat_ui.AppendNotice(presentation::UiNotice{
              .severity = presentation::UiSeverity::Warning,
              .title = "Max sub-agents reached",
              .detail = "Wait for existing sub-agents to complete.",
          });
          return;
        }
        static_cast<void>(
            chat_service.GetSubAgentManager().SpawnBackgroundFromUser(args));
      });
  RegisterPromptSlashCommands(
      slash_registry, prompts,
      [&chat_service](std::string prompt) {
        chat_service.SubmitUserMessage(std::move(prompt));
      },
      startup_issues);

  RegisterMcpSlashCommandHandlers(slash_registry, chat_ui, screen,
                                  std::move(mcp_admin));

  return slash_registry;
}

}  // namespace

int RunApp() {
  auto loaded = chat::LoadConfig();
  auto& config_result = loaded.chat;
  auto config = config_result.config;
  auto& prompt_result = loaded.prompt_library;
  auto startup_issues = prompt_result.issues;

  std::shared_ptr<provider::LanguageModelProvider> provider;
  if (config.provider_id.value == "bedrock") {
    provider::EnsureAwsApiGuardInstalled();
    provider =
        std::make_shared<provider::BedrockChatProvider>(chat::ProviderConfig{
            .id = config.provider_id,
            .model = config.model,
            .api_key = config.api_key,
            .api_key_env = config.api_key_env,
            .base_url = config.base_url,
            .options = config.options,
            .context_window = config.context_window,
        });
  } else {
    provider = BuildProvider(config);
  }

  {
    auto theme = presentation::theme::GetTheme(config.theme_name);
    if (theme.name != config.theme_name && !config.theme_name.empty()) {
      config_result.issues.push_back(
          {.severity = chat::ConfigIssueSeverity::Warning,
           .message = "Unknown theme: '" + config.theme_name + "'",
           .detail = "Falling back to default theme 'vivid'."});
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

  auto mcp_admin = std::make_shared<cli::McpAdminCommand>();

  auto mcp_mgr_event_sink =
      std::make_shared<std::function<void(chat::ChatEvent)>>();
  auto mcp_mgr = std::make_unique<mcp::McpManager>(
      config.mcp, [mcp_mgr_event_sink](chat::ChatEvent event) {
        if (*mcp_mgr_event_sink) {
          (*mcp_mgr_event_sink)(std::move(event));
        }
      });

  provider::ProviderRegistry registry;
  registry.Register(provider);
  chat::ChatService chat_service(std::move(registry), config,
                                 std::move(mcp_mgr));

  ChatActionsImpl chat_actions(chat_service, config, terminal_bg_guard, screen,
                               mcp_admin);

  presentation::ChatUI chat_ui(chat_actions);
  chat_actions.SetChatUi(chat_ui);

  ConfigureUiTaskRunner(screen, chat_ui);
  chat_ui.SetContextWindowTokens(
      provider::ResolveContextWindow(provider.get(), config.model.value));
  chat_ui.SetProviderModel(config.provider_id, config.model);

  ChatEventBridge bridge(chat_ui, /*history_provider=*/{},
                         [provider](const std::string& model_id) {
                           return provider::ResolveContextWindow(provider.get(),
                                                                 model_id);
                         });

  StreamingCoalescer event_coalescer(screen, bridge);
  *mcp_mgr_event_sink = [&event_coalescer](chat::ChatEvent event) {
    event_coalescer.Dispatch(std::move(event));
  };

  ConfigureServiceEventCallback(event_coalescer, chat_service);
  InstallChatUiCommandPalette(chat_ui, /*models=*/{});

  chat_ui.SetSlashCommands(BuildSlashCommandRegistry(
      screen.ExitLoopClosure(), chat_service, chat_ui, prompt_result.prompts,
      startup_issues, screen, mcp_admin));

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
