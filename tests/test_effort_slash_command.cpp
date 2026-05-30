#include "app/bootstrap.hpp"
#include "chat/chat_service.hpp"
#include "chat/config_paths.hpp"
#include "chat/settings_toml.hpp"
#include "chat/types.hpp"
#include "lambda_mock_provider.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/provider_registry.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using yac::chat::ChatConfig;
using yac::chat::ChatEvent;
using yac::chat::ChatEventType;
using yac::chat::ChatRequest;
using yac::chat::ChatService;
using yac::chat::ReasoningEffort;
using yac::presentation::ChatUI;
using yac::presentation::SlashCommand;
using yac::presentation::SlashCommandRegistry;
using yac::presentation::StartupStatus;
using yac::presentation::UiSeverity;
using yac::testing::LambdaMockProvider;

namespace {

class ScopedHome {
 public:
  explicit ScopedHome(std::string_view suffix)
      : root_(std::filesystem::temp_directory_path() /
              ("yac_effort_slash_" + std::string(suffix) + "_" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    if (const char* home = std::getenv("HOME")) {
      had_home_ = true;
      old_home_ = home;
    }
    std::filesystem::create_directories(root_);
    setenv("HOME", root_.string().c_str(), 1);
  }

  ScopedHome(const ScopedHome&) = delete;
  ScopedHome& operator=(const ScopedHome&) = delete;
  ScopedHome(ScopedHome&&) = delete;
  ScopedHome& operator=(ScopedHome&&) = delete;

  ~ScopedHome() {
    if (had_home_) {
      setenv("HOME", old_home_.c_str(), 1);
    } else {
      unsetenv("HOME");
    }
    std::filesystem::remove_all(root_);
  }

  [[nodiscard]] std::filesystem::path SettingsPath() const {
    return yac::chat::GetSettingsPath(root_);
  }

 private:
  std::filesystem::path root_;
  bool had_home_ = false;
  std::string old_home_;
};

ChatConfig MakeConfig(std::string provider_id, std::string model) {
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{std::move(provider_id)};
  config.model = ::yac::ModelId{std::move(model)};
  return config;
}

StartupStatus MakeStartupStatus(const ChatConfig& config) {
  return StartupStatus{.provider_id = config.provider_id,
                       .model = config.model,
                       .workspace_root = "/workspace",
                       .api_key_env = "OPENAI_API_KEY",
                       .api_key_configured = true,
                       .lsp_command = "clangd",
                       .lsp_available = true};
}

std::shared_ptr<LambdaMockProvider> MakeProvider(std::string provider_id) {
  return std::make_shared<LambdaMockProvider>(
      std::move(provider_id),
      [](const ChatRequest&, auto sink, std::stop_token) {
        sink(ChatEvent{yac::chat::TextDeltaEvent{.text = "ok"}});
      });
}

ChatService MakeService(
    ChatConfig config, std::shared_ptr<LambdaMockProvider> provider = nullptr) {
  yac::provider::ProviderRegistry registry;
  if (provider != nullptr) {
    registry.Register(
        std::static_pointer_cast<yac::provider::LanguageModelProvider>(
            std::move(provider)));
  }
  return ChatService(std::move(registry), std::move(config));
}

SlashCommandRegistry MakeEffortRegistry(ChatService& service, ChatUI& ui) {
  SlashCommandRegistry registry;
  yac::app::RegisterEffortSlashCommandHandlers(registry, service, ui);
  return registry;
}

const SlashCommand& FindCommand(const SlashCommandRegistry& registry,
                                std::string_view name) {
  const auto& commands = registry.Commands();
  const auto iter = std::ranges::find_if(
      commands,
      [name](const SlashCommand& command) { return command.name == name; });
  REQUIRE(iter != commands.end());
  return *iter;
}

bool IsVisibleInMenu(const SlashCommandRegistry& registry,
                     std::string_view name) {
  const auto& command = FindCommand(registry, name);
  return !command.visible_in_menu.has_value() ||
         command.visible_in_menu.value()();
}

void WaitForFinished(ChatService& service, std::string message) {
  std::mutex mutex;
  std::condition_variable condition;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      condition.notify_one();
    }
  });
  service.SubmitUserMessage(std::move(message));
  std::unique_lock lock(mutex);
  REQUIRE(condition.wait_for(lock, std::chrono::seconds(5),
                             [&] { return finished; }));
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string content;
  std::string line;
  while (std::getline(input, line)) {
    content += line;
    content += '\n';
  }
  return content;
}

std::string RenderComponent(const ftxui::Component& component, int width = 100,
                            int height = 30) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

bool HasMatchingEffortSetting(const std::filesystem::path& settings_path,
                              ReasoningEffort effort) {
  ChatConfig loaded;
  std::vector<yac::chat::ConfigIssue> issues;
  yac::chat::LoadSettingsFromToml(settings_path, loaded, issues);
  REQUIRE(issues.empty());
  return std::ranges::any_of(
      loaded.model_settings,
      [effort](const yac::chat::ProviderModelSettings& setting) {
        return setting.provider_id.value == "openai" &&
               setting.model.value == "gpt-5.5" && setting.effort == effort;
      });
}

}  // namespace

TEST_CASE("/effort visibility follows live active provider and model") {
  auto service = MakeService(MakeConfig("openai", "gpt-5.5"));
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  const auto& command = FindCommand(registry, "effort");
  REQUIRE(command.description == "Set reasoning effort for this model");
  REQUIRE(IsVisibleInMenu(registry, "effort"));

  service.SetModel(::yac::ModelId{"gpt-4o"});

  REQUIRE_FALSE(IsVisibleInMenu(registry, "effort"));
}

