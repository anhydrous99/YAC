#include "config_env_test_helpers.hpp"
#include "fake_state_store.hpp"
#include "openai_auth_test_helpers.hpp"
#include "provider/openai_auth.hpp"
#include "provider/openai_auth_flow.hpp"
#include "provider/openai_auth_store.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ChatEvent;
using yac::chat::ChatEventType;
using yac::chat::ChatMessage;
using yac::chat::ChatRequest;
using yac::chat::ChatRole;
using yac::chat::ProviderConfig;
using yac::chat::ProviderCredential;
using yac::chat::StateCredentialSource;
using yac::chat::StateCredentialType;
using yac::provider::IOpenAiAuthBackend;
using yac::provider::OpenAiApiKeyAuth;
using yac::provider::OpenAiAuthFlow;
using yac::provider::OpenAiAuthStore;
using yac::provider::OpenAiChatProvider;
using yac::provider::OpenAiCompatibleChatProvider;
using yac::provider::OpenAiOAuthAuth;
using yac::provider::SerializeOpenAiAuth;
using yac::testing::FakeStateStore;
using yac::testing::ScopedEnvClear;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::TestHttpServer;

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::optional<std::string> value)
      : name_(std::move(name)) {
    if (const char* previous = std::getenv(name_.c_str())) {
      previous_ = previous;
    }
    if (value.has_value()) {
      setenv(name_.c_str(), value->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ~ScopedEnvVar() {
    if (previous_.has_value()) {
      setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

class EmptyOpenAiAuthBackend final : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    return std::nullopt;
  }

  void Set(std::string_view auth_json) override { (void)auth_json; }

  void Erase() override {}
};

ProviderCredential ApiKeyCredential(std::string provider_id,
                                    std::string secret_json) {
  return ProviderCredential{.provider_id = yac::ProviderId{provider_id},
                            .credential_type = StateCredentialType::ApiKey,
                            .secret_json = std::move(secret_json),
                            .source = StateCredentialSource::Test,
                            .created_at = "2026-05-30T00:00:00Z",
                            .updated_at = "2026-05-30T00:00:00Z"};
}

ProviderCredential OpenAiAuthCredential(std::string secret_json) {
  return ProviderCredential{.provider_id = yac::ProviderId{"openai"},
                            .credential_type = StateCredentialType::OpenAiAuth,
                            .secret_json = std::move(secret_json),
                            .source = StateCredentialSource::Test,
                            .created_at = "2026-05-30T00:00:00Z",
                            .updated_at = "2026-05-30T00:00:00Z"};
}

ProviderConfig CompatibleConfig(std::string base_url,
                                std::shared_ptr<FakeStateStore> store) {
  ProviderConfig config;
  config.id = yac::ProviderId{"openai-compatible"};
  config.model = yac::ModelId{"test-model"};
  config.base_url = std::move(base_url);
  config.api_key_env = "YAC_PROVIDER_CREDENTIAL_TEST_KEY";
  config.state_store = std::move(store);
  return config;
}

ProviderConfig OpenAiConfig(std::string base_url,
                            std::shared_ptr<FakeStateStore> store) {
  ProviderConfig config;
  config.id = yac::ProviderId{"openai"};
  config.model = yac::ModelId{"gpt-5.4"};
  config.base_url = std::move(base_url);
  config.api_key_env = "YAC_PROVIDER_CREDENTIAL_TEST_KEY";
  config.state_store = std::move(store);
  return config;
}

ChatRequest StreamingRequest() {
  ChatRequest request;
  request.model = yac::ModelId{"test-model"};
  request.stream = true;
  request.messages = {ChatMessage{.role = ChatRole::User, .content = "hello"}};
  return request;
}

std::vector<ChatEvent> RunStream(OpenAiCompatibleChatProvider& provider) {
  std::vector<ChatEvent> events;
  provider.CompleteStream(
      StreamingRequest(),
      [&events](ChatEvent event) { events.push_back(std::move(event)); },
      std::stop_token{});
  return events;
}

std::vector<ChatEvent> RunStream(OpenAiChatProvider& provider) {
  std::vector<ChatEvent> events;
  provider.CompleteStream(
      StreamingRequest(),
      [&events](ChatEvent event) { events.push_back(std::move(event)); },
      std::stop_token{});
  return events;
}

HttpResponse OkStream() {
  return HttpResponse{
      .headers = {{"Content-Type", "text/event-stream"}},
      .body = "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n"};
}

}  // namespace

TEST_CASE("provider credential precedence is env then TOML then SQLite") {
  ScopedEnvClear env_guard({"YAC_PROVIDER_CREDENTIAL_TEST_KEY"});

  SECTION("env API key beats TOML and SQLite for generic compatible provider") {
    auto store = std::make_shared<FakeStateStore>();
    store->SaveProviderCredential(ApiKeyCredential(
        "openai-compatible", R"({"api_key":"sqlite-secret"})"));
    TestHttpServer server([](const HttpRequest& request, std::size_t) {
      REQUIRE(request.headers.at("Authorization") == "Bearer env-secret");
      return OkStream();
    });
    auto config = CompatibleConfig(server.Url(""), store);
    config.api_key = "toml-secret";
    ScopedEnvVar env("YAC_PROVIDER_CREDENTIAL_TEST_KEY", "env-secret");

    OpenAiCompatibleChatProvider provider(config);
    const auto events = RunStream(provider);

    REQUIRE(server.Requests().size() == 1);
    REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  }

  SECTION("TOML inline key beats SQLite for generic compatible provider") {
    auto store = std::make_shared<FakeStateStore>();
    store->SaveProviderCredential(ApiKeyCredential(
        "openai-compatible", R"({"api_key":"sqlite-secret"})"));
    TestHttpServer server([](const HttpRequest& request, std::size_t) {
      REQUIRE(request.headers.at("Authorization") == "Bearer toml-secret");
      return OkStream();
    });
    auto config = CompatibleConfig(server.Url(""), store);
    config.api_key = "toml-secret";

    OpenAiCompatibleChatProvider provider(config);
    const auto events = RunStream(provider);

    REQUIRE(server.Requests().size() == 1);
    REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  }

  SECTION("SQLite API key is used when env and TOML are absent") {
    auto store = std::make_shared<FakeStateStore>();
    store->SaveProviderCredential(ApiKeyCredential(
        "openai-compatible", R"({"api_key":"sqlite-secret"})"));
    TestHttpServer server([](const HttpRequest& request, std::size_t) {
      REQUIRE(request.headers.at("Authorization") == "Bearer sqlite-secret");
      return OkStream();
    });

    OpenAiCompatibleChatProvider provider(
        CompatibleConfig(server.Url(""), store));
    const auto events = RunStream(provider);

    REQUIRE(server.Requests().size() == 1);
    REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  }

  SECTION("OpenAI SQLite auth is used only when env and TOML are absent") {
    auto store = std::make_shared<FakeStateStore>();
    store->SaveProviderCredential(OpenAiAuthCredential(
        SerializeOpenAiAuth(OpenAiApiKeyAuth{.key = "sqlite-openai-secret"})));
    TestHttpServer server([](const HttpRequest& request, std::size_t) {
      REQUIRE(request.headers.at("Authorization") ==
              "Bearer sqlite-openai-secret");
      return OkStream();
    });

    OpenAiChatProvider provider(OpenAiConfig(server.Url(""), store));
    const auto events = RunStream(provider);

    REQUIRE(server.Requests().size() == 1);
    REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  }
}

TEST_CASE("TOML plaintext key is not persisted by provider startup") {
  auto store = std::make_shared<FakeStateStore>();
  auto config = CompatibleConfig("http://127.0.0.1:1", store);
  config.api_key = "toml-secret-never-save";

  OpenAiCompatibleChatProvider provider(config);

  REQUIRE(store->ListProviderCredentials().empty());
}

TEST_CASE("provider auth errors redact stored SQLite secrets") {
  ScopedEnvClear env_guard({"YAC_PROVIDER_CREDENTIAL_TEST_KEY"});
  auto store = std::make_shared<FakeStateStore>();
  store->SaveProviderCredential(OpenAiAuthCredential(SerializeOpenAiAuth(
      OpenAiOAuthAuth{.refresh_token = "sqlite-refresh-secret",
                      .access_token = "sqlite-access-secret",
                      .expires_at = std::chrono::system_clock::time_point{
                          std::chrono::hours{24 * 365 * 100}}})));
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer sqlite-access-secret");
    return HttpResponse{
        .status = 403,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"error":{"message":"bad sqlite-access-secret and sqlite-refresh-secret"}})"};
  });

  auto config = OpenAiConfig(server.Url(""), store);
  auto empty_backend = std::make_shared<EmptyOpenAiAuthBackend>();
  auto empty_auth_store =
      std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
          .keychain_backend = empty_backend, .file_backend = empty_backend});
  OpenAiAuthFlow::Dependencies flow_dependencies;
  flow_dependencies.auth_store = std::move(empty_auth_store);
  OpenAiChatProvider provider(
      config,
      OpenAiChatProvider::Dependencies{
          .auth_flow = std::make_shared<OpenAiAuthFlow>(flow_dependencies),
          .oauth_base_url = config.base_url});
  const auto events = RunStream(provider);

  REQUIRE(server.Requests().size() == 1);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == ChatEventType::Error);
  const auto& text = events[0].Get<yac::chat::ErrorEvent>().text;
  REQUIRE(text.find("sqlite-access-secret") == std::string::npos);
  REQUIRE(text.find("sqlite-refresh-secret") == std::string::npos);
}
