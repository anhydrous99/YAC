#include "app/provider_auth_startup.hpp"
#include "openai_auth_test_helpers.hpp"
#include "provider/openai_chat_provider.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace yac::provider::test {
namespace {

using yac::tests::openai_auth::AssertHeaderAbsent;
using yac::tests::openai_auth::AssertHeaderEquals;
using yac::tests::openai_auth::DeterministicSessionId;
using yac::tests::openai_auth::DynamicModelIdTestVectors;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::MakeAccountIdJwtLikeToken;
using yac::tests::openai_auth::StaticAcceptedModelIds;
using yac::tests::openai_auth::TestHttpServer;

constexpr std::string_view kExpectedCodexBaseInstructions =
    "You are YAC, a terminal coding assistant. Help the user with software "
    "engineering tasks while following the available tools, workspace "
    "instructions, and safety constraints.";

class MemoryAuthBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    ++get_count_;
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

  [[nodiscard]] int GetCount() const {
    std::scoped_lock lock(mutex_);
    return get_count_;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
  mutable int get_count_ = 0;
};

class ThrowingKeychainBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    ++get_count_;
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Erase() override {
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  [[nodiscard]] int GetCount() const { return get_count_; }

 private:
  mutable int get_count_ = 0;
};

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      had_old_value_ = true;
      old_value_ = current;
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (had_old_value_) {
      setenv(name_.c_str(), old_value_.c_str(), 1);
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
  std::string old_value_;
  bool had_old_value_ = false;
};

class ScopedEnvUnset {
 public:
  explicit ScopedEnvUnset(std::string name) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      had_old_value_ = true;
      old_value_ = current;
    }
    unsetenv(name_.c_str());
  }

  ~ScopedEnvUnset() {
    if (had_old_value_) {
      setenv(name_.c_str(), old_value_.c_str(), 1);
    }
  }

  ScopedEnvUnset(const ScopedEnvUnset&) = delete;
  ScopedEnvUnset& operator=(const ScopedEnvUnset&) = delete;
  ScopedEnvUnset(ScopedEnvUnset&&) = delete;
  ScopedEnvUnset& operator=(ScopedEnvUnset&&) = delete;

 private:
  std::string name_;
  std::string old_value_;
  bool had_old_value_ = false;
};

[[nodiscard]] std::shared_ptr<MemoryAuthBackend> MakeFileBackend() {
  return std::make_shared<MemoryAuthBackend>();
}

[[nodiscard]] std::shared_ptr<OpenAiAuthStore> MakeStore(
    const std::shared_ptr<MemoryAuthBackend>& file_backend) {
  return std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
      .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
      .file_backend = file_backend,
  });
}

[[nodiscard]] OpenAiAuthFlow::Dependencies MakeFlowDependencies(
    std::string issuer_url, const std::shared_ptr<OpenAiAuthStore>& store) {
  OpenAiAuthFlow::Dependencies dependencies;
  dependencies.issuer_url = std::move(issuer_url);
  dependencies.auth_store = store;
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  return dependencies;
}

[[nodiscard]] OpenAiChatProvider MakeProvider(
    const std::string& base_url, const std::shared_ptr<OpenAiAuthStore>& store,
    const std::string& oauth_base_url = {}) {
  chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.model = ::yac::ModelId{"gpt-5.4"};
  config.base_url = base_url;
  config.api_key_env = "OPENAI_API_KEY";
  return OpenAiChatProvider(
      config,
      OpenAiChatProvider::Dependencies{
          .auth_flow = std::make_shared<OpenAiAuthFlow>(
              MakeFlowDependencies(base_url, store)),
          .oauth_base_url = oauth_base_url.empty() ? base_url : oauth_base_url,
      });
}

[[nodiscard]] OpenAiChatProvider MakeProviderWithInlineKey(
    const std::string& base_url, const std::shared_ptr<OpenAiAuthStore>& store,
    std::string inline_key, const std::string& oauth_base_url = {}) {
  chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.model = ::yac::ModelId{"gpt-5.4"};
  config.base_url = base_url;
  config.api_key_env = "OPENAI_API_KEY";
  config.api_key = std::move(inline_key);
  return OpenAiChatProvider(
      config,
      OpenAiChatProvider::Dependencies{
          .auth_flow = std::make_shared<OpenAiAuthFlow>(
              MakeFlowDependencies(base_url, store)),
          .oauth_base_url = oauth_base_url.empty() ? base_url : oauth_base_url,
      });
}

