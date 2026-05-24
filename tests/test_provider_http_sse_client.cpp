#include "openai_auth_test_helpers.hpp"
#include "provider/http_sse_client.hpp"

#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

using yac::provider::internal::HttpStreamPostRequest;
using yac::provider::internal::PerformHttpStreamPost;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::TestHttpServer;

}  // namespace

TEST_CASE("HttpStreamPost sends payload and streams response body") {
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.method == "POST");
    REQUIRE(request.path == "/stream");
    REQUIRE(request.headers.at("Content-Type") == "application/json");
    REQUIRE(request.headers.at("Accept") == "text/event-stream");
    REQUIRE(request.headers.at("X-Test") == "present");
    REQUIRE(request.body == R"({"hello":"world"})");
    return HttpResponse{.headers = {{"Content-Type", "text/event-stream"}},
                        .body = "data: one\n\ndata: two\n\n"};
  });

  std::string body;
  std::string status_line;
  const auto response = PerformHttpStreamPost(
      HttpStreamPostRequest{
          .url = server.Url("/stream"),
          .payload = R"({"hello":"world"})",
          .headers = {"Content-Type: application/json",
                      "Accept: text/event-stream", "X-Test: present"},
          .on_header_line =
              [&status_line](std::string_view line) {
                if (line.starts_with("HTTP/")) {
                  status_line = std::string(line);
                }
              },
          .on_body_chunk =
              [&body](std::string_view chunk) { body.append(chunk); }},
      {});

  REQUIRE_FALSE(response.cancelled);
  REQUIRE(response.status_code == 200);
  REQUIRE(status_line.starts_with("HTTP/1.1 200"));
  REQUIRE(body == "data: one\n\ndata: two\n\n");
  REQUIRE(server.Requests().size() == 1);
}

TEST_CASE("HttpStreamPost reports HTTP error status and body") {
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/limited");
    return HttpResponse{.status = 429,
                        .headers = {{"Content-Type", "application/json"}},
                        .body = R"({"error":"rate limited"})"};
  });

  std::string body;
  const auto response = PerformHttpStreamPost(
      HttpStreamPostRequest{
          .url = server.Url("/limited"),
          .payload = "{}",
          .headers = {"Content-Type: application/json"},
          .on_body_chunk =
              [&body](std::string_view chunk) { body.append(chunk); }},
      {});

  REQUIRE_FALSE(response.cancelled);
  REQUIRE(response.status_code == 429);
  REQUIRE(body == R"({"error":"rate limited"})");
}

TEST_CASE(
    "HttpStreamPost returns cancelled before sending when already stopped") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    FAIL("cancelled request should not reach server");
    return HttpResponse{};
  });

  std::stop_source stop_source;
  stop_source.request_stop();
  bool body_called = false;
  const auto response = PerformHttpStreamPost(
      HttpStreamPostRequest{
          .url = server.Url("/cancelled"),
          .payload = "{}",
          .headers = {"Content-Type: application/json"},
          .on_body_chunk =
              [&body_called](std::string_view) { body_called = true; }},
      stop_source.get_token());

  REQUIRE(response.cancelled);
  REQUIRE_FALSE(body_called);
  REQUIRE(server.Requests().empty());
}

TEST_CASE("HttpStreamPost surfaces server disconnect errors") {
  TestHttpServer server([](const HttpRequest&, std::size_t) -> HttpResponse {
    throw std::runtime_error("drop connection");
  });

  REQUIRE_THROWS_AS(
      PerformHttpStreamPost(HttpStreamPostRequest{
                                .url = server.Url("/disconnect"),
                                .payload = "{}",
                                .headers = {"Content-Type: application/json"}},
                            {}),
      std::runtime_error);
}
