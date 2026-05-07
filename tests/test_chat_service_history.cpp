#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "core_types/typed_ids.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;

namespace {

class ScopedEnvClear {
 public:
  ScopedEnvClear() {
    static constexpr std::array kVars = {"YAC_PROVIDER",
                                         "YAC_MODEL",
                                         "YAC_BASE_URL",
                                         "YAC_TEMPERATURE",
                                         "YAC_MAX_TOOL_ROUNDS",
                                         "YAC_API_KEY_ENV",
                                         "YAC_SYSTEM_PROMPT",
                                         "YAC_WORKSPACE_ROOT",
                                         "OPENAI_API_KEY",
                                         "ZAI_API_KEY",
                                         "YAC_CUSTOM_ZAI_KEY",
                                         "YAC_TEST_API_KEY_FROM_FILE",
                                         "YAC_TEST_API_KEY_OVERRIDE"};
    for (const auto* name : kVars) {
      if (const char* val = std::getenv(name)) {
        saved_.emplace_back(name, val);
        unsetenv(name);
      }
    }
  }
  ~ScopedEnvClear() {
    for (const auto& [name, val] : saved_) {
      setenv(name.c_str(), val.c_str(), 1);
    }
  }
  ScopedEnvClear(const ScopedEnvClear&) = delete;
  ScopedEnvClear& operator=(const ScopedEnvClear&) = delete;
  ScopedEnvClear(ScopedEnvClear&&) = delete;
  ScopedEnvClear& operator=(ScopedEnvClear&&) = delete;

 private:
  std::vector<std::pair<std::string, std::string>> saved_;
};

std::shared_ptr<LambdaMockProvider> MakeFakeProvider(
    ::yac::ModelId expected_model = ::yac::ModelId{"fake-model"}) {
  return std::make_shared<LambdaMockProvider>(
      "fake", [expected_model = std::move(expected_model)](
                  const ChatRequest& request, ChatEventSink sink,
                  std::stop_token stop_token) {
        REQUIRE(request.model == expected_model);
        REQUIRE(request.stream);
        REQUIRE(!request.messages.empty());
        REQUIRE(request.messages.back().role == ChatRole::User);
        if (stop_token.stop_requested()) {
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "hi"}});
        sink(ChatEvent{TextDeltaEvent{.text = " there"}});
      });
}

std::vector<ChatEvent> CollectEvents(ChatService& service,
                                     const std::string& message) {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<ChatEvent> events;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    events.push_back(std::move(event));
    if (events.back().Type() == ChatEventType::Finished) {
      finished = true;
      condition.notify_one();
    }
  });

  service.SubmitUserMessage(message);

  std::unique_lock lock(mutex);
  condition.wait(lock, [&] { return finished; });
  return events;
}

ChatService MakeService(
    std::shared_ptr<LanguageModelProvider> provider = nullptr,
    ChatConfig config = {}) {
  ProviderRegistry registry;
  if (provider) {
    if (config.provider_id.value == "openai-compatible") {
      config.provider_id = ::yac::ProviderId{provider->Id()};
    }
    registry.Register(std::move(provider));
  } else {
    registry.Register(MakeFakeProvider());
    config.provider_id = ::yac::ProviderId{"fake"};
    config.model = ::yac::ModelId{"fake-model"};
  }
  return ChatService(std::move(registry), config);
}

bool HasEvent(const std::vector<ChatEvent>& events, ChatEventType type) {
  return std::ranges::any_of(
      events, [type](const auto& e) { return e.Type() == type; });
}

const ChatEvent& FindEvent(const std::vector<ChatEvent>& events,
                           ChatEventType type) {
  auto it = std::ranges::find_if(
      events, [type](const auto& e) { return e.Type() == type; });
  REQUIRE(it != events.end());
  return *it;
}

class ScopedSettingsFile {
 public:
  explicit ScopedSettingsFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
  }
  ~ScopedSettingsFile() { std::filesystem::remove_all(path_); }
  ScopedSettingsFile(const ScopedSettingsFile&) = delete;
  ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
  ScopedSettingsFile(ScopedSettingsFile&&) = delete;
  ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

  void Write(std::string_view content) const {
    std::ofstream stream(path_, std::ios::trunc);
    stream << content;
  }

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::shared_ptr<LambdaMockProvider> MakeUsageReportingProvider(
    int prompt_tokens) {
  return std::make_shared<LambdaMockProvider>(
      "fake", [prompt_tokens](const ChatRequest&, ChatEventSink sink,
                              std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "ack"}});
        sink(ChatEvent{UsageReportedEvent{
            .provider_id = ::yac::ProviderId{"fake"},
            .model = ::yac::ModelId{"fake-model"},
            .usage = TokenUsage{.prompt_tokens = prompt_tokens,
                                .completion_tokens = 1,
                                .total_tokens = prompt_tokens + 1}}});
      });
}

}  // namespace