[[nodiscard]] chat::ChatRequest MakeStreamingRequest(
    std::optional<std::string> responses_instructions = std::nullopt) {
  chat::ChatRequest request;
  request.model = ::yac::ModelId{"gpt-5.4"};
  request.session_id = DeterministicSessionId();
  request.responses_instructions = std::move(responses_instructions);
  request.stream = true;
  request.messages = {
      chat::ChatMessage{.role = chat::ChatRole::User, .content = "hello"}};
  return request;
}

[[nodiscard]] HttpResponse ModelsResponse() {
  return HttpResponse{
      .headers = {{"Content-Type", "application/json"}},
      .body = R"JSON({"data":[{"id":"gpt-4o-mini","object":"model"}]})JSON"};
}

[[nodiscard]] HttpResponse ResponsesStream(std::string_view text = "hello") {
  return HttpResponse{
      .headers = {{"Content-Type", "text/event-stream"}},
      .body = std::string(
                  R"(data: {"type":"response.output_text.delta","delta":")") +
              std::string(text) +
              R"("}
data: {"type":"response.completed","response":{"usage":{"input_tokens":1,"output_tokens":1,"total_tokens":2}}}
)",
  };
}

[[nodiscard]] std::vector<chat::ChatEvent> RunStream(
    OpenAiChatProvider& provider) {
  std::vector<chat::ChatEvent> events;
  provider.CompleteStream(
      MakeStreamingRequest(),
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      {});
  return events;
}

}  // namespace

TEST_CASE("env_api_key_wins_over_stored_openai_auth",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-stored"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/models");
    REQUIRE(request.headers.at("Authorization") == "Bearer env-key");
    return ModelsResponse();
  });
  ScopedEnvVar env("OPENAI_API_KEY", "env-key");

  auto provider = MakeProvider(server.Url(""), store);
  const auto models = provider.ListModels(500ms);
  REQUIRE(models.size() == 1);
  REQUIRE(server.Requests().size() == 1);
}

TEST_CASE("effective_auth_source_reports_precedence_without_secrets",
          "[openai_chat_provider_auth]") {
  ScopedEnvUnset env("OPENAI_API_KEY");

  SECTION("none") {
    const auto store = MakeStore(MakeFileBackend());
    auto provider = MakeProvider("http://127.0.0.1:1", store);

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::None);
  }

  SECTION("inline key") {
    const auto store = MakeStore(MakeFileBackend());
    chat::ProviderConfig config;
    config.id = ::yac::ProviderId{"openai"};
    config.model = ::yac::ModelId{"gpt-5.4"};
    config.base_url = "http://127.0.0.1:1";
    config.api_key_env = "OPENAI_API_KEY";
    config.api_key = "inline-secret";
    OpenAiChatProvider provider(
        config, OpenAiChatProvider::Dependencies{
                    .auth_flow = std::make_shared<OpenAiAuthFlow>(
                        MakeFlowDependencies(config.base_url, store)),
                    .oauth_base_url = config.base_url,
                });

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::InlineApiKey);
  }

  SECTION("inline key shadows stored OAuth") {
    const auto store = MakeStore(MakeFileBackend());
    (void)store->Save(OpenAiOAuthAuth{
        .refresh_token = "refresh-stored",
        .access_token = "access-stored",
        .expires_at =
            std::chrono::system_clock::time_point{std::chrono::seconds{2000}}});
    auto provider =
        MakeProviderWithInlineKey("http://127.0.0.1:1", store, "inline-secret");

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::InlineApiKey);
  }

  SECTION("inline key shadows stored API key") {
    const auto store = MakeStore(MakeFileBackend());
    (void)store->Save(OpenAiApiKeyAuth{.key = "stored-secret"});
    auto provider =
        MakeProviderWithInlineKey("http://127.0.0.1:1", store, "inline-secret");

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::InlineApiKey);
  }

  SECTION("stored API key") {
    const auto store = MakeStore(MakeFileBackend());
    (void)store->Save(OpenAiApiKeyAuth{.key = "stored-secret"});
    auto provider = MakeProvider("http://127.0.0.1:1", store);

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::StoredApiKey);
  }

  SECTION("env key shadows stored auth") {
    const auto store = MakeStore(MakeFileBackend());
    (void)store->Save(OpenAiOAuthAuth{.refresh_token = "refresh-stored",
                                      .access_token = "access-stored"});
    ScopedEnvVar env_key("OPENAI_API_KEY", "env-secret");
    auto provider = MakeProvider("http://127.0.0.1:1", store);

    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::EnvApiKey);
  }
}

