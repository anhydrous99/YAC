#include "openai_auth_test_helpers.hpp"
#include "provider/openai_auth_flow.hpp"
#include "provider/openai_auth_store.hpp"
#include "provider/openai_chat_provider.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

using Catch::Matchers::ContainsSubstring;
using namespace std::chrono_literals;

namespace yac::test {
namespace {

using yac::tests::openai_auth::DeterministicSessionId;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::MakeAccountIdJwtLikeToken;
using yac::tests::openai_auth::TestHttpServer;

constexpr std::string_view kStoredApiKey = "sk-test-e2e-api-key";
constexpr std::string_view kStoredRefreshToken = "refresh-test-e2e";
constexpr std::string_view kStoredAccessToken = "access-test-e2e";
constexpr std::string_view kRefreshedRefreshToken = "refresh-rotated-e2e";
constexpr std::string_view kYacCodexBaseInstructions =
    "You are YAC, a terminal coding assistant. Help the user with software "
    "engineering tasks while following the available tools, workspace "
    "instructions, and safety constraints.";
constexpr auto kCliTimeout = 30s;
constexpr auto kCliPollInterval = 20ms;

std::string GetInstructions(const HttpRequest& request) {
  return nlohmann::json::parse(request.body).at("instructions").get<std::string>();
}

class TempDir {
 public:
  TempDir() {
#ifndef _WIN32
    std::string tmpl =
        (std::filesystem::temp_directory_path() / "yac_openai_auth_e2e_XXXXXX")
            .string();
    const char* result = ::mkdtemp(tmpl.data());
    if (result == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path_ = result;
#else
    path_ = std::filesystem::temp_directory_path() / "yac_openai_auth_e2e";
    std::filesystem::create_directories(path_);
#endif
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::optional<std::string> value)
      : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      previous_ = std::string(current);
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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.is_open());
  output << content;
}

std::filesystem::path SettingsPath(const std::filesystem::path& home_dir) {
  return home_dir / ".yac" / "settings.toml";
}

std::filesystem::path AuthPath(const std::filesystem::path& home_dir) {
  return home_dir / ".yac" / "provider" / "auth" / "openai.json";
}

struct ChildProcessResult {
  int exit_code = -1;
  std::string output;
};

void SetNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    throw std::runtime_error("fcntl(F_GETFL) failed");
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw std::runtime_error("fcntl(F_SETFL) failed");
  }
}

void DrainAvailableOutput(int fd, std::string& output) {
  std::array<char, 4096> buffer{};
  for (;;) {
    const ssize_t count = ::read(fd, buffer.data(), buffer.size());
    if (count > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return;
    }
    throw std::runtime_error("read failed");
  }
}

ChildProcessResult RunYacCli(const std::filesystem::path& home_dir,
                             const std::vector<std::string>& args,
                             std::string_view stdin_content = {}) {
  std::array<int, 2> in_pipe{};
  std::array<int, 2> out_pipe{};
  if (::pipe(in_pipe.data()) != 0 || ::pipe(out_pipe.data()) != 0) {
    throw std::runtime_error("pipe failed");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(out_pipe[1], STDERR_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);

    ::setenv("HOME", home_dir.c_str(), 1);
    ::unsetenv("OPENAI_API_KEY");
    ::unsetenv("YAC_PROVIDER");
    ::unsetenv("YAC_BASE_URL");
    ::unsetenv("YAC_API_KEY_ENV");
    ::setenv("YAC_OPENAI_AUTH_STORE", "file", 1);
    ::unsetenv("DBUS_SESSION_BUS_ADDRESS");

    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.emplace_back(YAC_BINARY_PATH);
    argv_storage.insert(argv_storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& arg : argv_storage) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  ::close(in_pipe[0]);
  ::close(out_pipe[1]);

  if (!stdin_content.empty()) {
    std::size_t written = 0;
    while (written < stdin_content.size()) {
      const ssize_t count = ::write(in_pipe[1], stdin_content.data() + written,
                                    stdin_content.size() - written);
      if (count <= 0) {
        break;
      }
      written += static_cast<std::size_t>(count);
    }
  }
  ::close(in_pipe[1]);

  SetNonBlocking(out_pipe[0]);
  std::string output;
  const auto deadline = std::chrono::steady_clock::now() + kCliTimeout;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    DrainAvailableOutput(out_pipe[0], output);
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      DrainAvailableOutput(out_pipe[0], output);
      ::close(out_pipe[0]);
      if (WIFEXITED(status) != 0) {
        return {.exit_code = WEXITSTATUS(status), .output = std::move(output)};
      }
      return {.exit_code = -1, .output = std::move(output)};
    }
    if (result < 0 && errno != EINTR) {
      ::close(out_pipe[0]);
      throw std::runtime_error("waitpid failed");
    }
    std::this_thread::sleep_for(kCliPollInterval);
  }

  ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  DrainAvailableOutput(out_pipe[0], output);
  ::close(out_pipe[0]);
  output += "\nError: yac CLI timed out after 30 seconds\n";
  return {.exit_code = -1, .output = std::move(output)};
}

