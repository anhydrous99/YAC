#include "tool_call/executor.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <map>
#include <openai.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace yac::tool_call {

namespace {

using Json = openai::_detail::Json;

constexpr size_t kMaxListDirEntries = 200;
constexpr size_t kMaxContentPreviewBytes = 4000;

Json ParseArguments(const chat::ToolCallRequest& request) {
  if (request.arguments_json.empty()) {
    return Json::object();
  }
  return Json::parse(request.arguments_json);
}

std::string RequireString(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_string()) {
    throw std::runtime_error("Missing string argument '" + key + "'.");
  }
  return args[key].get<std::string>();
}

int RequireInt(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_number_integer()) {
    throw std::runtime_error("Missing integer argument '" + key + "'.");
  }
  return args[key].get<int>();
}

std::string OptionalString(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_string()) {
    return {};
  }
  return args[key].get<std::string>();
}

int CountLines(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
         (text.back() == '\n' ? 0 : 1);
}

std::string PreviewText(const std::string& text) {
  if (text.size() <= kMaxContentPreviewBytes) {
    return text;
  }
  return text.substr(0, kMaxContentPreviewBytes) + "\n...";
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Unable to open file for writing: " +
                             path.string());
  }
  file << content;
}

std::string DirectoryEntryTypeToJson(DirectoryEntryType type) {
  switch (type) {
    case DirectoryEntryType::File:
      return "file";
    case DirectoryEntryType::Directory:
      return "dir";
    case DirectoryEntryType::Other:
      return "other";
  }
  return "other";
}

std::string DiagnosticSeverityToJson(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error:
      return "error";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Information:
      return "information";
    case DiagnosticSeverity::Hint:
      return "hint";
  }
  return "information";
}

Json LocationToJson(const LspLocation& location) {
  return {{"file", location.filepath},
          {"line", location.line},
          {"character", location.character}};
}

Json TextEditToJson(const LspTextEdit& edit) {
  return {{"file", edit.filepath},
          {"start_line", edit.start_line},
          {"start_character", edit.start_character},
          {"end_line", edit.end_line},
          {"end_character", edit.end_character},
          {"new_text", edit.new_text}};
}

ToolExecutionResult ErrorResult(ToolCallBlock block, std::string message) {
  std::visit(
      [&message](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = message;
        }
      },
      block);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = Json{{"error", message}}.dump(),
      .is_error = true,
  };
}

size_t OffsetForLineCharacter(const std::string& text, int line,
                              int character) {
  const int target_line = std::max(1, line);
  const int target_character = std::max(1, character);
  size_t offset = 0;
  for (int current_line = 1; current_line < target_line && offset < text.size();
       ++current_line) {
    auto newline = text.find('\n', offset);
    if (newline == std::string::npos) {
      return text.size();
    }
    offset = newline + 1;
  }
  return std::min(text.size(),
                  offset + static_cast<size_t>(target_character - 1));
}

}  // namespace

ToolExecutor::ToolExecutor(std::filesystem::path workspace_root,
                           std::shared_ptr<ILspClient> lsp_client)
    : workspace_root_(std::filesystem::absolute(std::move(workspace_root))
                          .lexically_normal()),
      lsp_client_(std::move(lsp_client)) {}

std::vector<chat::ToolDefinition> ToolExecutor::Definitions() {
  return {
      {.name = "file_write",
       .description = "Create or fully overwrite a workspace file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"},"content":{"type":"string"}},"required":["filepath","content"]})"},
      {.name = "list_dir",
       .description = "List direct entries in a workspace directory.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"path":{"type":"string"}},"required":["path"]})"},
      {.name = "lsp_diagnostics",
       .description = "Return language-server diagnostics for a file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})"},
      {.name = "lsp_references",
       .description = "Find references to the symbol at a file position.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})"},
      {.name = "lsp_goto_definition",
       .description = "Find definitions for the symbol at a file position.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})"},
      {.name = "lsp_rename",
       .description = "Rename a symbol across the workspace.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["file_path","line","character","new_name"]})"},
      {.name = "lsp_symbols",
       .description = "Return document symbols for a file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})"},
  };
}