TEST_CASE("env_api_key_bypasses_stored_auth_reads",
          "[openai_chat_provider_auth]") {
  const auto keychain = std::make_shared<ThrowingKeychainBackend>();
  const auto file_backend = MakeFileBackend();
  const auto store =
      std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
          .keychain_backend = keychain, .file_backend = file_backend});
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-stored"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.headers.at("Authorization") == "Bearer env-secret");
    if (request.path == "/models") {
      return ModelsResponse();
    }
    REQUIRE(request.path == "/chat/completions");
    return HttpResponse{
        .headers = {{"Content-Type", "text/event-stream"}},
        .body = "data: {\"choices\":[{\"delta\":{\"content\":\"env-ok\"}}]}\n"};
  });
  ScopedEnvVar env("OPENAI_API_KEY", "env-secret");

  auto provider = MakeProvider(server.Url(""), store);
  REQUIRE(provider.ResolveEffectiveAuthSource() ==
          OpenAiChatProvider::EffectiveAuthSource::EnvApiKey);
  const auto models = provider.ListModels(500ms);
  const auto events = RunStream(provider);

  REQUIRE(models.size() == 1);
  REQUIRE(server.Requests().size() == 2);
  REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "env-ok");
  REQUIRE(keychain->GetCount() == 0);
  REQUIRE(file_backend->GetCount() == 0);
}

TEST_CASE("inline_api_key_shadows_stored_auth_for_openai_runtime",
          "[openai_chat_provider_auth]") {
  ScopedEnvUnset env("OPENAI_API_KEY");

  SECTION("stored OAuth") {
    const auto file_backend = MakeFileBackend();
    const auto store = MakeStore(file_backend);
    (void)store->Save(OpenAiOAuthAuth{
        .refresh_token = "refresh-stored",
        .access_token = "access-stored",
        .expires_at =
            std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
        .account_id = "acct-stored"});
    TestHttpServer server([](const HttpRequest& request, std::size_t index) {
      REQUIRE_NOTHROW(
          AssertHeaderEquals(request, "Authorization", "Bearer inline-key"));
      if (index == 0) {
        REQUIRE(request.path == "/models");
        return ModelsResponse();
      }
      REQUIRE(request.path == "/chat/completions");
      REQUIRE_NOTHROW(AssertHeaderAbsent(request, "originator"));
      REQUIRE_NOTHROW(AssertHeaderAbsent(request, "ChatGPT-Account-Id"));
      return HttpResponse{
          .headers = {{"Content-Type", "text/event-stream"}},
          .body =
              "data: "
              "{\"choices\":[{\"delta\":{\"content\":\"inline-ok\"}}]}\n"};
    });

    auto provider = MakeProviderWithInlineKey(server.Url(""), store,
                                              "inline-key", server.Url(""));
    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::InlineApiKey);
    const auto models = provider.ListModels(500ms);
    const auto events = RunStream(provider);

    REQUIRE(models.size() == 1);
    REQUIRE(server.Requests().size() == 2);
    REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
    REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "inline-ok");
  }

  SECTION("stored API key") {
    const auto file_backend = MakeFileBackend();
    const auto store = MakeStore(file_backend);
    (void)store->Save(OpenAiApiKeyAuth{.key = "stored-api-key"});
    TestHttpServer server([](const HttpRequest& request, std::size_t) {
      REQUIRE(request.path == "/chat/completions");
      REQUIRE_NOTHROW(
          AssertHeaderEquals(request, "Authorization", "Bearer inline-key"));
      return HttpResponse{
          .headers = {{"Content-Type", "text/event-stream"}},
          .body =
              "data: "
              "{\"choices\":[{\"delta\":{\"content\":\"inline-api-ok\"}}]}\n"};
    });

    auto provider =
        MakeProviderWithInlineKey(server.Url(""), store, "inline-key");
    REQUIRE(provider.ResolveEffectiveAuthSource() ==
            OpenAiChatProvider::EffectiveAuthSource::InlineApiKey);
    const auto events = RunStream(provider);

    REQUIRE(server.Requests().size() == 1);
    REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
    REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "inline-api-ok");
  }
}

