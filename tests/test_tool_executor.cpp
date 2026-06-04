#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/todo_state.hpp"
#include "tool_call/web_fetch.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::tool_call;

namespace {

class FakeLspClient : public ILspClient {
 public:
  LspDiagnosticsCall Diagnostics(const std::string& file_path) override {
    return LspDiagnosticsCall{
        .file_path = file_path,
        .diagnostics = {{DiagnosticSeverity::Warning, "unused", 2}}};
  }

  LspReferencesCall References(const std::string& file_path, int line,
                               int character,
                               const std::string& symbol) override {
    return LspReferencesCall{.symbol = symbol,
                             .file_path = file_path,
                             .references = {{file_path, line, character}}};
  }

  LspGotoDefinitionCall GotoDefinition(const std::string& file_path, int line,
                                       int character,
                                       const std::string& symbol) override {
    return LspGotoDefinitionCall{.symbol = symbol,
                                 .file_path = file_path,
                                 .line = line,
                                 .character = character,
                                 .definitions = {{file_path, 1, 1}}};
  }

  LspRenameCall Rename(const std::string& file_path, int line, int character,
                       const std::string& old_name,
                       const std::string& new_name) override {
    return LspRenameCall{.file_path = file_path,
                         .line = line,
                         .character = character,
                         .old_name = old_name,
                         .new_name = new_name,
                         .changes_count = 2,
                         .changes = {{file_path, 1, 5, 1, 8, new_name},
                                     {file_path, 2, 1, 2, 4, new_name}}};
  }

  LspSymbolsCall Symbols(const std::string& file_path) override {
    return LspSymbolsCall{.file_path = file_path,
                          .symbols = {{"main", "function", 3}}};
  }
};

class ErrorRenameLspClient : public FakeLspClient {
 public:
  LspRenameCall Rename(const std::string& file_path, int line, int character,
                       const std::string& old_name,
                       const std::string& new_name) override {
    return LspRenameCall{.file_path = file_path,
                         .line = line,
                         .character = character,
                         .old_name = old_name,
                         .new_name = new_name,
                         .is_error = true,
                         .error = "rename failed"};
  }
};

class OutsideWorkspaceRenameLspClient : public FakeLspClient {
 public:
  LspRenameCall Rename(const std::string& file_path, int line, int character,
                       const std::string& old_name,
                       const std::string& new_name) override {
    return LspRenameCall{.file_path = file_path,
                         .line = line,
                         .character = character,
                         .old_name = old_name,
                         .new_name = new_name,
                         .changes_count = 1,
                         .changes = {{"../outside.cpp", 1, 1, 1, 4, new_name}}};
  }
};

class FakeWebFetchTransport final : public WebFetchTransport {
 public:
  WebFetchTransportResponse Fetch(const WebFetchTransportRequest& request,
                                  std::stop_token stop_token) override {
    (void)stop_token;
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
  std::vector<std::string> headers{"Content-Type: text/plain\r\n"};
  std::vector<std::string> chunks{"executor body"};
};

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
    return response;
  }

  bool called = false;
  std::string observed_endpoint;
  std::string observed_api_key;
  std::string observed_body;
  std::chrono::milliseconds observed_timeout{0};
  WebSearchTransportResponse response{
      .status_code = 200,
      .content_type = "application/json",
      .body = Json{{"results", Json::array()}}.dump()};
};