TEST_CASE("ChatService includes system prompt before history in requests") {
  auto provider = MakeFakeProvider();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"fake-model"};
  config.system_prompt = "Use terse answers.";
  auto service = MakeService(provider, config);

  (void)CollectEvents(service, "hello");

  const auto request = provider->LastRequest();
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[0].role == ChatRole::System);
  REQUIRE(request.messages[0].content == "Use terse answers.");
  REQUIRE(request.messages[1].role == ChatRole::User);
  REQUIRE(request.messages[1].content == "hello");
}

TEST_CASE("ChatService ResetConversation clears history and pending") {
  auto service = MakeService();

  std::vector<ChatEvent> events;
  std::mutex mtx;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("hello");

  service.ResetConversation();

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.IsBusy());

  std::scoped_lock lock(mtx);
  REQUIRE(HasEvent(events, ChatEventType::ConversationCleared));
}

TEST_CASE("ChatService assigns unique message IDs") {
  auto service = MakeService();

  std::vector<ChatMessageId> ids;
  std::mutex mtx;
  std::condition_variable cv;
  int finished_count = 0;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    if (const auto* queued = event.As<UserMessageQueuedEvent>()) {
      ids.push_back(queued->message_id);
    }
    if (event.Type() == ChatEventType::Finished) {
      ++finished_count;
      cv.notify_one();
    }
  });

  auto id1 = service.SubmitUserMessage("first");
  auto id2 = service.SubmitUserMessage("second");

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished_count >= 2; });

  REQUIRE(id1 != id2);
}

TEST_CASE("LoadChatConfig returns defaults when settings.toml is absent") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_defaults.toml");
  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;
  REQUIRE(config.provider_id.value == "openai-compatible");
  REQUIRE(config.model.value == "gpt-4o-mini");
  REQUIRE(config.temperature == 0.7);
  REQUIRE(config.max_tool_rounds == kDefaultToolRoundLimit);
  REQUIRE_FALSE(config.system_prompt.has_value());
}

TEST_CASE(
    "LoadChatConfigResult creates settings.toml on first run when requested") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_first_run.toml");
  REQUIRE_FALSE(std::filesystem::exists(settings.Path()));
  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/true);
  REQUIRE(std::filesystem::exists(settings.Path()));
  REQUIRE(result.config.provider_id.value == "openai-compatible");
  REQUIRE(result.config.model.value == "gpt-4o-mini");
}

TEST_CASE("LoadChatConfigResult warns when API key is missing") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_missing_key.toml");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.api_key.empty());
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Warning &&
           issue.message.find("OPENAI_API_KEY") != std::string::npos &&
           issue.detail.find("sk-") == std::string::npos;
  }));
}

TEST_CASE("YAC_TEMPERATURE env var reports invalid values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_temp_env.toml");
  setenv("YAC_TEMPERATURE", "too-hot", 1);

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.temperature == 0.7);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message == "Invalid YAC_TEMPERATURE";
  }));

  unsetenv("YAC_TEMPERATURE");
}

TEST_CASE("settings.toml reports out-of-range temperature as an error") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_temp_toml.toml");
  settings.Write("temperature = 5.0\n");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.temperature == 0.7);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("temperature") != std::string::npos;
  }));
}

TEST_CASE("[provider].id = zai applies the Z.ai preset") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_zai_defaults.toml");
  settings.Write(
      "[provider]\n"
      "id = \"zai\"\n");
  setenv("ZAI_API_KEY", "zai-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id.value == "zai");
  REQUIRE(config.model.value == "glm-5.1");
  REQUIRE(config.base_url == "https://api.z.ai/api/coding/paas/v4");
  REQUIRE(config.api_key_env == "ZAI_API_KEY");
  REQUIRE(config.api_key == "zai-api-key");

  unsetenv("ZAI_API_KEY");
}

TEST_CASE("TOML values override the Z.ai preset") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_zai_overrides.toml");
  settings.Write(
      "[provider]\n"
      "id = \"zai\"\n"
      "model = \"glm-4.7\"\n"
      "base_url = \"https://zai.example.com/v1\"\n"
      "api_key_env = \"YAC_CUSTOM_ZAI_KEY\"\n");
  setenv("YAC_CUSTOM_ZAI_KEY", "custom-zai-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id.value == "zai");
  REQUIRE(config.model.value == "glm-4.7");
  REQUIRE(config.base_url == "https://zai.example.com/v1");
  REQUIRE(config.api_key_env == "YAC_CUSTOM_ZAI_KEY");
  REQUIRE(config.api_key == "custom-zai-key");

  unsetenv("YAC_CUSTOM_ZAI_KEY");
}

TEST_CASE("settings.toml values are read end-to-end") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_full.toml");
  settings.Write(
      "temperature = 1.5\n"
      "max_tool_rounds = 44\n"
      "system_prompt = \"TOML system prompt\"\n"
      "\n"
      "[provider]\n"
      "id = \"custom-provider\"\n"
      "model = \"custom-model\"\n"
      "base_url = \"https://example.com/v1/\"\n"
      "api_key_env = \"YAC_TEST_API_KEY_FROM_FILE\"\n");
  setenv("YAC_TEST_API_KEY_FROM_FILE", "toml-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id.value == "custom-provider");
  REQUIRE(config.model.value == "custom-model");
  REQUIRE(config.base_url == "https://example.com/v1/");
  REQUIRE(config.temperature == 1.5);
  REQUIRE(config.max_tool_rounds == 44);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_FROM_FILE");
  REQUIRE(config.api_key == "toml-api-key");
  REQUIRE(config.system_prompt == std::string{"TOML system prompt"});

  unsetenv("YAC_TEST_API_KEY_FROM_FILE");
}

