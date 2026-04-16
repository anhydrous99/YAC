#include "tool_call/executor.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stop_token>
#include <string>

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

std::filesystem::path TempRoot(const std::string& name) {
  auto path =
      std::filesystem::temp_directory_path() / ("yac_tool_executor_" + name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

ToolExecutor MakeExecutor(const std::filesystem::path& root) {
  return ToolExecutor(root, std::make_shared<FakeLspClient>());
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  return text;
}

}  // namespace

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
  REQUIRE(ReadFile(root / "src/new.cpp") == "one\ntwo\n");
  std::filesystem::remove_all(root);
}

TEST_CASE("ToolExecutor lists directory entries with metadata") {
  auto root = TempRoot("list");
  std::filesystem::create_directories(root / "src");
  {
    std::ofstream file(root / "src/main.cpp");
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
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
}

TEST_CASE("ToolExecutor applies LSP rename edits after approval") {
  auto root = TempRoot("rename");
  {
    std::ofstream file(root / "rename.cpp");
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
  REQUIRE(ReadFile(root / "rename.cpp") == "int next = 0;\nnext++;\n");
  std::filesystem::remove_all(root);
}
