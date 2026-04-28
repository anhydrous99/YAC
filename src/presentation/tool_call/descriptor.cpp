#include "descriptor.hpp"

#include "presentation/util/count_summary.hpp"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace yac::presentation::tool_call {

namespace {

std::string TruncateString(const std::string& value, size_t max_len) {
  if (value.size() <= max_len) {
    return value;
  }
  return value.substr(0, max_len) + "...";
}

std::string Basename(const std::string& path) {
  const auto pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string SubAgentStatusSummary(tool_data::SubAgentStatus status) {
  switch (status) {
    case tool_data::SubAgentStatus::Running:
      return "running";
    case tool_data::SubAgentStatus::Complete:
      return "done";
    case tool_data::SubAgentStatus::Error:
      return "error";
    case tool_data::SubAgentStatus::Timeout:
      return "timeout";
    case tool_data::SubAgentStatus::Cancelled:
      return "cancelled";
    case tool_data::SubAgentStatus::Pending:
      return "pending";
  }
  return "pending";
}

}  // namespace

ToolCallDescriptor DescribeToolCall(const tool_data::ToolCallBlock& block) {
  return std::visit(
      [](const auto& call) -> ToolCallDescriptor {
        using T = std::decay_t<decltype(call)>;
        if constexpr (std::is_same_v<T, tool_data::BashCall>) {
          return ToolCallDescriptor{
              .tag = "bash",
              .label = "Run command",
              .summary = call.exit_code != 0
                             ? "exit " + std::to_string(call.exit_code)
                             : "exit 0",
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileEditCall>) {
          return ToolCallDescriptor{
              .tag = "edit",
              .label = "Edit " + TruncateString(Basename(call.filepath), 30),
              .summary = util::CountSummary(call.diff.size(), "line", "lines"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileReadCall>) {
          return ToolCallDescriptor{
              .tag = "read",
              .label = "Read " + TruncateString(Basename(call.filepath), 30),
              .summary = util::CountSummary(call.lines_loaded, "line", "lines"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileWriteCall>) {
          std::string summary;
          if (call.is_error) {
            summary = "failed";
          } else if (call.is_streaming) {
            summary = "streaming…";
          } else {
            summary = "+" + std::to_string(call.lines_added) + " -" +
                      std::to_string(call.lines_removed);
          }
          return ToolCallDescriptor{
              .tag = "write",
              .label = "Write " + TruncateString(Basename(call.filepath), 30),
              .summary = std::move(summary),
          };
        } else if constexpr (std::is_same_v<T, tool_data::ListDirCall>) {
          return ToolCallDescriptor{
              .tag = "list",
              .label = "List directory",
              .summary = call.is_error ? "failed"
                                       : util::CountSummary(call.entries.size(),
                                                            "entry", "entries"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::GrepCall>) {
          return ToolCallDescriptor{
              .tag = "grep",
              .label =
                  "Search for \"" + TruncateString(call.pattern, 20) + "\"",
              .summary =
                  util::CountSummary(call.match_count, "match", "matches"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::GlobCall>) {
          return ToolCallDescriptor{
              .tag = "glob",
              .label = "Find files",
              .summary = util::CountSummary(call.matched_files.size(), "file",
                                            "files"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::WebFetchCall>) {
          return ToolCallDescriptor{
              .tag = "fetch",
              .label = "Fetch URL",
              .summary =
                  call.title.empty() ? std::string{"fetched"} : call.title,
          };
        } else if constexpr (std::is_same_v<T, tool_data::WebSearchCall>) {
          return ToolCallDescriptor{
              .tag = "search",
              .label = "Web search",
              .summary =
                  util::CountSummary(call.results.size(), "result", "results"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspDiagnosticsCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Get diagnostics",
              .summary = call.is_error
                             ? "failed"
                             : util::CountSummary(call.diagnostics.size(),
                                                  "diagnostic", "diagnostics"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspReferencesCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Find references",
              .summary = call.is_error
                             ? "failed"
                             : util::CountSummary(call.references.size(),
                                                  "reference", "references"),
          };
        } else if constexpr (std::is_same_v<T,
                                            tool_data::LspGotoDefinitionCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Go to definition",
              .summary = call.is_error
                             ? "failed"
                             : util::CountSummary(call.definitions.size(),
                                                  "definition", "definitions"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspRenameCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Rename symbol",
              .summary = call.is_error
                             ? "failed"
                             : util::CountSummary(call.changes_count, "change",
                                                  "changes"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspSymbolsCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "List symbols",
              .summary = call.is_error
                             ? "failed"
                             : util::CountSummary(call.symbols.size(), "symbol",
                                                  "symbols"),
          };
        } else if constexpr (std::is_same_v<T, tool_data::SubAgentCall>) {
          return ToolCallDescriptor{
              .tag = "agent",
              .label = "[>] Sub-agent",
              .summary = "Sub-agent: " + TruncateString(call.task, 40) + " - " +
                         SubAgentStatusSummary(call.status),
          };
        } else if constexpr (std::is_same_v<T, tool_data::TodoWriteCall>) {
          if (call.is_error) {
            return ToolCallDescriptor{
                .tag = "todo",
                .label = "Todo list",
                .summary = "failed",
            };
          }
          const auto completed = std::count_if(
              call.todos.begin(), call.todos.end(),
              [](const auto& todo) { return todo.status == "completed"; });
          if (call.todos.empty()) {
            return ToolCallDescriptor{
                .tag = "todo",
                .label = "Todo list",
                .summary = "Todo list cleared",
            };
          }
          return ToolCallDescriptor{
              .tag = "todo",
              .label = "Todo list",
              .summary = "Updated " + std::to_string(call.todos.size()) +
                         " todos (" + std::to_string(completed) + " completed)",
          };
        } else if constexpr (std::is_same_v<T, tool_data::AskUserCall>) {
          return ToolCallDescriptor{
              .tag = "ask",
              .label = "ask_user",
              .summary = "ask_user",
          };
        } else if constexpr (std::is_same_v<T, tool_data::McpToolCall>) {
          // TODO(mcp): implement in T32
          return ToolCallDescriptor{
              .tag = "mcp",
              .label = "MCP tool",
              .summary = "",
          };
        } else {
          return ToolCallDescriptor{
              .tag = "tool",
              .label = "Tool",
              .summary = "",
          };
        }
      },
      block);
}

}  // namespace yac::presentation::tool_call