PreparedToolCall ToolExecutor::Prepare(const chat::ToolCallRequest& request) {
  try {
    const auto args = ParseArguments(request);
    if (request.name == "file_write") {
      const auto filepath = RequireString(args, "filepath");
      const auto content = RequireString(args, "content");
      auto block = FileWriteCall{.filepath = filepath,
                                 .content_preview = PreviewText(content),
                                 .lines_added = CountLines(content)};
      return PreparedToolCall{
          .request = request,
          .preview = std::move(block),
          .requires_approval = true,
          .approval_prompt = "Write " + filepath + " (" +
                             std::to_string(CountLines(content)) + " lines)."};
    }
    if (request.name == "list_dir") {
      const auto path = RequireString(args, "path");
      return PreparedToolCall{.request = request,
                              .preview = ListDirCall{.path = path}};
    }
    if (request.name == "lsp_diagnostics") {
      const auto file_path = RequireString(args, "file_path");
      return PreparedToolCall{
          .request = request,
          .preview = LspDiagnosticsCall{.file_path = file_path}};
    }
    if (request.name == "lsp_references") {
      const auto file_path = RequireString(args, "file_path");
      return PreparedToolCall{
          .request = request,
          .preview = LspReferencesCall{.symbol = OptionalString(args, "symbol"),
                                       .file_path = file_path}};
    }
    if (request.name == "lsp_goto_definition") {
      const auto file_path = RequireString(args, "file_path");
      return PreparedToolCall{.request = request,
                              .preview = LspGotoDefinitionCall{
                                  .symbol = OptionalString(args, "symbol"),
                                  .file_path = file_path,
                                  .line = RequireInt(args, "line"),
                                  .character = RequireInt(args, "character")}};
    }
    if (request.name == "lsp_rename") {
      const auto file_path = RequireString(args, "file_path");
      const auto new_name = RequireString(args, "new_name");
      return PreparedToolCall{
          .request = request,
          .preview = LspRenameCall{.file_path = file_path,
                                   .line = RequireInt(args, "line"),
                                   .character = RequireInt(args, "character"),
                                   .old_name = OptionalString(args, "old_name"),
                                   .new_name = new_name},
          .requires_approval = true,
          .approval_prompt =
              "Rename symbol in " + file_path + " to '" + new_name + "'."};
    }
    if (request.name == "lsp_symbols") {
      const auto file_path = RequireString(args, "file_path");
      return PreparedToolCall{
          .request = request,
          .preview = LspSymbolsCall{.file_path = file_path}};
    }
    return PreparedToolCall{
        .request = request,
        .preview = BashCall{.command = request.name,
                            .output = "Unknown tool: " + request.name,
                            .exit_code = 1,
                            .is_error = true}};
  } catch (const std::exception& error) {
    return PreparedToolCall{.request = request,
                            .preview = BashCall{.command = request.name,
                                                .output = error.what(),
                                                .exit_code = 1,
                                                .is_error = true}};
  }
}

