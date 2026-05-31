#include "tool_call/web_fetch.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using yac::tool_call::FetchWebUrl;
using yac::tool_call::kWebFetchMaxBodyBytes;
using yac::tool_call::WebFetchNetworkPolicy;
using yac::tool_call::WebFetchRequest;
using yac::tool_call::WebFetchTransport;
using yac::tool_call::WebFetchTransportRequest;
using yac::tool_call::WebFetchTransportResponse;

namespace {

class FakeWebFetchTransport final : public WebFetchTransport {
 public:
  WebFetchTransportResponse Fetch(const WebFetchTransportRequest& request,
                                  std::stop_token stop_token) override {
    (void)stop_token;
    if (error.has_value()) {
      throw std::runtime_error(*error);
    }
    called = true;
    observed_url = request.url;
    observed_timeout = request.timeout;
    for (const auto& header : headers) {
      request.on_header_line(header);
    }
    for (const auto& chunk : chunks) {
      request.on_body_chunk(chunk);
    }
    return {.status_code = status_code};
  }

  bool called = false;
  std::string observed_url;
  std::chrono::milliseconds observed_timeout{0};
  long status_code = 200;
  std::optional<std::string> error;
  std::vector<std::string> headers;
  std::vector<std::string> chunks;
};

[[nodiscard]] WebFetchRequest FakeTransportRequest(
    std::string url, int timeout = 30, std::string format = "markdown") {
  return WebFetchRequest{
      .url = std::move(url),
      .format = std::move(format),
      .timeout = timeout,
      .network_policy = WebFetchNetworkPolicy::InjectedTransport};
}

}  // namespace

TEST_CASE("web_fetch fake transport returns raw response body") {
  FakeWebFetchTransport transport;
  transport.headers = {"HTTP/1.1 200 OK\r\n", "Content-Type: text/plain\r\n",
                       "Content-Length: 12\r\n"};
  transport.chunks = {"example ", "body"};

  const auto response =
      FetchWebUrl(FakeTransportRequest("https://example.test/page"), transport);

  REQUIRE(transport.called);
  REQUIRE(transport.observed_url == "https://example.test/page");
  REQUIRE(response.url == "https://example.test/page");
  REQUIRE(response.status_code == 200);
  REQUIRE(response.content_type == "text/plain");
  REQUIRE(response.body == "example body");
}

TEST_CASE("web_fetch rejects malformed and non-http urls before transport") {
  FakeWebFetchTransport transport;

  REQUIRE_THROWS_WITH(FetchWebUrl(FakeTransportRequest("not a url"), transport),
                      ContainsSubstring("Malformed URL"));
  REQUIRE_FALSE(transport.called);

  REQUIRE_THROWS_WITH(
      FetchWebUrl(FakeTransportRequest("ftp://example.com"), transport),
      ContainsSubstring("only http and https URLs are supported"));
  REQUIRE_FALSE(transport.called);
}

TEST_CASE("web_fetch real-network policy blocks private addresses") {
  FakeWebFetchTransport transport;
  const WebFetchRequest request{.url = "http://127.0.0.1/private"};

  REQUIRE_THROWS_WITH(
      FetchWebUrl(request, transport),
      ContainsSubstring("private network addresses are blocked"));
  REQUIRE_FALSE(transport.called);
}

TEST_CASE(
    "web_fetch real-network policy blocks IPv4-mapped IPv6 private addresses") {
  const std::vector<std::string> urls = {
      "http://[::ffff:127.0.0.1]/private",
      "http://[::ffff:10.0.0.1]/private",
      "http://[::ffff:169.254.1.1]/private",
  };

  for (const auto& url : urls) {
    FakeWebFetchTransport transport;
    const WebFetchRequest request{.url = url};

    REQUIRE_THROWS_WITH(
        FetchWebUrl(request, transport),
        ContainsSubstring("private network addresses are blocked"));
    REQUIRE_FALSE(transport.called);
  }
}

TEST_CASE("web_fetch rejects userinfo URLs before transport") {
  FakeWebFetchTransport transport;

  REQUIRE_THROWS_WITH(
      FetchWebUrl(FakeTransportRequest("https://user@example.test/page"),
                  transport),
      ContainsSubstring("Malformed URL"));
  REQUIRE_FALSE(transport.called);
}

TEST_CASE("web_fetch real-network policy blocks additional private ranges") {
  const std::vector<std::string> urls = {
      "http://0.0.0.1/private",
      "http://100.64.0.1/private",
      "http://[fc00::1]/private",
  };

  for (const auto& url : urls) {
    FakeWebFetchTransport transport;
    const WebFetchRequest request{.url = url};

    REQUIRE_THROWS_WITH(
        FetchWebUrl(request, transport),
        ContainsSubstring("private network addresses are blocked"));
    REQUIRE_FALSE(transport.called);
  }
}

TEST_CASE("web_fetch clamps timeout to 120 seconds") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/plain\r\n"};
  transport.chunks = {"ok"};

  (void)FetchWebUrl(FakeTransportRequest("https://example.test/page", 999),
                    transport);

  REQUIRE(transport.observed_timeout == std::chrono::seconds(120));
}

TEST_CASE("web_fetch default timeout is 30 seconds") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/plain\r\n"};
  transport.chunks = {"ok"};

  (void)FetchWebUrl(FakeTransportRequest("https://example.test/page"),
                    transport);

  REQUIRE(transport.observed_timeout == std::chrono::seconds(30));
}

