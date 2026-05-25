#include "app/provider_auth_startup.hpp"
#include "app/provider_factory.hpp"
#include "chat/types.hpp"
#include "provider/bedrock_chat_provider.hpp"
#include "provider/openai_auth_store.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class MemoryAuthBackend : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    return value_;
  }

  void Set(std::string_view auth_json) override {
    std::scoped_lock lock(mutex_);
    value_ = std::string(auth_json);
  }

  void Erase() override {
    std::scoped_lock lock(mutex_);
    value_.reset();
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
};

class ThrowingFileBackend : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw std::runtime_error("file backend unreadable: access-stored");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw std::runtime_error("file backend unreadable");
  }

  void Erase() override { throw std::runtime_error("file backend unreadable"); }
};

class ThrowingKeychainBackend : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }

  void Erase() override {
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }
};

std::shared_ptr<yac::provider::OpenAiAuthStore> MakeStore() {
  return std::make_shared<yac::provider::OpenAiAuthStore>(
      yac::provider::OpenAiAuthStore::Dependencies{
          .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
          .file_backend = std::make_shared<MemoryAuthBackend>(),
      });
}

std::shared_ptr<yac::provider::OpenAiAuthStore> MakeThrowingFileStore() {
  return std::make_shared<yac::provider::OpenAiAuthStore>(
      yac::provider::OpenAiAuthStore::Dependencies{
          .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
          .file_backend = std::make_shared<ThrowingFileBackend>(),
      });
}

yac::provider::OpenAiChatProvider MakeOpenAiProvider(
    const yac::chat::ProviderConfig& config,
    const std::shared_ptr<yac::provider::OpenAiAuthStore>& store) {
  yac::provider::OpenAiAuthFlow::Dependencies flow_dependencies;
  flow_dependencies.auth_store = store;
  return yac::provider::OpenAiChatProvider(
      config, yac::provider::OpenAiChatProvider::Dependencies{
                  .auth_flow = std::make_shared<yac::provider::OpenAiAuthFlow>(
                      flow_dependencies),
              });
}

bool HasNotice(const std::vector<yac::chat::ConfigIssue>& issues,
               std::string_view text) {
  for (const auto& issue : issues) {
    if (issue.message.find(text) != std::string::npos ||
        issue.detail.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("MakeLanguageModelProvider returns OpenAI provider for openai") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::OpenAiChatProvider*>(provider.get()) !=
          nullptr);
  REQUIRE(provider->Id() == "openai");
}

TEST_CASE(
    "MakeLanguageModelProvider returns OpenAI-compatible provider for generic "
    "ids") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"custom-provider"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::OpenAiCompatibleChatProvider*>(
              provider.get()) != nullptr);
  REQUIRE(provider->Id() == "custom-provider");
}

TEST_CASE("MakeLanguageModelProvider returns Bedrock provider for bedrock") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"bedrock"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::BedrockChatProvider*>(provider.get()) !=
          nullptr);
  REQUIRE(provider->Id() == "bedrock");
}

TEST_CASE("OpenAI startup auth notice follows stored auth state") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.api_key_env = "OPENAI_API_KEY";

  SECTION("stored OAuth suppresses missing API key warning") {
    auto store = MakeStore();
    (void)store->Save(yac::provider::OpenAiOAuthAuth{
        .refresh_token = "refresh-stored",
        .access_token = "access-stored",
        .expires_at =
            std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
    });
    auto provider = MakeOpenAiProvider(config, store);
    std::vector<yac::chat::ConfigIssue> issues;

    yac::app::AppendProviderAuthStartupIssues(config, provider, issues);

    REQUIRE_FALSE(HasNotice(issues, "OPENAI_API_KEY is not set"));
    REQUIRE_FALSE(HasNotice(issues, "OpenAI auth is not configured"));
    REQUIRE_FALSE(HasNotice(issues, "refresh-stored"));
    REQUIRE_FALSE(HasNotice(issues, "access-stored"));
  }

  SECTION("missing auth gets OpenAI-specific warning") {
    auto provider = MakeOpenAiProvider(config, MakeStore());
    std::vector<yac::chat::ConfigIssue> issues;

    yac::app::AppendProviderAuthStartupIssues(config, provider, issues);

    REQUIRE(HasNotice(issues, "OpenAI auth is not configured"));
    REQUIRE(HasNotice(issues, "OPENAI_API_KEY"));
    REQUIRE(HasNotice(issues, "yac auth openai login"));
  }

  SECTION("stored auth read failures emit a warning instead of throwing") {
    auto provider = MakeOpenAiProvider(config, MakeThrowingFileStore());
    std::vector<yac::chat::ConfigIssue> issues;

    REQUIRE_NOTHROW(
        yac::app::AppendProviderAuthStartupIssues(config, provider, issues));
    REQUIRE(HasNotice(issues, "OpenAI stored auth could not be read"));
    REQUIRE_FALSE(HasNotice(issues, "file backend unreadable"));
    REQUIRE_FALSE(HasNotice(issues, "access-stored"));
    REQUIRE_FALSE(HasNotice(issues, "OpenAI auth is not configured"));
  }
}
