#include "openai_auth_test_helpers.hpp"
#include "provider/openai_chat_provider.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
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

using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::MakeAccountIdJwtLikeToken;
using yac::tests::openai_auth::TestHttpServer;

class MemoryAuthBackend : public IOpenAiAuthBackend {
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

class ThrowingKeychainBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Erase() override {
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }
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

[[nodiscard]] chat::ChatRequest MakeStreamingRequest() {
  chat::ChatRequest request;
  request.model = ::yac::ModelId{"gpt-5.4"};
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

TEST_CASE("stored_api_key_is_used_for_openai_runtime",
          "[openai_chat_provider_auth]") {
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  (void)store->Save(OpenAiApiKeyAuth{.key = "stored-api-key"});
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/chat/completions");
    REQUIRE(request.headers.at("Authorization") == "Bearer stored-api-key");
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
    REQUIRE(request.headers.at("Authorization") == "Bearer access-stored");
    REQUIRE(request.headers.at("originator") == "yac");
    REQUIRE(request.headers.at("User-Agent") == "yac/0.1.0");
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-123");
    return ResponsesStream();
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 1);
  REQUIRE(events[0].Type() == chat::ChatEventType::TextDelta);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "hello");
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
  REQUIRE(models.size() == 6);
  REQUIRE(models[0].id == "gpt-5.5");
  REQUIRE(models[5].id == "gpt-5.4-mini");
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
    return ResponsesStream("retry-ok");
  });

  auto provider = MakeProvider(server.Url(""), store, server.Url(""));
  const auto events = RunStream(provider);
  REQUIRE(server.Requests().size() == 3);
  REQUIRE(events[0].Get<chat::TextDeltaEvent>().text == "retry-ok");
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
