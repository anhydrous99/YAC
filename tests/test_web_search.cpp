#include "tool_call/web_search.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using yac::tool_call::BuildExaWebSearchRequestBody;
using yac::tool_call::ExecuteWebSearchTool;
using yac::tool_call::ParseExaWebSearchResponse;
using yac::tool_call::SearchWeb;
using yac::tool_call::WebSearchProviderConfig;
using yac::tool_call::WebSearchRequest;
using yac::tool_call::WebSearchTransport;
using yac::tool_call::WebSearchTransportRequest;
using yac::tool_call::WebSearchTransportResponse;

namespace {

class FakeWebSearchTransport final : public WebSearchTransport {
 public:
  WebSearchTransportResponse Search(const WebSearchTransportRequest& request,
                                    std::stop_token stop_token) override {
    (void)stop_token;
    called = true;
    observed_endpoint = request.endpoint;
    observed_api_key = request.api_key;
    observed_body = request.body;
    observed_timeout = request.timeout;
    if (error.has_value()) {
      throw std::runtime_error(*error);
    }
    return response;
  }

  bool called = false;
  std::string observed_endpoint;
  std::string observed_api_key;
  std::string observed_body;
  std::chrono::milliseconds observed_timeout{0};
  WebSearchTransportResponse response{.status_code = 200,
                                      .content_type = "application/json"};
  std::optional<std::string> error;
};

[[nodiscard]] WebSearchProviderConfig FakeConfig() {
  return {.endpoint = "https://exa.test/search",
          .api_key = "exa-secret-token",
          .timeout_seconds = 25};
}

[[nodiscard]] std::string TwoResultJson() {
  return nlohmann::json{{"results", nlohmann::json::array({
                                        {{"title", "First"},
                                         {"url", "https://example.test/first"},
                                         {"text", "first snippet"}},
                                        {{"title", "Second"},
                                         {"url", "https://example.test/second"},
                                         {"snippet", "second snippet"}},
                                    })}}
      .dump();
}

}  // namespace

TEST_CASE("web_search builds Exa-compatible request payload fields") {
  const auto body = nlohmann::json::parse(BuildExaWebSearchRequestBody(
      WebSearchRequest{.query = "yac",
                       .num_results = 999,
                       .context_max_characters = 999999}));

  REQUIRE(body["query"] == "yac");
  REQUIRE(body["num_results"] == 10);
  REQUIRE(body["context_max_characters"] == 12000);
}

TEST_CASE("web_search plain JSON response preserves provider order") {
  FakeWebSearchTransport transport;
  transport.response.body = TwoResultJson();

  const auto response =
      SearchWeb(WebSearchRequest{.query = "yac"}, FakeConfig(), transport);

  REQUIRE(transport.called);
  REQUIRE(transport.observed_endpoint == "https://exa.test/search");
  REQUIRE(transport.observed_api_key == "exa-secret-token");
  const auto observed_body = nlohmann::json::parse(transport.observed_body);
  REQUIRE(observed_body["query"] == "yac");
  REQUIRE(observed_body["num_results"] == 5);
  REQUIRE(observed_body["context_max_characters"] == 4096);
  REQUIRE(transport.observed_timeout == std::chrono::seconds(25));
  REQUIRE(response.results.size() == 2);
  REQUIRE(response.results[0].title == "First");
  REQUIRE(response.results[0].url == "https://example.test/first");
  REQUIRE(response.results[0].snippet == "first snippet");
  REQUIRE(response.results[1].title == "Second");
  REQUIRE(response.results[1].url == "https://example.test/second");
  REQUIRE(response.results[1].snippet == "second snippet");
}

TEST_CASE("web_search SSE data frames ignore done and normalize results") {
  const std::string body = "data: " + TwoResultJson() + "\n\ndata: [DONE]\n\n";

  const auto response = ParseExaWebSearchResponse(body, "text/event-stream");

  REQUIRE(response.results.size() == 2);
  REQUIRE(response.results[0].title == "First");
  REQUIRE(response.results[1].title == "Second");
}

TEST_CASE("web_search malformed provider JSON is deterministic") {
  FakeWebSearchTransport transport;
  transport.response.body = "{not-json";

  const auto result = ExecuteWebSearchTool(WebSearchRequest{.query = "yac"},
                                           FakeConfig(), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(payload["error"] == "web_search provider returned invalid response");
}

TEST_CASE("web_search provider error is normalized and redacted") {
  FakeWebSearchTransport transport;
  transport.response.body =
      nlohmann::json{{"error", {{"message", "Bearer exa-secret-token denied"}}}}
          .dump();

  const auto result = ExecuteWebSearchTool(WebSearchRequest{.query = "yac"},
                                           FakeConfig(), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(std::string(payload["error"]).find("exa-secret-token") ==
          std::string::npos);
  REQUIRE_THAT(std::string(payload["error"]), ContainsSubstring("[REDACTED]"));
}

TEST_CASE("web_search timeout-like provider failures are deterministic") {
  FakeWebSearchTransport transport;
  transport.error = "upstream TimedOut with Bearer exa-secret-token";

  const auto result = ExecuteWebSearchTool(WebSearchRequest{.query = "yac"},
                                           FakeConfig(), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE(result.is_error);
  REQUIRE(payload["error"] == "web_search request timed out");
}

TEST_CASE("web_search accepts empty result arrays") {
  FakeWebSearchTransport transport;
  transport.response.body =
      nlohmann::json{{"results", nlohmann::json::array()}}.dump();

  const auto result = ExecuteWebSearchTool(WebSearchRequest{.query = "yac"},
                                           FakeConfig(), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE_FALSE(result.is_error);
  REQUIRE(payload["provider"] == "exa");
  REQUIRE(payload["metadata"]["provider"] == "exa");
  REQUIRE_THAT(std::string(payload["output"]),
               ContainsSubstring("No search results found"));
  REQUIRE(payload["results"].is_array());
  REQUIRE(payload["results"].empty());
}

TEST_CASE("web_search stable output contains ordered titles and URLs") {
  FakeWebSearchTransport transport;
  transport.response.body = TwoResultJson();

  const auto result = ExecuteWebSearchTool(WebSearchRequest{.query = "yac"},
                                           FakeConfig(), transport);
  const auto payload = nlohmann::json::parse(result.result_json);

  REQUIRE_FALSE(result.is_error);
  REQUIRE(payload["provider"] == "exa");
  REQUIRE(payload["metadata"]["provider"] == "exa");
  const std::string output = payload["output"];
  REQUIRE(output.find("First") != std::string::npos);
  REQUIRE(output.find("https://example.test/first") != std::string::npos);
  REQUIRE(output.find("Second") != std::string::npos);
  REQUIRE(output.find("https://example.test/second") != std::string::npos);
  REQUIRE(output.find("First") < output.find("Second"));
}
