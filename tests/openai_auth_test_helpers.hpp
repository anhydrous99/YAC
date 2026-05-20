#pragma once

#include "../src/mcp/token_store.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::tests::openai_auth {

struct HttpRequest {
  std::string method;
  std::string path;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

class TestHttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&, std::size_t)>;

  explicit TestHttpServer(Handler handler);
  ~TestHttpServer();

  TestHttpServer(const TestHttpServer&) = delete;
  TestHttpServer& operator=(const TestHttpServer&) = delete;
  TestHttpServer(TestHttpServer&&) = delete;
  TestHttpServer& operator=(TestHttpServer&&) = delete;

  [[nodiscard]] std::string Url(std::string_view path) const;
  [[nodiscard]] std::vector<HttpRequest> Requests() const;

 private:
  [[nodiscard]] static HttpRequest ReadRequest(int client_fd);
  static void WriteResponse(int client_fd, const HttpResponse& response);
  void Run(std::stop_token stop_token);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class FakeBrowserLauncher {
 public:
  bool LaunchBrowser(std::string_view url);

  [[nodiscard]] std::vector<std::string> Urls() const;

 private:
  mutable std::mutex mutex_;
  std::vector<std::string> urls_;
};

class FakeTokenStore : public yac::mcp::ITokenStore {
 public:
  [[nodiscard]] std::optional<std::string> Get(
      std::string_view server_id) const override;

  void Set(std::string_view server_id, std::string_view token_json) override;

  void Erase(std::string_view server_id) override;

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> tokens_;
};

class FakeKeychainTokenStore : public FakeTokenStore {};
class FakeFileTokenStore : public FakeTokenStore {};

[[nodiscard]] std::string MakeUnsignedJwtLikeToken(
    std::string_view payload_json);

[[nodiscard]] std::string MakeAccountIdJwtLikeToken(
    std::string_view account_id);

}  // namespace yac::tests::openai_auth