TEST_CASE("stored_api_key_is_used_for_openai_runtime",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiApiKeyAuth{.key = "stored-api-key"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/chat/completions");
    REQUIRE(request.headers.at("Authorization") == "Bearer stored-api-key");
    REQUIRE_NOTHROW(AssertHeaderAbsent(request, "originator"));
    REQUIRE_NOTHROW(AssertHeaderAbsent(request, "session_id"));
    return HttpResponse{
        .headers = {{"Content-Type", "text/event-stream"}},
        .body =
            "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n"
            "data: "
            "{\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1,\"total_"
            "tokens\":2}}\n"};
  });

  auto provider = MakeProvider(server.Url(""), store);
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
}

TEST_CASE("stored_api_key_cache_is_reused_for_source_and_models",
          "[openai_chat_provider_auth]") {
  ScopedEnvUnset env("OPENAI_API_KEY");
  const auto keychain = std::make_shared<ThrowingKeychainBackend>();
  const auto file_backend = MakeFileBackend();
  file_backend->Set(
      SerializeOpenAiAuth(OpenAiApiKeyAuth{.key = "stored-api-key-cache"}));
  const auto store =
      std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
          .keychain_backend = keychain, .file_backend = file_backend});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/models");
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer stored-api-key-cache");
    return ModelsResponse();
  });

  auto provider = MakeProvider(server.Url(""), store);
  REQUIRE(provider.ResolveEffectiveAuthSource() ==
          OpenAiChatProvider::EffectiveAuthSource::StoredApiKey);
  const auto models = provider.ListModels(500ms);

  REQUIRE(models.size() == 1);
  REQUIRE(server.Requests().size() == 1);
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 1);
}

TEST_CASE("stored_auth_read_once_across_startup_discovery_and_send",
          "[openai_chat_provider_auth]") {
  ScopedEnvUnset env("OPENAI_API_KEY");
  const auto keychain = std::make_shared<MemoryAuthBackend>();
  const auto file_backend = MakeFileBackend();
  keychain->Set(
      SerializeOpenAiAuth(OpenAiApiKeyAuth{.key = "stored-api-key-flow"}));
  const auto store =
      std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
          .keychain_backend = keychain, .file_backend = file_backend});
  TestHttpServer server([](const HttpRequest& request,
                           std::size_t request_index) {
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer stored-api-key-flow");
    if (request_index == 0) {
      REQUIRE(request.path == "/models");
      return ModelsResponse();
    }
    REQUIRE(request_index == 1);
    REQUIRE(request.path == "/chat/completions");
    return HttpResponse{
        .headers = {{"Content-Type", "text/event-stream"}},
        .body =
            "data: {\"choices\":[{\"delta\":{\"content\":\"flow-ok\"}}]}\n"};
  });
  chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.model = ::yac::ModelId{"gpt-5.4"};
  config.base_url = server.Url("");
  config.api_key_env = "OPENAI_API_KEY";
  OpenAiChatProvider provider(
      config, OpenAiChatProvider::Dependencies{
                  .auth_flow = std::make_shared<OpenAiAuthFlow>(
                      MakeFlowDependencies(config.base_url, store)),
                  .oauth_base_url = config.base_url,
              });
  std::vector<chat::ConfigIssue> issues;

  yac::app::AppendProviderAuthStartupIssues(config, provider, issues);
  const auto models = provider.ListModels(500ms);
  const auto events = RunStream(provider);

  REQUIRE(issues.empty());
  REQUIRE(models.size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "flow-ok");
  REQUIRE(server.Requests().size() == 2);
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
}

