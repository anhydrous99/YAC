#include "tool_call/executor_catalog.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <exception>
#include <string>
#include <utility>

namespace yac::tool_call {

std::vector<chat::ToolDefinition> ToolDefinitions() {
  return {
      {.name = "file_read",
       .description = "Read the contents of a workspace file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"}},"required":["filepath"]})"},
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

PreparedToolCall PrepareToolCall(const chat::ToolCallRequest& request) {
  try {
    const auto args = ParseArguments(request);
    if (request.name == "file_write") {
      const auto filepath = RequireString(args, "filepath");
      const auto content = RequireString(args, "content");
      auto block = FileWriteCall{.filepath = filepath,
                                 .content_preview = PreviewText(content),
                                 .content_tail = TailLines(content, 3),
                                 .lines_added = CountLines(content)};
      return PreparedToolCall{
          .request = request,
          .preview = std::move(block),
          .requires_approval = true,
          .approval_prompt = "Write " + filepath + " (" +
                             std::to_string(CountLines(content)) + " lines)."};
    }
    if (request.name == "file_read") {
      const auto filepath = RequireString(args, "filepath");
      return PreparedToolCall{.request = request,
                              .preview = FileReadCall{.filepath = filepath}};
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

}  // namespace yac::tool_call
