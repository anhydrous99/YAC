#include "mcp/protocol_constants.hpp"
#include "mcp/stdio_mcp_transport.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace std::chrono_literals;

namespace yac::mcp::test {
namespace {

namespace pc = protocol;

class TempFile {
 public:
  explicit TempFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove(path_);
  }

  ~TempFile() { std::filesystem::remove(path_); }

  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] McpServerConfig MakeConfig(std::vector<std::string> args = {}) {
  return McpServerConfig{
      .id = "echo-server",
      .transport = "stdio",
      .command = ECHO_JSONRPC_SERVER_PATH,
      .args = std::move(args),
  };
}

[[nodiscard]] bool WaitForSubstring(const std::filesystem::path& path,
                                    std::string_view needle,
                                    std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    if (buffer.str().find(needle) != std::string::npos) {
      return true;
    }
    // SLEEP-RATIONALE: let server thread start before sending data
    std::this_thread::sleep_for(10ms);
  }

  std::ifstream stream(path);
  std::stringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str().find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("request_response") {
  StdioMcpTransport transport(MakeConfig());
  transport.Start();

  const Json response = transport.SendRequest(pc::kMethodPing, Json::object(),
                                              1s, std::stop_token{});

  REQUIRE(transport.Status() == TransportStatus::Ready);
  REQUIRE(response[std::string(pc::kFieldId)] == 1);
  REQUIRE(response.contains(std::string(pc::kFieldResult)));
  REQUIRE(response[std::string(pc::kFieldResult)]["ok"] == true);
  REQUIRE(response[std::string(pc::kFieldResult)]["echoMethod"] ==
          std::string(pc::kMethodPing));

  transport.Stop(std::stop_token{});
}

TEST_CASE("cancellation_emits_notification") {
  TempFile log_file("yac_test_mcp_stdio_transport_cancel.log");
  StdioMcpTransport transport(MakeConfig(
      {"--slow-mode", "--log-frames-to=" + log_file.Path().string()}));
  transport.Start();

  std::jthread request_thread([&transport](std::stop_token) {
    try {
      (void)transport.SendRequest(pc::kMethodPing, Json::object(), 5s,
                                  std::stop_token{});
    } catch (const std::exception&) {
      // Test cancellation path: SendRequest is expected to throw on stop.
    }
  });

  // SLEEP-RATIONALE: let server process the request before checking state
  std::this_thread::sleep_for(100ms);
  transport.Stop(std::stop_token{});
  request_thread.join();

  REQUIRE(WaitForSubstring(log_file.Path(),
                           std::string(pc::kMethodNotificationsCancelled), 1s));
  REQUIRE(
      WaitForSubstring(log_file.Path(), std::string(pc::kFieldRequestId), 1s));
}

TEST_CASE("embedded_newline_rejected") {
  McpServerConfig config{
      .id = "invalid-frame",
      .transport = "stdio",
      .command = "/bin/sh",
      .args = {"-c",
               "printf "
               "'{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\n\"ok\":true}}\n'"},
  };
  StdioMcpTransport transport(std::move(config));
  transport.Start();

  REQUIRE_THROWS_WITH(
      transport.SendRequest(pc::kMethodPing, Json::object(), 1s,
                            std::stop_token{}),
      Catch::Matchers::ContainsSubstring("Invalid MCP stdio JSON frame"));
}

}  // namespace yac::mcp::test