TEST_CASE("stored_oauth_uses_codex_responses_endpoint_and_headers",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-123"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "Authorization", "Bearer access-stored"));
    REQUIRE_NOTHROW(AssertHeaderEquals(request, "originator", "opencode"));
    REQUIRE_THAT(request.headers.at("User-Agent"),
                 ContainsSubstring("opencode/0.1.0 ("));
    REQUIRE_THAT(request.headers.at("User-Agent"), ContainsSubstring("; "));
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "session_id", DeterministicSessionId()));
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "ChatGPT-Account-Id", "acct-123"));
    const auto body = nlohmann::json::parse(request.body);
    REQUIRE(body.contains("instructions"));
    REQUIRE(body.at("instructions").is_string());
    REQUIRE_THAT(
        body.at("instructions").get<std::string>(),
        ContainsSubstring(std::string(kExpectedCodexBaseInstructions)));
    return ResponsesStream();
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "hello");
}

TEST_CASE("stored_oauth_appends_explicit_responses_instructions",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-123"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "Authorization", "Bearer access-stored"));
    REQUIRE_NOTHROW(AssertHeaderEquals(request, "originator", "opencode"));
    REQUIRE_THAT(request.headers.at("User-Agent"),
                 ContainsSubstring("opencode/0.1.0 ("));
    REQUIRE_THAT(request.headers.at("User-Agent"), ContainsSubstring("; "));
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "session_id", DeterministicSessionId()));
    REQUIRE_NOTHROW(
        AssertHeaderEquals(request, "ChatGPT-Account-Id", "acct-123"));

    const auto body = nlohmann::json::parse(request.body);
    REQUIRE(body.contains("instructions"));
    REQUIRE(body.at("instructions").is_string());
    REQUIRE(body.at("instructions").get<std::string>() ==
            std::string(kExpectedCodexBaseInstructions) +
                "\n\nFollow the caller instructions.");
    return ResponsesStream();
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  auto request = MakeStreamingRequest("Follow the caller instructions.");
  std::vector<chat::ChatEvent> events;
  provider.CompleteStream(
      request,
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      {});
  REQUIRE(server.Requests().size() == 1);
}

TEST_CASE("legacy_inline_api_key_is_used_when_no_env_or_stored_auth",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/models");
    REQUIRE(request.headers.at("Authorization") == "Bearer legacy-key");
    return ModelsResponse();
  });

  chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.model = ::yac::ModelId{"gpt-5.4"};
  config.base_url = server.Url("");
  config.api_key_env = "OPENAI_API_KEY";
  config.api_key = "legacy-key";
  OpenAiChatProvider provider(
      config, OpenAiChatProvider::Dependencies{
                  .auth_flow = std::make_shared<OpenAiAuthFlow>(
                      MakeFlowDependencies(server.Url(""), store)),
                  .oauth_base_url = server.Url(""),
              });

  const auto models = provider.ListModels(500ms);
  REQUIRE(models.size() == 1);
  REQUIRE(server.Requests().size() == 1);
}

TEST_CASE("oauth_list_models_returns_static_allowlist",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}}});

  auto provider =
      MakeProvider("http://127.0.0.1:1", store, "http://127.0.0.1:1");
  const auto models = provider.ListModels(500ms);
  const auto expected_models = StaticAcceptedModelIds();
  REQUIRE(models.size() == expected_models.size());
  for (std::size_t index = 0; index < expected_models.size(); ++index) {
    REQUIRE(models[index].id == expected_models[index]);
    REQUIRE(OpenAiChatProvider::IsOAuthModelAllowed(expected_models[index]));
  }
}

TEST_CASE("oauth_model_filter_matches_opencode_dynamic_versions",
          "[openai_chat_provider_auth]") {
  for (const auto& item : DynamicModelIdTestVectors()) {
    REQUIRE(OpenAiChatProvider::IsOAuthModelAllowed(item.id) == item.accepted);
  }
  REQUIRE_FALSE(OpenAiChatProvider::IsOAuthModelAllowed("gpt-5.4-preview"));
  REQUIRE_FALSE(OpenAiChatProvider::IsOAuthModelAllowed("gpt-5.40-preview"));
  REQUIRE_FALSE(OpenAiChatProvider::IsOAuthModelAllowed("gpt-5.10-preview"));
  REQUIRE_FALSE(OpenAiChatProvider::IsOAuthModelAllowed("gpt-"));
  REQUIRE_FALSE(OpenAiChatProvider::IsOAuthModelAllowed("gpt-5.a"));
}