TEST_CASE("/effort reports current value and allowed values") {
  auto service = MakeService(MakeConfig("openai", "gpt-5-pro"));
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  REQUIRE(registry.TryDispatch("/effort"));

  REQUIRE(service.History().empty());
  REQUIRE(ui.GetNotices().size() == 1);
  const auto& notice = ui.GetNotices().back().notice;
  REQUIRE(notice.severity == UiSeverity::Info);
  REQUIRE(notice.title ==
          "Effort for openai/gpt-5-pro: unset. Allowed: high. Use /effort "
          "<value> or /effort unset.");
}

TEST_CASE("/help includes effort line for supported model with unset value") {
  const auto config = MakeConfig("openai", "gpt-5-pro");

  const auto help = yac::app::BuildHelpText(MakeStartupStatus(config), config);

  REQUIRE(help.find("  /effort       Effort: unset. Allowed: high\n") !=
          std::string::npos);
}

TEST_CASE("/help includes current effort and full allowed values") {
  auto config = MakeConfig("openai", "gpt-5.5");
  config.model_settings.push_back(yac::chat::ProviderModelSettings{
      .provider_id = ::yac::ProviderId{"openai"},
      .model = ::yac::ModelId{"gpt-5.5"},
      .effort = ReasoningEffort::Medium});

  const auto help = yac::app::BuildHelpText(MakeStartupStatus(config), config);

  REQUIRE(help.find("  /effort       Effort: medium. Allowed: none, minimal, "
                    "low, medium, high, xhigh\n") != std::string::npos);
}

TEST_CASE("/help omits effort line for unsupported model") {
  const auto config = MakeConfig("zai", "gpt-5.5");

  const auto help = yac::app::BuildHelpText(MakeStartupStatus(config), config);

  REQUIRE(help.find("/effort") == std::string::npos);
}

TEST_CASE("/effort manual dispatch warns when unsupported") {
  ScopedHome home("unsupported");
  auto service = MakeService(MakeConfig("zai", "gpt-5.5"));
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  REQUIRE_FALSE(IsVisibleInMenu(registry, "effort"));
  REQUIRE(registry.TryDispatch("/effort high"));

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.ConfiguredReasoningEffort().has_value());
  REQUIRE_FALSE(std::filesystem::exists(home.SettingsPath()));
  REQUIRE(ui.GetNotices().size() == 1);
  const auto& notice = ui.GetNotices().back().notice;
  REQUIRE(notice.severity == UiSeverity::Warning);
  REQUIRE(notice.title == "Effort is not supported by zai/gpt-5.5.");
}

TEST_CASE("/effort rejects values outside model capability") {
  ScopedHome home("bad_value");
  auto service = MakeService(MakeConfig("openai", "gpt-5-pro"));
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  REQUIRE(registry.TryDispatch("/effort low"));

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.ConfiguredReasoningEffort().has_value());
  REQUIRE_FALSE(std::filesystem::exists(home.SettingsPath()));
  REQUIRE(ui.GetNotices().size() == 1);
  const auto& notice = ui.GetNotices().back().notice;
  REQUIRE(notice.severity == UiSeverity::Warning);
  REQUIRE(notice.title ==
          "Effort 'low' is not supported by gpt-5-pro. Allowed: high.");
}

TEST_CASE("/effort saves scoped setting and affects future requests") {
  ScopedHome home("save");
  auto provider = MakeProvider("openai");
  auto service = MakeService(MakeConfig("openai", "gpt-5.5"), provider);
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  REQUIRE(registry.TryDispatch("/effort medium"));

  REQUIRE(service.History().empty());
  REQUIRE(service.ConfiguredReasoningEffort() == ReasoningEffort::Medium);
  REQUIRE(
      HasMatchingEffortSetting(home.SettingsPath(), ReasoningEffort::Medium));
  REQUIRE(ui.GetNotices().back().notice.severity == UiSeverity::Info);
  REQUIRE(ui.GetNotices().back().notice.title == "Effort saved");

  auto output = RenderComponent(ui.Build());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("effort: medium"));

  WaitForFinished(service, "hello");

  const auto requests = provider->Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].reasoning_effort == ReasoningEffort::Medium);
}

TEST_CASE("/effort unset removes scoped setting and clears runtime effort") {
  ScopedHome home("unset");
  auto service = MakeService(MakeConfig("openai", "gpt-5.5"));
  ChatUI ui;
  auto registry = MakeEffortRegistry(service, ui);

  REQUIRE(registry.TryDispatch("/effort high"));
  REQUIRE(service.ConfiguredReasoningEffort() == ReasoningEffort::High);
  REQUIRE(registry.TryDispatch("/effort unset"));

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.ConfiguredReasoningEffort().has_value());
  REQUIRE(ReadTextFile(home.SettingsPath()).find("effort = \"high\"") ==
          std::string::npos);
  ChatConfig loaded;
  std::vector<yac::chat::ConfigIssue> issues;
  yac::chat::LoadSettingsFromToml(home.SettingsPath(), loaded, issues);
  REQUIRE(issues.empty());
  const auto setting = std::ranges::find_if(
      loaded.model_settings, [](const yac::chat::ProviderModelSettings& item) {
        return item.provider_id.value == "openai" &&
               item.model.value == "gpt-5.5";
      });
  REQUIRE(setting != loaded.model_settings.end());
  REQUIRE_FALSE(setting->effort.has_value());
  REQUIRE(ui.GetNotices().back().notice.title == "Effort saved");
  REQUIRE(ui.GetNotices().back().notice.detail ==
          "Effort for openai/gpt-5.5: unset.");

  auto output = RenderComponent(ui.Build());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("effort: unset"));
}