class TempRoot {
 public:
  explicit TempRoot(const std::string& name)
      : path_(std::filesystem::temp_directory_path() /
              ("yac_tool_executor_" + name)) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempRoot() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempRoot(const TempRoot&) = delete;
  TempRoot& operator=(const TempRoot&) = delete;
  TempRoot(TempRoot&&) = delete;
  TempRoot& operator=(TempRoot&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class TestToolExecutor {
 public:
  explicit TestToolExecutor(const std::filesystem::path& root,
                            std::shared_ptr<ILspClient> lsp_client =
                                std::make_shared<FakeLspClient>())
      : executor_(root, std::move(lsp_client), todo_state_) {}

  [[nodiscard]] ToolExecutionResult Execute(const PreparedToolCall& prepared,
                                            std::stop_token stop_token) const {
    return executor_.Execute(prepared, stop_token);
  }

  void SetWebFetchTransport(WebFetchTransport* transport) {
    executor_.SetWebFetchTransport(transport);
  }

  void SetWebSearchConfig(const WebSearchConfig& config) {
    executor_.SetWebSearchConfig(config);
  }

  void SetWebSearchTransport(WebSearchTransport* transport) {
    executor_.SetWebSearchTransport(transport);
  }

 private:
  TodoState todo_state_;
  ToolExecutor executor_;
};

TestToolExecutor MakeExecutor(const TempRoot& root) {
  return TestToolExecutor(root.Path());
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  return text;
}

}  // namespace

TEST_CASE("ToolExecutor dispatch registry unknown returns error") {
  auto root = TempRoot("unknown");
  auto executor = MakeExecutor(root);
  PreparedToolCall prepared{
      .request = {.id = "call_1", .name = "does_not_exist"},
      .preview = FileWriteCall{.filepath = "x.txt"}};

  auto result = executor.Execute(prepared, std::stop_token{});

  REQUIRE(result.is_error);
  const auto json = Json::parse(result.result_json);
  REQUIRE(json["error"] == "Unknown tool: does_not_exist");
  REQUIRE(json["tool_name"] == "does_not_exist");
  REQUIRE(json["received_arguments"].get<std::string>().empty());
  REQUIRE_FALSE(json.contains("expected_schema"));
  REQUIRE(std::holds_alternative<FileWriteCall>(result.block));
  REQUIRE(std::get<FileWriteCall>(result.block).is_error);
  REQUIRE(std::get<FileWriteCall>(result.block).error ==
          "Unknown tool: does_not_exist");
}

TEST_CASE("ToolExecutor handler registry keys match tool definitions") {
  REQUIRE(ToolHandlerCount() == ToolExecutor::Definitions().size());
  for (const auto& definition : ToolExecutor::Definitions()) {
    const auto* handler = LookupToolHandler(definition.name);
    REQUIRE(handler != nullptr);
    REQUIRE(handler->prepare != nullptr);
    REQUIRE(handler->execute != nullptr);
  }
}

TEST_CASE("ToolExecutor executes web_fetch through injected transport") {
  auto root = TempRoot("web_fetch");
  auto executor = MakeExecutor(root);
  FakeWebFetchTransport transport;
  executor.SetWebFetchTransport(&transport);
  ToolCallRequest request{
      .id = "call_1",
      .name = "web_fetch",
      .arguments_json =
          R"({"url":"https://example.test/page","format":"text"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  const auto payload = Json::parse(result.result_json);
  REQUIRE(payload["body"] == "executor body");
  REQUIRE(transport.called);
  REQUIRE(transport.observed_url == "https://example.test/page");
  REQUIRE(transport.observed_timeout == std::chrono::seconds(30));
}

TEST_CASE("ToolExecutor returns normalized web_fetch errors without crashing") {
  auto root = TempRoot("web_fetch_error");
  auto executor = MakeExecutor(root);
  FakeWebFetchTransport transport;
  executor.SetWebFetchTransport(&transport);
  ToolCallRequest request{.id = "call_1",
                          .name = "web_fetch",
                          .arguments_json = R"({"url":"not a url"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
  const auto payload = Json::parse(result.result_json);
  REQUIRE(std::string(payload["error"]).find("Malformed URL") !=
          std::string::npos);
  REQUIRE_FALSE(transport.called);
}

TEST_CASE("ToolExecutor applies configured web_search defaults") {
  auto root = TempRoot("web_search_defaults");
  auto executor = MakeExecutor(root);
  FakeWebSearchTransport transport;
  executor.SetWebSearchTransport(&transport);
  executor.SetWebSearchConfig(
      WebSearchConfig{.enabled = true,
                      .provider = "exa",
                      .endpoint = "https://exa.test/search",
                      .api_key = "fake-exa-key",
                      .timeout_seconds = 30,
                      .result_limit = 7,
                      .context_limit = 9000});
  ToolCallRequest request{.id = "call_1",
                          .name = "web_search",
                          .arguments_json = R"({"query":"yac terminal"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(transport.called);
  REQUIRE(transport.observed_endpoint == "https://exa.test/search");
  REQUIRE(transport.observed_api_key == "fake-exa-key");
  REQUIRE(transport.observed_timeout == std::chrono::seconds(30));
  const auto observed_body = Json::parse(transport.observed_body);
  REQUIRE(observed_body["num_results"] == 7);
  REQUIRE(observed_body["context_max_characters"] == 9000);
  const auto& call = std::get<WebSearchCall>(result.block);
  REQUIRE(call.num_results == 7);
  REQUIRE(call.context_max_characters == 9000);
}

TEST_CASE("ToolExecutor keeps explicit web_search limits authoritative") {
  auto root = TempRoot("web_search_explicit");
  auto executor = MakeExecutor(root);
  FakeWebSearchTransport transport;
  executor.SetWebSearchTransport(&transport);
  executor.SetWebSearchConfig(
      WebSearchConfig{.enabled = true,
                      .provider = "exa",
                      .endpoint = "https://exa.test/search",
                      .api_key = "fake-exa-key",
                      .timeout_seconds = 30,
                      .result_limit = 7,
                      .context_limit = 9000});
  ToolCallRequest request{
      .id = "call_1",
      .name = "web_search",
      .arguments_json =
          R"({"query":"yac terminal","num_results":12,"context_max_characters":13000})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(transport.called);
  const auto observed_body = Json::parse(transport.observed_body);
  REQUIRE(observed_body["num_results"] == 10);
  REQUIRE(observed_body["context_max_characters"] == 12000);
  const auto& call = std::get<WebSearchCall>(result.block);
  REQUIRE(call.num_results == 10);
  REQUIRE(call.context_max_characters == 12000);
}

TEST_CASE("ToolExecutor file_write requires approval") {
  auto root = TempRoot("write_prepare");
  ToolCallRequest request{
      .id = "call_1",
      .name = "file_write",
      .arguments_json = R"({"filepath":"src/new.cpp","content":"one\ntwo\n"})"};

  auto prepared = ToolExecutor::Prepare(request);

  REQUIRE(prepared.requires_approval);
  REQUIRE(prepared.approval_prompt == "Write src/new.cpp (2 lines).");
}

TEST_CASE("ToolExecutor writes files inside the workspace") {
  auto root = TempRoot("write");
  auto executor = MakeExecutor(root);
  ToolCallRequest request{
      .id = "call_1",
      .name = "file_write",
      .arguments_json = R"({"filepath":"src/new.cpp","content":"one\ntwo\n"})"};

  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE(prepared.requires_approval);
  auto result = executor.Execute(prepared, std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(std::holds_alternative<FileWriteCall>(result.block));
  REQUIRE(ReadFile(root.Path() / "src/new.cpp") == "one\ntwo\n");
}

TEST_CASE("ToolExecutor lists directory entries with metadata") {
  auto root = TempRoot("list");
  std::filesystem::create_directories(root.Path() / "src");
  {
    std::ofstream file(root.Path() / "src/main.cpp");
    file << "int main() {}\n";
  }
  auto executor = MakeExecutor(root);
  ToolCallRequest request{.id = "call_1",
                          .name = "list_dir",
                          .arguments_json = R"({"path":"src"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  const auto& call = std::get<ListDirCall>(result.block);
  REQUIRE(call.entries.size() == 1);
  REQUIRE(call.entries[0].name == "main.cpp");
  REQUIRE(call.entries[0].type == DirectoryEntryType::File);
}

TEST_CASE("ToolExecutor rejects paths outside the workspace") {
  auto root = TempRoot("outside");
  auto executor = MakeExecutor(root);
  ToolCallRequest request{.id = "call_1",
                          .name = "list_dir",
                          .arguments_json = R"({"path":"../"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
}

TEST_CASE("ToolExecutor applies LSP rename edits after approval") {
  auto root = TempRoot("rename");
  {
    std::ofstream file(root.Path() / "rename.cpp");
    file << "int old = 0;\nold++;\n";
  }
  auto executor = MakeExecutor(root);
  ToolCallRequest request{
      .id = "call_1",
      .name = "lsp_rename",
      .arguments_json =
          R"({"file_path":"rename.cpp","line":1,"character":5,"old_name":"old","new_name":"next"})"};

  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE(prepared.requires_approval);
  auto result = executor.Execute(prepared, std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(ReadFile(root.Path() / "rename.cpp") == "int next = 0;\nnext++;\n");
}

TEST_CASE("ToolExecutor reports cancelled execution before running tool") {
  auto root = TempRoot("cancelled");
  auto executor = MakeExecutor(root);
  ToolCallRequest request{
      .id = "call_1", .name = "list_dir", .arguments_json = R"({"path":"."})"};
  auto prepared = ToolExecutor::Prepare(request);
  std::stop_source stop_source;
  stop_source.request_stop();

  auto result = executor.Execute(prepared, stop_source.get_token());

  REQUIRE(result.is_error);
  const auto& call = std::get<ListDirCall>(result.block);
  REQUIRE(call.is_error);
  REQUIRE(call.error == "Tool execution cancelled.");
}

TEST_CASE("ToolExecutor preserves LSP rename errors from the client seam") {
  auto root = TempRoot("rename_error");
  {
    std::ofstream file(root.Path() / "rename.cpp");
    file << "int old = 0;\nold++;\n";
  }
  TestToolExecutor executor(root.Path(),
                            std::make_shared<ErrorRenameLspClient>());
  ToolCallRequest request{
      .id = "call_1",
      .name = "lsp_rename",
      .arguments_json =
          R"({"file_path":"rename.cpp","line":1,"character":5,"old_name":"old","new_name":"next"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
  const auto& call = std::get<LspRenameCall>(result.block);
  REQUIRE(call.is_error);
  REQUIRE(call.error == "rename failed");
  REQUIRE(ReadFile(root.Path() / "rename.cpp") == "int old = 0;\nold++;\n");
}

TEST_CASE("ToolExecutor rejects LSP rename edits outside the workspace") {
  auto root = TempRoot("rename_outside");
  {
    std::ofstream file(root.Path() / "rename.cpp");
    file << "old\n";
  }
  TestToolExecutor executor(
      root.Path(), std::make_shared<OutsideWorkspaceRenameLspClient>());
  ToolCallRequest request{
      .id = "call_1",
      .name = "lsp_rename",
      .arguments_json =
          R"({"file_path":"rename.cpp","line":1,"character":1,"old_name":"old","new_name":"next"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
  const auto& call = std::get<LspRenameCall>(result.block);
  REQUIRE(call.is_error);
  REQUIRE(call.error.find("Path is outside the workspace") !=
          std::string::npos);
  REQUIRE(ReadFile(root.Path() / "rename.cpp") == "old\n");
}

TEST_CASE("ToolExecutor reads files inside the workspace") {
  auto root = TempRoot("read");
  std::filesystem::create_directories(root.Path() / "src");
  {
    std::ofstream file(root.Path() / "src/hello.cpp");
    file << "line one\nline two\nline three\n";
  }
  auto executor = MakeExecutor(root);
  ToolCallRequest request{.id = "call_1",
                          .name = "file_read",
                          .arguments_json = R"({"filepath":"src/hello.cpp"})"};

  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE_FALSE(prepared.requires_approval);
  auto result = executor.Execute(prepared, std::stop_token{});

  REQUIRE_FALSE(result.is_error);
  REQUIRE(std::holds_alternative<FileReadCall>(result.block));
  const auto& call = std::get<FileReadCall>(result.block);
  REQUIRE(call.filepath == "src/hello.cpp");
  REQUIRE(call.lines_loaded == 3);
  REQUIRE(result.result_json.find("line one\\nline two\\nline three\\n") !=
          std::string::npos);
}

TEST_CASE("ToolExecutor reports error for missing file") {
  auto root = TempRoot("read_missing");
  auto executor = MakeExecutor(root);
  ToolCallRequest request{
      .id = "call_1",
      .name = "file_read",
      .arguments_json = R"({"filepath":"does_not_exist.txt"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
}

TEST_CASE("ToolExecutor rejects file_read paths outside the workspace") {
  auto root = TempRoot("read_outside");
  auto executor = MakeExecutor(root);
  ToolCallRequest request{
      .id = "call_1",
      .name = "file_read",
      .arguments_json = R"({"filepath":"../../etc/passwd"})"};

  auto result =
      executor.Execute(ToolExecutor::Prepare(request), std::stop_token{});

  REQUIRE(result.is_error);
}

TEST_CASE("WorkspaceFilesystem::WriteFile rejects content over the size cap") {
  auto root = TempRoot("write_cap");
  std::string content(kMaxFileBytes + 1, 'x');
  REQUIRE_THROWS_AS(
      WorkspaceFilesystem::WriteFile(root.Path() / "big.bin", content),
      std::runtime_error);
  REQUIRE_FALSE(std::filesystem::exists(root.Path() / "big.bin"));
}

TEST_CASE("WorkspaceFilesystem::ReadFile rejects files over the size cap") {
  auto root = TempRoot("read_cap");
  const auto path = root.Path() / "big.bin";
  {
    std::ofstream(path, std::ios::binary).put('\0');
  }
  std::error_code ec;
  std::filesystem::resize_file(path, kMaxFileBytes + 1, ec);
  REQUIRE_FALSE(ec);
  REQUIRE_THROWS_AS(WorkspaceFilesystem::ReadFile(path), std::runtime_error);
}

TEST_CASE(
    "WorkspaceFilesystem::WriteFile surfaces an error on read-only parent") {
  auto root = TempRoot("write_readonly");
  const auto target_dir = root.Path() / "locked";
  std::filesystem::create_directories(target_dir);
  std::filesystem::permissions(
      target_dir,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
      std::filesystem::perm_options::replace);

  bool threw = false;
  try {
    WorkspaceFilesystem::WriteFile(target_dir / "denied.txt", "hello");
  } catch (const std::runtime_error&) {
    threw = true;
  }

  std::filesystem::permissions(target_dir, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  REQUIRE(threw);
}