std::shared_ptr<yac::provider::OpenAiAuthStore> MakeStoreForHome(
    const std::filesystem::path& home_dir) {
  ScopedEnvVar home("HOME", home_dir.string());
  return std::make_shared<yac::provider::OpenAiAuthStore>(
      yac::provider::OpenAiAuthStore::Dependencies{
          .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
          .file_backend =
              std::make_shared<yac::provider::OpenAiFileAuthBackend>(),
      });
}

std::shared_ptr<yac::provider::OpenAiAuthFlow> MakeFlow(
    std::string issuer_url,
    const std::shared_ptr<yac::provider::OpenAiAuthStore>& store,
    std::chrono::system_clock::time_point now) {
  return std::make_shared<yac::provider::OpenAiAuthFlow>(
      yac::provider::OpenAiAuthFlow::Dependencies{
          .issuer_url = std::move(issuer_url),
          .clock = [now] { return now; },
          .auth_store = store,
      });
}

yac::provider::OpenAiChatProvider MakeProvider(
    const std::string& base_url,
    const std::shared_ptr<yac::provider::OpenAiAuthFlow>& flow,
    const std::string& oauth_base_url = {}) {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};
  config.model = ::yac::ModelId{"gpt-5.4"};
  config.base_url = base_url;
  config.api_key_env = "OPENAI_API_KEY";
  return yac::provider::OpenAiChatProvider(
      config,
      yac::provider::OpenAiChatProvider::Dependencies{
          .auth_flow = flow,
          .oauth_base_url = oauth_base_url.empty() ? base_url : oauth_base_url,
      });
}

yac::chat::ChatRequest MakeStreamingRequest(
    std::optional<std::string_view> responses_instructions) {
  yac::chat::ChatRequest request;
  request.model = ::yac::ModelId{"gpt-5.4"};
  request.session_id = DeterministicSessionId();
  request.stream = true;
  if (responses_instructions.has_value()) {
    request.responses_instructions = *responses_instructions;
  }
  request.messages = {yac::chat::ChatMessage{.role = yac::chat::ChatRole::User,
                                              .content = "hello"}};
  return request;
}

yac::chat::ChatRequest MakeStreamingRequest() {
  return MakeStreamingRequest("Follow the mock OAuth instructions.");
}

std::vector<yac::chat::ChatEvent> RunStream(
    yac::provider::OpenAiChatProvider& provider,
    yac::chat::ChatRequest request) {
  std::vector<yac::chat::ChatEvent> events;
  provider.CompleteStream(std::move(request),
                          [&events](yac::chat::ChatEvent event) {
                            events.push_back(std::move(event));
                          },
                          {});
  return events;
}

std::vector<yac::chat::ChatEvent> RunStream(
    yac::provider::OpenAiChatProvider& provider) {
  return RunStream(provider, MakeStreamingRequest());
}

HttpResponse ApiStream(std::string_view text) {
  return HttpResponse{
      .headers = {{"Content-Type", "text/event-stream"}},
      .body = std::string(R"(data: {"choices":[{"delta":{"content":")") +
              std::string(text) +
              R"("}}]}
data: {"usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}}
)",
  };
}

