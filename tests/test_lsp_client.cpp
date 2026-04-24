#include "tool_call/lsp_client.hpp"

#include <chrono>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::JsonRpcLspClient;
using yac::tool_call::LspServerConfig;

// Regression test for the reader-death-on-child-exit hang: before the fault
// mechanism was added, a dead child process left the condition-variable
// waiter in SendRequest blocked for the full request_timeout. This test
// exercises that path by pointing the client at /bin/false, which exits 1
// immediately and therefore never responds to the initialize request.
TEST_CASE("JsonRpcLspClient returns promptly when the server child exits") {
  LspServerConfig config;
  config.command = "/bin/false";
  config.workspace_root = std::filesystem::temp_directory_path();
  // Much longer than the expected prompt-error path so a regression (reader
  // hang) would make the test take the full 5s and clearly diverge.
  config.request_timeout = std::chrono::milliseconds(5000);
  config.diagnostics_timeout = std::chrono::milliseconds(500);

  JsonRpcLspClient client(std::move(config));

  const auto start = std::chrono::steady_clock::now();
  const auto result = client.Diagnostics("nonexistent.cpp");
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(result.is_error);
  REQUIRE(elapsed < std::chrono::milliseconds(2000));
}