TEST_CASE("web_fetch rejects declared content length above five megabytes") {
  FakeWebFetchTransport transport;
  transport.headers = {
      "Content-Length: " + std::to_string(kWebFetchMaxBodyBytes + 1) + "\r\n"};

  REQUIRE_THROWS_WITH(
      FetchWebUrl(FakeTransportRequest("https://example.test/page"), transport),
      ContainsSubstring("response exceeded 5242880 bytes"));
}

TEST_CASE("web_fetch allows declared content length at five megabytes") {
  FakeWebFetchTransport transport;
  transport.headers = {
      "Content-Type: text/plain\r\n",
      "Content-Length: " + std::to_string(kWebFetchMaxBodyBytes) + "\r\n"};
  transport.chunks = {"within cap"};

  const auto response =
      FetchWebUrl(FakeTransportRequest("https://example.test/page"), transport);

  REQUIRE(response.body == "within cap");
}

TEST_CASE("web_fetch rejects streamed body above five megabytes") {
  FakeWebFetchTransport transport;
  transport.chunks = {std::string(kWebFetchMaxBodyBytes, 'a'), "b"};

  REQUIRE_THROWS_WITH(
      FetchWebUrl(FakeTransportRequest("https://example.test/page"), transport),
      ContainsSubstring("response exceeded 5242880 bytes"));
}

TEST_CASE("web_fetch execution result carries raw body json") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/plain\r\n"};
  transport.chunks = {"example body"};

  const auto result = yac::tool_call::ExecuteWebFetchTool(
      FakeTransportRequest("https://example.test/page"), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE_FALSE(result.is_error);
  REQUIRE(payload["url"] == "https://example.test/page");
  REQUIRE(payload["body"] == "example body");
  REQUIRE(payload["status_code"] == 200);
}

TEST_CASE("web_fetch markdown transform removes hidden html content") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/html; charset=utf-8\r\n"};
  transport.chunks = {
      "<html><head><meta name=\"x\" content=\"y\"><link href=\"x\">"
      "<style>p{color:red}</style></head><body><h1>Hello</h1>"
      "<script>alert(1)</script><p>World <a href=\"/next\">Next</a></p>"
      "<ul><li>One</li><li>Two</li></ul><table><tr><td>A</td><td>B</td></tr>"
      "</table></body></html>"};

  const auto response = FetchWebUrl(
      FakeTransportRequest("https://example.test/page", 30, "markdown"),
      transport);

  REQUIRE(response.body.find("# Hello") != std::string::npos);
  REQUIRE(response.body.find("World [Next](/next)") != std::string::npos);
  REQUIRE(response.body.find("- One") != std::string::npos);
  REQUIRE(response.body.find("A | B") != std::string::npos);
  REQUIRE(response.body.find("alert(1)") == std::string::npos);
  REQUIRE(response.body.find("color:red") == std::string::npos);
}

TEST_CASE("web_fetch markdown transform preserves visible table text") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/html\r\n"};
  transport.chunks = {
      "<main><h2>Report</h2><table><tr><th>Name</th><th>Status</th></tr>"
      "<tr><td>Fetch</td><td>Ready</td></tr></table></main>"};

  const auto response = FetchWebUrl(
      FakeTransportRequest("https://example.test/page", 30, "markdown"),
      transport);

  REQUIRE(response.body.find("## Report") != std::string::npos);
  REQUIRE(response.body.find("Name | Status") != std::string::npos);
  REQUIRE(response.body.find("Fetch | Ready") != std::string::npos);
}

TEST_CASE("web_fetch text transform returns visible html text only") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/html\r\n"};
  transport.chunks = {
      "<h1>Hello</h1><script>alert(1)</script><style>.x{}</style>"
      "<noscript>hidden</noscript><p>World &amp; friends</p>"};

  const auto response = FetchWebUrl(
      FakeTransportRequest("https://example.test/page", 30, "text"), transport);

  REQUIRE(response.body.find("Hello") != std::string::npos);
  REQUIRE(response.body.find("World & friends") != std::string::npos);
  REQUIRE(response.body.find("alert(1)") == std::string::npos);
  REQUIRE(response.body.find(".x") == std::string::npos);
  REQUIRE(response.body.find("hidden") == std::string::npos);
}

TEST_CASE("web_fetch html transform preserves html without script execution") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: text/html\r\n"};
  transport.chunks = {"<h1>Hello</h1><script>alert(1)</script><p>World</p>"};

  const auto response = FetchWebUrl(
      FakeTransportRequest("https://example.test/page", 30, "html"), transport);

  REQUIRE(response.body ==
          "<h1>Hello</h1><script>alert(1)</script><p>World</p>");
}

TEST_CASE(
    "web_fetch rejects unsupported binary content type deterministically") {
  FakeWebFetchTransport transport;
  transport.headers = {"Content-Type: image/png\r\n"};
  transport.chunks = {"\x89PNG"};

  const auto result = yac::tool_call::ExecuteWebFetchTool(
      FakeTransportRequest("https://example.test/image.png"), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(std::string(payload["error"])
              .find("web_fetch cannot transform content type") !=
          std::string::npos);
}

TEST_CASE("web_fetch rejects non-success HTTP status") {
  FakeWebFetchTransport transport;
  transport.status_code = 404;
  transport.chunks = {"missing"};

  const auto result = yac::tool_call::ExecuteWebFetchTool(
      FakeTransportRequest("https://example.test/missing"), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(std::string(payload["error"]).find("web_fetch HTTP 404") !=
          std::string::npos);
}

TEST_CASE("web_fetch normalizes transport timeout errors") {
  FakeWebFetchTransport transport;
  transport.error = "Operation TimedOut";

  const auto result = yac::tool_call::ExecuteWebFetchTool(
      FakeTransportRequest("https://example.test/slow"), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(std::string(payload["error"]).find("timeout") != std::string::npos);
}