HttpResponse OAuthStream(std::string_view text) {
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

std::string VisibleOutput(const std::vector<yac::chat::ChatEvent>& events) {
  std::string output;
  for (const auto& event : events) {
    if (event.Type() == yac::chat::ChatEventType::TextDelta) {
      output += event.Get<yac::chat::TextDeltaEvent>().text;
    }
    if (event.Type() == yac::chat::ChatEventType::Error) {
      output += event.Get<yac::chat::ErrorEvent>().text;
    }
  }
  return output;
}

TEST_CASE("cli_api_key_status_logout_use_temp_home_and_redact_output",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);
  ScopedEnvVar provider_env("YAC_PROVIDER", std::nullopt);
  ScopedEnvVar base_url_env("YAC_BASE_URL", std::nullopt);
  ScopedEnvVar api_key_env_name("YAC_API_KEY_ENV", std::nullopt);

  WriteFile(SettingsPath(temp_dir.Path()),
            "[provider]\n"
            "id = \"openai\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");

  const auto set_result =
      RunYacCli(temp_dir.Path(), {"auth", "openai", "set-api-key", "--stdin"},
                std::string(kStoredApiKey) + "\n");
  REQUIRE(set_result.exit_code == 0);
  REQUIRE_THAT(set_result.output, ContainsSubstring("Stored OpenAI API key."));
  CHECK(set_result.output.find(kStoredApiKey) == std::string::npos);
  REQUIRE(std::filesystem::exists(AuthPath(temp_dir.Path())));

  const auto status_result =
      RunYacCli(temp_dir.Path(), {"auth", "openai", "status"});
  REQUIRE(status_result.exit_code == 0);
  CHECK_THAT(status_result.output,
             ContainsSubstring("configured provider: openai"));
  CHECK_THAT(status_result.output, ContainsSubstring("stored credential: api"));
  CHECK_THAT(status_result.output,
             ContainsSubstring("effective auth: api (stored)"));
  CHECK(status_result.output.find(kStoredApiKey) == std::string::npos);

  const auto logout_result =
      RunYacCli(temp_dir.Path(), {"auth", "openai", "logout"});
  REQUIRE(logout_result.exit_code == 0);
  CHECK_THAT(logout_result.output, ContainsSubstring("Logged out: openai"));
  CHECK(logout_result.output.find(kStoredApiKey) == std::string::npos);
  CHECK_FALSE(std::filesystem::exists(AuthPath(temp_dir.Path())));

  const std::string combined_output =
      set_result.output + status_result.output + logout_result.output;
  CHECK(combined_output.find(kStoredApiKey) == std::string::npos);
  CHECK(combined_output.find(kStoredRefreshToken) == std::string::npos);
  CHECK(combined_output.find(kStoredAccessToken) == std::string::npos);
}

TEST_CASE("stored_api_key_runtime_hits_mock_openai_endpoint",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);

  const auto store = MakeStoreForHome(temp_dir.Path());
  static_cast<void>(store->Save(
      yac::provider::OpenAiApiKeyAuth{.key = std::string(kStoredApiKey)}));
  const auto flow = MakeFlow(
      "http://127.0.0.1:1", store,
      std::chrono::system_clock::time_point{std::chrono::seconds{1000}});

  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/chat/completions");
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer " + std::string(kStoredApiKey));
    return ApiStream("api-ok");
  });

  auto provider = MakeProvider(server.Url(""), flow);
  const auto events = RunStream(provider);

  REQUIRE(server.Requests().size() == 1);
  REQUIRE_FALSE(events.empty());
  CHECK(events.front().Type() == yac::chat::ChatEventType::TextDelta);
  CHECK(VisibleOutput(events) == "api-ok");
  CHECK(VisibleOutput(events).find(kStoredApiKey) == std::string::npos);
}