ToolExecutionResult ToolExecutor::Execute(const PreparedToolCall& prepared,
                                          std::stop_token stop_token) const {
  if (stop_token.stop_requested()) {
    return ErrorResult(prepared.preview, "Tool execution cancelled.");
  }
  try {
    if (prepared.request.name == "file_write") {
      return ExecuteFileWrite(prepared.request);
    }
    if (prepared.request.name == "list_dir") {
      return ExecuteListDir(prepared.request);
    }
    if (prepared.request.name == "lsp_diagnostics") {
      return ExecuteLspDiagnostics(prepared.request);
    }
    if (prepared.request.name == "lsp_references") {
      return ExecuteLspReferences(prepared.request);
    }
    if (prepared.request.name == "lsp_goto_definition") {
      return ExecuteLspGotoDefinition(prepared.request);
    }
    if (prepared.request.name == "lsp_rename") {
      return ExecuteLspRename(prepared.request);
    }
    if (prepared.request.name == "lsp_symbols") {
      return ExecuteLspSymbols(prepared.request);
    }
    return ErrorResult(prepared.preview,
                       "Unknown tool: " + prepared.request.name);
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

ToolExecutionResult ToolExecutor::ExecuteFileWrite(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  const auto filepath = RequireString(args, "filepath");
  const auto content = RequireString(args, "content");
  const auto path = ResolveWorkspacePath(filepath);
  const auto old_content = ReadFile(path);
  const auto lines_removed = CountLines(old_content);
  const auto lines_added = CountLines(content);
  WriteFile(path, content);

  auto block = FileWriteCall{.filepath = DisplayPath(path),
                             .content_preview = PreviewText(content),
                             .lines_added = lines_added,
                             .lines_removed = lines_removed};
  Json result{{"filepath", block.filepath},
              {"lines_added", lines_added},
              {"lines_removed", lines_removed}};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

ToolExecutionResult ToolExecutor::ExecuteListDir(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  const auto requested_path = RequireString(args, "path");
  const auto path = ResolveWorkspacePath(requested_path);
  if (!std::filesystem::is_directory(path)) {
    throw std::runtime_error("Path is not a directory: " + requested_path);
  }

  std::vector<DirectoryEntry> entries;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    DirectoryEntryType type = DirectoryEntryType::Other;
    uintmax_t size = 0;
    if (entry.is_directory()) {
      type = DirectoryEntryType::Directory;
    } else if (entry.is_regular_file()) {
      type = DirectoryEntryType::File;
      size = entry.file_size();
    }
    entries.push_back(DirectoryEntry{
        .name = entry.path().filename().string(), .type = type, .size = size});
  }
  std::sort(entries.begin(), entries.end(),
            [](const DirectoryEntry& lhs, const DirectoryEntry& rhs) {
              if (lhs.type != rhs.type) {
                return lhs.type == DirectoryEntryType::Directory;
              }
              return lhs.name < rhs.name;
            });
  const bool truncated = entries.size() > kMaxListDirEntries;
  if (truncated) {
    entries.resize(kMaxListDirEntries);
  }

  Json result{{"path", DisplayPath(path)},
              {"truncated", truncated},
              {"entries", Json::array()}};
  for (const auto& entry : entries) {
    result["entries"].push_back({{"name", entry.name},
                                 {"type", DirectoryEntryTypeToJson(entry.type)},
                                 {"size", entry.size}});
  }
  auto block = ListDirCall{.path = DisplayPath(path),
                           .entries = std::move(entries),
                           .truncated = truncated};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

ToolExecutionResult ToolExecutor::ExecuteLspDiagnostics(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  auto call = lsp_client_->Diagnostics(
      DisplayPath(ResolveWorkspacePath(RequireString(args, "file_path"))));
  Json result{{"file_path", call.file_path}, {"diagnostics", Json::array()}};
  for (const auto& diagnostic : call.diagnostics) {
    result["diagnostics"].push_back(
        {{"severity", DiagnosticSeverityToJson(diagnostic.severity)},
         {"message", diagnostic.message},
         {"line", diagnostic.line}});
  }
  const bool is_error = call.is_error;
  return ToolExecutionResult{.block = std::move(call),
                             .result_json = result.dump(),
                             .is_error = is_error};
}

ToolExecutionResult ToolExecutor::ExecuteLspReferences(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  auto call = lsp_client_->References(
      DisplayPath(ResolveWorkspacePath(RequireString(args, "file_path"))),
      RequireInt(args, "line"), RequireInt(args, "character"),
      OptionalString(args, "symbol"));
  Json result{{"symbol", call.symbol},
              {"file_path", call.file_path},
              {"references", Json::array()}};
  for (const auto& ref : call.references) {
    result["references"].push_back(LocationToJson(ref));
  }
  const bool is_error = call.is_error;
  return ToolExecutionResult{.block = std::move(call),
                             .result_json = result.dump(),
                             .is_error = is_error};
}

ToolExecutionResult ToolExecutor::ExecuteLspGotoDefinition(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  auto call = lsp_client_->GotoDefinition(
      DisplayPath(ResolveWorkspacePath(RequireString(args, "file_path"))),
      RequireInt(args, "line"), RequireInt(args, "character"),
      OptionalString(args, "symbol"));
  Json result{{"symbol", call.symbol},
              {"file_path", call.file_path},
              {"line", call.line},
              {"character", call.character},
              {"definitions", Json::array()}};
  for (const auto& def : call.definitions) {
    result["definitions"].push_back(LocationToJson(def));
  }
  const bool is_error = call.is_error;
  return ToolExecutionResult{.block = std::move(call),
                             .result_json = result.dump(),
                             .is_error = is_error};
}

ToolExecutionResult ToolExecutor::ExecuteLspRename(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  auto call = lsp_client_->Rename(
      DisplayPath(ResolveWorkspacePath(RequireString(args, "file_path"))),
      RequireInt(args, "line"), RequireInt(args, "character"),
      OptionalString(args, "old_name"), RequireString(args, "new_name"));
  if (call.is_error) {
    return ToolExecutionResult{
        .block = call,
        .result_json = Json{{"error", call.error}}.dump(),
        .is_error = true,
    };
  }

  std::map<std::filesystem::path, std::vector<LspTextEdit>> edits_by_file;
  for (const auto& edit : call.changes) {
    edits_by_file[ResolveWorkspacePath(edit.filepath)].push_back(edit);
  }

  std::map<std::filesystem::path, std::string> new_contents;
  for (auto& [path, edits] : edits_by_file) {
    auto content = ReadFile(path);
    std::sort(edits.begin(), edits.end(),
              [](const LspTextEdit& lhs, const LspTextEdit& rhs) {
                if (lhs.start_line != rhs.start_line) {
                  return lhs.start_line > rhs.start_line;
                }
                return lhs.start_character > rhs.start_character;
              });
    for (const auto& edit : edits) {
      const auto start = OffsetForLineCharacter(content, edit.start_line,
                                                edit.start_character);
      const auto end =
          OffsetForLineCharacter(content, edit.end_line, edit.end_character);
      content.replace(start, end - start, edit.new_text);
    }
    new_contents[path] = std::move(content);
  }
  for (const auto& [path, content] : new_contents) {
    WriteFile(path, content);
  }

  Json result{{"file_path", call.file_path},
              {"old_name", call.old_name},
              {"new_name", call.new_name},
              {"changes_count", call.changes_count},
              {"changes", Json::array()}};
  for (const auto& edit : call.changes) {
    result["changes"].push_back(TextEditToJson(edit));
  }
  return ToolExecutionResult{.block = std::move(call),
                             .result_json = result.dump()};
}

ToolExecutionResult ToolExecutor::ExecuteLspSymbols(
    const chat::ToolCallRequest& request) const {
  const auto args = ParseArguments(request);
  auto call = lsp_client_->Symbols(
      DisplayPath(ResolveWorkspacePath(RequireString(args, "file_path"))));
  Json result{{"file_path", call.file_path}, {"symbols", Json::array()}};
  for (const auto& symbol : call.symbols) {
    result["symbols"].push_back(
        {{"name", symbol.name}, {"kind", symbol.kind}, {"line", symbol.line}});
  }
  const bool is_error = call.is_error;
  return ToolExecutionResult{.block = std::move(call),
                             .result_json = result.dump(),
                             .is_error = is_error};
}

std::filesystem::path ToolExecutor::ResolveWorkspacePath(
    const std::string& path) const {
  std::filesystem::path candidate(path);
  if (candidate.is_relative()) {
    candidate = workspace_root_ / candidate;
  }
  candidate = std::filesystem::absolute(candidate).lexically_normal();

  const auto root = workspace_root_.string();
  const auto value = candidate.string();
  const auto with_separator =
      root.back() == std::filesystem::path::preferred_separator
          ? root
          : root + std::filesystem::path::preferred_separator;
  if (value != root && !value.starts_with(with_separator)) {
    throw std::runtime_error("Path is outside the workspace: " + path);
  }
  return candidate;
}

std::string ToolExecutor::DisplayPath(const std::filesystem::path& path) const {
  auto normalized = std::filesystem::absolute(path).lexically_normal();
  std::error_code error;
  auto relative = std::filesystem::relative(normalized, workspace_root_, error);
  if (!error && !relative.empty() && !relative.native().starts_with("..")) {
    return relative.string();
  }
  return normalized.string();
}

}  // namespace yac::tool_call
