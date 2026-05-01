#include "tool_call/lsp_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/tool_error_result.hpp"

#include <filesystem>
#include <map>

namespace yac::tool_call {

namespace {

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

}  // namespace

ToolExecutionResult ExecuteLspDiagnosticsTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  auto call = lsp_client.Diagnostics(workspace_filesystem.DisplayPath(
      workspace_filesystem.ResolvePath(RequireString(args, "file_path"))));
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

ToolExecutionResult ExecuteLspReferencesTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  auto call = lsp_client.References(
      workspace_filesystem.DisplayPath(
          workspace_filesystem.ResolvePath(RequireString(args, "file_path"))),
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

ToolExecutionResult ExecuteLspGotoDefinitionTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  auto call = lsp_client.GotoDefinition(
      workspace_filesystem.DisplayPath(
          workspace_filesystem.ResolvePath(RequireString(args, "file_path"))),
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

ToolExecutionResult ExecuteLspRenameTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  auto call = lsp_client.Rename(
      workspace_filesystem.DisplayPath(
          workspace_filesystem.ResolvePath(RequireString(args, "file_path"))),
      RequireInt(args, "line"), RequireInt(args, "character"),
      OptionalString(args, "old_name"), RequireString(args, "new_name"));
  if (call.is_error) {
    return ErrorResult(call, call.error);
  }

  std::map<std::filesystem::path, std::vector<LspTextEdit>> edits_by_file;
  for (const auto& edit : call.changes) {
    edits_by_file[workspace_filesystem.ResolvePath(edit.filepath)].push_back(
        edit);
  }

  std::map<std::filesystem::path, std::string> new_contents;
  for (auto& [path, edits] : edits_by_file) {
    auto content = WorkspaceFilesystem::ReadFile(path);
    std::ranges::sort(edits,
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
    WorkspaceFilesystem::WriteFile(path, content);
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

ToolExecutionResult ExecuteLspSymbolsTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  auto call = lsp_client.Symbols(workspace_filesystem.DisplayPath(
      workspace_filesystem.ResolvePath(RequireString(args, "file_path"))));
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

}  // namespace yac::tool_call