TEST_CASE("stored_oauth_runtime_uses_mock_codex_endpoint",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);

  const auto store = MakeStoreForHome(temp_dir.Path());
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = std::string(kStoredRefreshToken),
      .access_token = std::string(kStoredAccessToken),
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = std::string("acct-e2e"),
  }));
  const auto flow = MakeFlow(
      "http://127.0.0.1:1", store,
      std::chrono::system_clock::time_point{std::chrono::seconds{1000}});

  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    const std::string instructions = GetInstructions(request);
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer " + std::string(kStoredAccessToken));
    REQUIRE(request.headers.at("originator") == "opencode");
    REQUIRE_THAT(request.headers.at("User-Agent"),
                 ContainsSubstring("opencode/"));
    REQUIRE_THAT(request.headers.at("User-Agent"), ContainsSubstring("("));
    REQUIRE_THAT(request.headers.at("User-Agent"), ContainsSubstring("; "));
    REQUIRE(request.headers.at("session_id") == DeterministicSessionId());
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-e2e");
    CHECK(instructions == std::string(kYacCodexBaseInstructions) +
                            "\n\nFollow the mock OAuth instructions.");
    CHECK(instructions.find("OpenCode") == std::string::npos);
    REQUIRE(request.body.find("\"role\":\"system\"") == std::string::npos);
    return OAuthStream("oauth-ok");
  });

  auto provider = MakeProvider("http://127.0.0.1:1", flow, server.Url(""));
  const auto events = RunStream(provider);

  REQUIRE(server.Requests().size() == 1);
  REQUIRE_FALSE(events.empty());
  CHECK(events.front().Type() == yac::chat::ChatEventType::TextDelta);
  CHECK(VisibleOutput(events) == "oauth-ok");
  CHECK(VisibleOutput(events).find(kStoredAccessToken) == std::string::npos);
  CHECK(VisibleOutput(events).find(kStoredRefreshToken) == std::string::npos);
}

TEST_CASE("stored_oauth_runtime_uses_base_instructions_without_caller_prompt",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);

  const auto store = MakeStoreForHome(temp_dir.Path());
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = std::string(kStoredRefreshToken),
      .access_token = std::string(kStoredAccessToken),
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = std::string("acct-e2e"),
  }));
  const auto flow = MakeFlow(
      "http://127.0.0.1:1", store,
      std::chrono::system_clock::time_point{std::chrono::seconds{1000}});

  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    const std::string instructions = GetInstructions(request);
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer " + std::string(kStoredAccessToken));
    REQUIRE(request.headers.at("originator") == "opencode");
    REQUIRE_THAT(request.headers.at("User-Agent"),
                 ContainsSubstring("opencode/"));
    REQUIRE(request.headers.at("session_id") == DeterministicSessionId());
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-e2e");
    CHECK(instructions == std::string(kYacCodexBaseInstructions));
    CHECK(instructions.find("OpenCode") == std::string::npos);
    CHECK(request.body.find("Follow the mock OAuth instructions.") ==
          std::string::npos);
    REQUIRE(request.body.find("\"role\":\"system\"") == std::string::npos);
    return OAuthStream("oauth-ok");
  });

  auto provider = MakeProvider("http://127.0.0.1:1", flow, server.Url(""));
  const auto events = RunStream(provider, MakeStreamingRequest(std::nullopt));

  REQUIRE(server.Requests().size() == 1);
  REQUIRE_FALSE(events.empty());
  CHECK(events.front().Type() == yac::chat::ChatEventType::TextDelta);
  CHECK(VisibleOutput(events) == "oauth-ok");
  CHECK(VisibleOutput(events).find(kStoredAccessToken) == std::string::npos);
  CHECK(VisibleOutput(events).find(kStoredRefreshToken) == std::string::npos);
}