TEST_CASE("expired_oauth_refreshes_before_request",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1110}},
      .account_id = "acct-old"});
  TestHttpServer server([](const HttpRequest& request,
                           std::size_t request_index) {
    if (request.path == "/oauth/token") {
      REQUIRE(request_index == 0);
      REQUIRE_THAT(request.body, ContainsSubstring("grant_type=refresh_token"));
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = std::string(R"({"access_token":")") +
                  MakeAccountIdJwtLikeToken("acct-new") +
                  R"(","refresh_token":"refresh-new","expires_in":600})"};
    }
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer " + MakeAccountIdJwtLikeToken("acct-new"));
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-new");
    return ResponsesStream("refreshed");
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 2);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "refreshed");
}

TEST_CASE("oauth_401_refreshes_once_and_retries_once",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-old"});
  TestHttpServer server([](const HttpRequest& request,
                           std::size_t request_index) {
    if (request.path == "/backend-api/codex/responses" && request_index == 0) {
      REQUIRE(request.headers.at("Authorization") == "Bearer access-old");
      REQUIRE(request.headers.at("originator") == "opencode");
      REQUIRE(request.headers.at("session_id") == DeterministicSessionId());
      const auto body = nlohmann::json::parse(request.body);
      REQUIRE(body.contains("instructions"));
      REQUIRE(body.at("instructions").is_string());
      REQUIRE_FALSE(body.at("instructions").get<std::string>().empty());
      return HttpResponse{
          .status = 401,
          .headers = {{"Content-Type", "application/json"}},
          .body = R"({"error":{"message":"expired access token"}})"};
    }
    if (request.path == "/oauth/token") {
      REQUIRE(request_index == 1);
      REQUIRE_THAT(request.body, ContainsSubstring("grant_type=refresh_token"));
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"access_token":"access-new","refresh_token":"refresh-new","expires_in":600})"};
    }
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE(request_index == 2);
    REQUIRE(request.headers.at("Authorization") == "Bearer access-new");
    REQUIRE(request.headers.at("originator") == "opencode");
    REQUIRE(request.headers.at("session_id") == DeterministicSessionId());
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-old");
    const auto body = nlohmann::json::parse(request.body);
    REQUIRE(body.contains("instructions"));
    REQUIRE(body.at("instructions").is_string());
    REQUIRE_FALSE(body.at("instructions").get<std::string>().empty());
    return ResponsesStream("retry-ok");
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 3);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "retry-ok");
}

TEST_CASE("oauth_error_detail_is_extracted", "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-stored",
      .access_token = "access-stored",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-123"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/backend-api/codex/responses");
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"detail":"Unsupported parameter: temprature"})"};
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  std::vector<chat::ChatEvent> events;
  provider.CompleteStream(
      MakeStreamingRequest(),
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      {});

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::Error);
  REQUIRE_THAT(events[0].Get<chat::ErrorEvent>().text,
               ContainsSubstring("OpenAI OAuth request failed with HTTP 400"));
  REQUIRE_THAT(events[0].Get<chat::ErrorEvent>().text,
               ContainsSubstring("Unsupported parameter: temprature"));
  REQUIRE(events[0].Get<chat::ErrorEvent>().text.find("{\"detail\"") ==
          std::string::npos);
}

TEST_CASE("revoked_refresh_token_emits_actionable_error",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiOAuthAuth{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1001}}});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/oauth/token");
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"error":"invalid_grant","error_description":"refresh token revoked"})"};
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  std::vector<chat::ChatEvent> events;
  provider.CompleteStream(
      MakeStreamingRequest(),
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      {});

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::Error);
  REQUIRE_THAT(events[0].Get<chat::ErrorEvent>().text,
               ContainsSubstring("Sign in again"));
  REQUIRE_THAT(events[0].Get<chat::ErrorEvent>().text,
               ContainsSubstring("refresh token revoked"));
  REQUIRE(events[0].Get<chat::ErrorEvent>().text.find(
              "OPENAI_API_KEY is not set") == std::string::npos);
}

}  // namespace yac::provider::test