TEST_CASE("YAC_* env vars override settings.toml values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_env_override.toml");
  settings.Write(
      "temperature = 1.5\n"
      "\n"
      "[provider]\n"
      "id = \"file-provider\"\n"
      "model = \"file-model\"\n"
      "api_key_env = \"YAC_TEST_API_KEY_OVERRIDE\"\n");

  setenv("YAC_PROVIDER", "env-provider", 1);
  setenv("YAC_MODEL", "env-model", 1);
  setenv("YAC_TEMPERATURE", "0.8", 1);
  setenv("YAC_MAX_TOOL_ROUNDS", "12", 1);
  setenv("YAC_TEST_API_KEY_OVERRIDE", "env-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id.value == "env-provider");
  REQUIRE(config.model.value == "env-model");
  REQUIRE(config.temperature == 0.8);
  REQUIRE(config.max_tool_rounds == 12);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_OVERRIDE");
  REQUIRE(config.api_key == "env-api-key");

  unsetenv("YAC_PROVIDER");
  unsetenv("YAC_MODEL");
  unsetenv("YAC_TEMPERATURE");
  unsetenv("YAC_MAX_TOOL_ROUNDS");
  unsetenv("YAC_TEST_API_KEY_OVERRIDE");
}

TEST_CASE("YAC_MAX_TOOL_ROUNDS env var reports invalid values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_tool_rounds_env.toml");
  setenv("YAC_MAX_TOOL_ROUNDS", "0", 1);

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.max_tool_rounds == kDefaultToolRoundLimit);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message == "Invalid YAC_MAX_TOOL_ROUNDS";
  }));

  unsetenv("YAC_MAX_TOOL_ROUNDS");
}

TEST_CASE(
    "Malformed settings.toml reports an error but yields a usable "
    "config") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_malformed.toml");
  settings.Write("this is =  not = valid\n");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.provider_id.value == "openai-compatible");
  REQUIRE(result.config.model.value == "gpt-4o-mini");
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("settings.toml") != std::string::npos;
  }));
}

TEST_CASE(
    "ChatService auto-compacts before the next prompt when usage crosses the "
    "threshold") {
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"gpt-4o-mini"};
  config.auto_compact_enabled = true;
  config.auto_compact_threshold = 0.5;
  config.auto_compact_keep_last = 2;
  config.auto_compact_mode = "truncate";
  auto provider = MakeUsageReportingProvider(108800);
  auto service = MakeService(provider, config);

  (void)CollectEvents(service, "first message");
  (void)CollectEvents(service, "second message");
  (void)CollectEvents(service, "third message");

  const auto round = CollectEvents(service, "fourth message");
  REQUIRE(HasEvent(round, ChatEventType::ConversationCompacted));

  const auto history = service.History();
  REQUIRE_FALSE(history.empty());
  REQUIRE(history.front().role == ChatRole::System);
  REQUIRE(history.front().content.find("messages removed") !=
          std::string::npos);
}

TEST_CASE(
    "ChatService does not auto-compact when usage is below the threshold") {
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"gpt-4o-mini"};
  config.auto_compact_enabled = true;
  config.auto_compact_threshold = 0.9;
  config.auto_compact_keep_last = 2;
  config.auto_compact_mode = "truncate";
  auto provider = MakeUsageReportingProvider(64000);
  auto service = MakeService(provider, config);

  (void)CollectEvents(service, "first");
  (void)CollectEvents(service, "second");
  (void)CollectEvents(service, "third");
  const auto round = CollectEvents(service, "fourth");

  REQUIRE_FALSE(HasEvent(round, ChatEventType::ConversationCompacted));
}

TEST_CASE("ChatService respects auto_compact_enabled = false") {
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"gpt-4o-mini"};
  config.auto_compact_enabled = false;
  config.auto_compact_threshold = 0.1;
  config.auto_compact_keep_last = 2;
  config.auto_compact_mode = "truncate";
  auto provider = MakeUsageReportingProvider(120000);
  auto service = MakeService(provider, config);

  (void)CollectEvents(service, "one");
  (void)CollectEvents(service, "two");
  const auto round = CollectEvents(service, "three");

  REQUIRE_FALSE(HasEvent(round, ChatEventType::ConversationCompacted));
}

TEST_CASE("ChatService stress: submit-then-reset never leaves history dirty") {
  auto service = MakeService();
  service.SetResetDrainBudgetForTest(std::chrono::milliseconds(500));

  for (int i = 0; i < 50; ++i) {
    service.SubmitUserMessage("stress-" + std::to_string(i));
    service.ResetConversation();
    REQUIRE(service.History().empty());
  }
}