TEST_CASE("expired_oauth_refreshes_then_streams_without_leaking_tokens",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);

  const auto store = MakeStoreForHome(temp_dir.Path());
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = std::string(kStoredRefreshToken),
      .access_token = std::string(kStoredAccessToken),
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1110}},
      .account_id = std::string("acct-old"),
  }));

  const auto refreshed_access = MakeAccountIdJwtLikeToken("acct-new");
  TestHttpServer server([&refreshed_access](const HttpRequest& request,
                                            std::size_t request_index) {
    if (request.path == "/oauth/token") {
      REQUIRE(request_index == 0);
      REQUIRE_THAT(request.body, ContainsSubstring("grant_type=refresh_token"));
      REQUIRE_THAT(request.body,
                   ContainsSubstring("refresh_token=" +
                                     std::string(kStoredRefreshToken)));
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = std::string(R"({"access_token":")") + refreshed_access +
                  std::string(R"(","refresh_token":")") +
                  std::string(kRefreshedRefreshToken) +
                  R"(","expires_in":600})",
      };
    }
    const std::string instructions = GetInstructions(request);
    REQUIRE(request.path == "/backend-api/codex/responses");
    REQUIRE(request_index == 1);
    REQUIRE(request.headers.at("Authorization") ==
            "Bearer " + refreshed_access);
    REQUIRE(request.headers.at("originator") == "opencode");
    REQUIRE_THAT(request.headers.at("User-Agent"),
                 ContainsSubstring("opencode/"));
    REQUIRE_THAT(request.headers.at("User-Agent"), ContainsSubstring("; "));
    REQUIRE(request.headers.at("session_id") == DeterministicSessionId());
    REQUIRE(request.headers.at("ChatGPT-Account-Id") == "acct-new");
    CHECK(instructions == std::string(kYacCodexBaseInstructions) +
                            "\n\nFollow the mock OAuth instructions.");
    CHECK(instructions.find("OpenCode") == std::string::npos);
    REQUIRE(request.body.find("\"role\":\"system\"") == std::string::npos);
    return OAuthStream("refresh-ok");
  });

  const auto flow = MakeFlow(
      server.Url(""), store,
      std::chrono::system_clock::time_point{std::chrono::seconds{1000}});
  auto provider = MakeProvider("http://127.0.0.1:1", flow, server.Url(""));
  const auto events = RunStream(provider);

  REQUIRE(server.Requests().size() == 2);
  CHECK(VisibleOutput(events) == "refresh-ok");
  CHECK(VisibleOutput(events).find(kStoredRefreshToken) == std::string::npos);
  CHECK(VisibleOutput(events).find(kStoredAccessToken) == std::string::npos);
  CHECK(VisibleOutput(events).find(refreshed_access) == std::string::npos);
  CHECK(VisibleOutput(events).find(kRefreshedRefreshToken) ==
        std::string::npos);

  const auto reloaded = store->Load();
  REQUIRE(reloaded.has_value());
  const auto* oauth =
      std::get_if<yac::provider::OpenAiOAuthAuth>(&reloaded->auth);
  REQUIRE(oauth != nullptr);
  CHECK(oauth->refresh_token == kRefreshedRefreshToken);
  CHECK(oauth->access_token == refreshed_access);
  CHECK(oauth->account_id == std::optional<std::string>{"acct-new"});

  const std::string auth_file = ReadFile(AuthPath(temp_dir.Path()));
  CHECK(auth_file.find(kStoredRefreshToken) == std::string::npos);
  CHECK(auth_file.find(kStoredAccessToken) == std::string::npos);
}

TEST_CASE("expired_oauth_refresh_failure_uses_only_mock_endpoint",
          "[openai_auth_e2e]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar api_key_env("OPENAI_API_KEY", std::nullopt);

  const auto store = MakeStoreForHome(temp_dir.Path());
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = std::string(kStoredRefreshToken),
      .access_token = std::string(kStoredAccessToken),
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1110}},
      .account_id = std::string("acct-old"),
  }));

  TestHttpServer server([](const HttpRequest& request, std::size_t index) {
    REQUIRE(index == 0);
    REQUIRE(request.path == "/oauth/token");
    REQUIRE_THAT(request.body, ContainsSubstring("grant_type=refresh_token"));
    REQUIRE_THAT(
        request.body,
        ContainsSubstring("refresh_token=" + std::string(kStoredRefreshToken)));
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"error":"invalid_grant","error_description":"mock refresh denied"})",
    };
  });

  const auto flow = MakeFlow(
      server.Url(""), store,
      std::chrono::system_clock::time_point{std::chrono::seconds{1000}});
  auto provider = MakeProvider("http://127.0.0.1:1", flow, server.Url(""));
  const auto events = RunStream(provider);

  REQUIRE(server.Requests().size() == 1);
  const std::string output = VisibleOutput(events);
  CHECK_THAT(output, ContainsSubstring("mock refresh denied"));
  CHECK(output.find(kStoredRefreshToken) == std::string::npos);
  CHECK(output.find(kStoredAccessToken) == std::string::npos);
}

}  // namespace
}  // namespace yac::test
