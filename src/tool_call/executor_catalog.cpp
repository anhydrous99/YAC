#include "tool_call/executor_catalog.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::tool_call {

namespace {

using PrepareFn = PreparedToolCall (*)(const chat::ToolCallRequest&,
                                       const Json& args);

PreparedToolCall PrepareFileWriteTool(const chat::ToolCallRequest& request,
                                      const Json& args) {
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

PreparedToolCall PrepareFileReadTool(const chat::ToolCallRequest& request,
                                     const Json& args) {
  const auto filepath = RequireString(args, "filepath");
  return PreparedToolCall{.request = request,
                          .preview = FileReadCall{.filepath = filepath}};
}

PreparedToolCall PrepareListDirTool(const chat::ToolCallRequest& request,
                                    const Json& args) {
  const auto path = RequireString(args, "path");
  return PreparedToolCall{.request = request,
                          .preview = ListDirCall{.path = path}};
}

PreparedToolCall PrepareLspDiagnosticsTool(const chat::ToolCallRequest& request,
                                           const Json& args) {
  const auto file_path = RequireString(args, "file_path");
  return PreparedToolCall{
      .request = request,
      .preview = LspDiagnosticsCall{.file_path = file_path}};
}

PreparedToolCall PrepareLspReferencesTool(const chat::ToolCallRequest& request,
                                          const Json& args) {
  const auto file_path = RequireString(args, "file_path");
  return PreparedToolCall{
      .request = request,
      .preview = LspReferencesCall{.symbol = OptionalString(args, "symbol"),
                                   .file_path = file_path}};
}

PreparedToolCall PrepareLspGotoDefinitionTool(
    const chat::ToolCallRequest& request, const Json& args) {
  const auto file_path = RequireString(args, "file_path");
  return PreparedToolCall{.request = request,
                          .preview = LspGotoDefinitionCall{
                              .symbol = OptionalString(args, "symbol"),
                              .file_path = file_path,
                              .line = RequireInt(args, "line"),
                              .character = RequireInt(args, "character")}};
}

PreparedToolCall PrepareLspRenameTool(const chat::ToolCallRequest& request,
                                      const Json& args) {
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

PreparedToolCall PrepareLspSymbolsTool(const chat::ToolCallRequest& request,
                                       const Json& args) {
  const auto file_path = RequireString(args, "file_path");
  return PreparedToolCall{.request = request,
                          .preview = LspSymbolsCall{.file_path = file_path}};
}

PreparedToolCall PrepareSubAgentTool(const chat::ToolCallRequest& request,
                                     const Json& args) {
  const auto task = RequireString(args, "task");
  const auto mode_str = OptionalString(args, "mode");
  const auto mode = (mode_str == "background") ? SubAgentMode::Background
                                               : SubAgentMode::Foreground;
  return PreparedToolCall{
      .request = request,
      .preview =
          SubAgentCall{
              .task = task, .mode = mode, .status = SubAgentStatus::Pending},
      .requires_approval = false};
}

PreparedToolCall PrepareTodoWriteTool(const chat::ToolCallRequest& request,
                                      const Json& args) {
  const auto todos_json = args.contains("todos") && args["todos"].is_array()
                              ? args["todos"]
                              : Json::array();
  std::vector<TodoItem> todos;
  todos.reserve(todos_json.size());
  for (const auto& item : todos_json) {
    todos.push_back(
        TodoItem{.content = item.value("content", std::string{}),
                 .status = item.value("status", std::string{"pending"}),
                 .priority = item.value("priority", std::string{"medium"})});
  }
  return PreparedToolCall{.request = request,
                          .preview = TodoWriteCall{.todos = std::move(todos)},
                          .requires_approval = false};
}

PreparedToolCall PrepareAskUserTool(const chat::ToolCallRequest& request,
                                    const Json& args) {
  const auto question = RequireString(args, "question");
  const auto options_json =
      args.contains("options") && args["options"].is_array() ? args["options"]
                                                             : Json::array();
  std::vector<std::string> options;
  options.reserve(options_json.size());
  for (const auto& opt : options_json) {
    if (opt.is_string()) {
      options.push_back(opt.get<std::string>());
    }
  }
  return PreparedToolCall{.request = request,
                          .preview = AskUserCall{.question = question,
                                                 .options = std::move(options)},
                          .requires_approval = true,
                          .approval_prompt = question};
}

PreparedToolCall PrepareBashTool(const chat::ToolCallRequest& request,
                                 const Json& args) {
  const auto command = RequireString(args, "command");
  std::string preview = command;
  if (preview.size() > 120) {
    preview.resize(120);
    preview += "...";
  }
  return PreparedToolCall{.request = request,
                          .preview = BashCall{.command = command},
                          .requires_approval = true,
                          .approval_prompt = "Execute: " + preview};
}

PreparedToolCall PrepareFileEditTool(const chat::ToolCallRequest& request,
                                     const Json& args) {
  const auto filepath = RequireString(args, "filepath");
  const auto old_string = RequireString(args, "old_string");
  const auto new_string = RequireString(args, "new_string");
  std::string preview = "Edit " + filepath + ": replace \"";
  preview +=
      old_string.size() > 40 ? old_string.substr(0, 40) + "..." : old_string;
  preview += "\" -> \"";
  preview +=
      new_string.size() > 40 ? new_string.substr(0, 40) + "..." : new_string;
  preview += "\"";
  return PreparedToolCall{.request = request,
                          .preview = FileEditCall{.filepath = filepath},
                          .requires_approval = true,
                          .approval_prompt = std::move(preview)};
}

PreparedToolCall PrepareGrepTool(const chat::ToolCallRequest& request,
                                 const Json& args) {
  const auto pattern = RequireString(args, "pattern");
  return PreparedToolCall{.request = request,
                          .preview = GrepCall{.pattern = pattern},
                          .requires_approval = false};
}

PreparedToolCall PrepareGlobTool(const chat::ToolCallRequest& request,
                                 const Json& args) {
  const auto pattern = RequireString(args, "pattern");
  return PreparedToolCall{.request = request,
                          .preview = GlobCall{.pattern = pattern},
                          .requires_approval = false};
}

using PrepareRegistry = std::unordered_map<std::string_view, PrepareFn>;

const PrepareRegistry kPrepareRegistry = {
    {kFileWriteToolName, &PrepareFileWriteTool},
    {kFileReadToolName, &PrepareFileReadTool},
    {kListDirToolName, &PrepareListDirTool},
    {kLspDiagnosticsToolName, &PrepareLspDiagnosticsTool},
    {kLspReferencesToolName, &PrepareLspReferencesTool},
    {kLspGotoDefinitionToolName, &PrepareLspGotoDefinitionTool},
    {kLspRenameToolName, &PrepareLspRenameTool},
    {kLspSymbolsToolName, &PrepareLspSymbolsTool},
    {kSubAgentToolName, &PrepareSubAgentTool},
    {kTodoWriteToolName, &PrepareTodoWriteTool},
    {kAskUserToolName, &PrepareAskUserTool},
    {kBashToolName, &PrepareBashTool},
    {kFileEditToolName, &PrepareFileEditTool},
    {kGrepToolName, &PrepareGrepTool},
    {kGlobToolName, &PrepareGlobTool},
};

}  // namespace

bool HasToolExecutorPrepareRegistryEntry(std::string_view name) {
  return kPrepareRegistry.contains(name);
}

std::size_t ToolExecutorPrepareRegistrySize() {
  return kPrepareRegistry.size();
}

std::vector<chat::ToolDefinition> ToolDefinitions() {
  return {
      {.name = std::string(kFileReadToolName),
       .description = "Read the contents of a workspace file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"}},"required":["filepath"]})"},
      {.name = std::string(kFileWriteToolName),
       .description = "Create or fully overwrite a workspace file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"},"content":{"type":"string"}},"required":["filepath","content"]})"},
      {.name = std::string(kListDirToolName),
       .description = "List direct entries in a workspace directory.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"path":{"type":"string"}},"required":["path"]})"},
      {.name = std::string(kLspDiagnosticsToolName),
       .description = "Return language-server diagnostics for a file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})"},
      {.name = std::string(kLspReferencesToolName),
       .description = "Find references to the symbol at a file position.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})"},
      {.name = std::string(kLspGotoDefinitionToolName),
       .description = "Find definitions for the symbol at a file position.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})"},
      {.name = std::string(kLspRenameToolName),
       .description = "Rename a symbol across the workspace.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["file_path","line","character","new_name"]})"},
      {.name = std::string(kLspSymbolsToolName),
       .description = "Return document symbols for a file.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})"},
      {.name = std::string(kSubAgentToolName),
       .description =
           "Spawn a sub-agent to perform a task in an isolated "
           "conversation. Use for tasks that benefit from context isolation "
           "(exploration, research, focused work). The sub-agent has access "
           "to all tools but cannot spawn further sub-agents.",
       .parameters_schema_json =
           R"({"type":"object","additionalProperties":false,"properties":{"task":{"type":"string","description":"Detailed description of what the sub-agent should accomplish"},"mode":{"type":"string","enum":["foreground","background"],"description":"foreground blocks until complete and returns result. background runs in parallel and notifies when done."}},"required":["task"]})"},
      {.name = std::string(kTodoWriteToolName), .description = "Write the current todo list.", .parameters_schema_json = R"({"type":"object","properties":{"todos":{"type":"array","items":{"type":"object","properties":{"content":{"type":"string","description":"Task description"},"status":{"type":"string","enum":["pending","in_progress","completed"],"description":"Current status"},"priority":{"type":"string","enum":["high","medium","low"],"description":"Priority level"}},"required":["content","status"]}}},"required":["todos"]})"},
      {.name = std::string(kAskUserToolName),
       .description = "Ask the user a question and wait for their response.",
       .parameters_schema_json =
           R"({"type":"object","properties":{"question":{"type":"string","description":"The question to ask the user"},"options":{"type":"array","items":{"type":"string"},"description":"Optional suggested answers"}},"required":["question"]})"},
      {.name = std::string(kBashToolName),
       .description =
           "Execute a shell command in the workspace directory. stdout and "
           "stderr are merged. Output is capped at 16 KB. Every call requires "
           "user approval before execution.",
       .parameters_schema_json =
           R"json({"type":"object","additionalProperties":false,"properties":{"command":{"type":"string","description":"Shell command to execute (passed to /bin/sh -c)"},"timeout_ms":{"type":"integer","description":"Timeout in milliseconds (default 30000, max 300000)","minimum":100,"maximum":300000}},"required":["command"]})json"},
      {.name = std::string(kFileEditToolName),
       .description =
           "Edit a file by replacing an exact string. old_string must match "
           "exactly once (whitespace-tolerant fallbacks applied). Use "
           "replace_all to replace all occurrences.",
       .parameters_schema_json =
           R"json({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string","description":"Workspace-relative or absolute path to the file"},"old_string":{"type":"string","description":"Exact text to replace (must not be empty)"},"new_string":{"type":"string","description":"Replacement text (can be empty to delete)"},"replace_all":{"type":"boolean","description":"Replace all occurrences (default false)"}},"required":["filepath","old_string","new_string"]})json"},
      {.name = std::string(kGrepToolName),
       .description =
           "Search for a regex pattern in workspace files using ripgrep. "
           "Respects .gitignore by default. Requires ripgrep (rg) in PATH.",
       .parameters_schema_json =
           R"json({"type":"object","additionalProperties":false,"properties":{"pattern":{"type":"string","description":"Regex pattern (Rust regex syntax)"},"path":{"type":"string","description":"Path to search; defaults to workspace root"},"include":{"type":"string","description":"Glob filter for filenames (e.g. '*.cpp')"},"case_sensitive":{"type":"boolean","description":"Case-sensitive search (default false)"},"include_ignored":{"type":"boolean","description":"Include .gitignored files (default false)"}},"required":["pattern"]})json"},
      {.name = std::string(kGlobToolName),
       .description = "Find files matching a glob pattern. Supports **, *, ?. "
                      "Respects .gitignore by default. Results sorted by mtime "
                      "descending.",
       .parameters_schema_json =
           R"json({"type":"object","additionalProperties":false,"properties":{"pattern":{"type":"string","description":"Glob pattern (e.g. 'src/**/*.hpp')"},"path":{"type":"string","description":"Path to search; defaults to workspace root"},"include_ignored":{"type":"boolean","description":"Include .gitignored files (default false)"}},"required":["pattern"]})json"},
  };
}

PreparedToolCall PrepareToolCall(const chat::ToolCallRequest& request) {
  try {
    const auto args = ParseArguments(request);
    const auto it = kPrepareRegistry.find(request.name);
    if (it != kPrepareRegistry.end()) {
      return it->second(request, args);
    }
    return PreparedToolCall{
        .request = request,
        .preview = ToolCallError{
            .tool_name = request.name,
            .error = ErrorInfo{.message = "Unknown tool: " + request.name}}};
  } catch (const std::exception& error) {
    return PreparedToolCall{
        .request = request,
        .preview = ToolCallError{.tool_name = request.name,
                                 .error = ErrorInfo{.message = error.what()}}};
  }
}

}  // namespace yac::tool_call
