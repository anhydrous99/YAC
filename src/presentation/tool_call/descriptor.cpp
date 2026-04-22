#include "descriptor.hpp"

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
              .summary = call.exit_code != 0 ? "exit " +
                                                   std::to_string(call.exit_code)
                                             : "exit 0",
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileEditCall>) {
          return ToolCallDescriptor{
              .tag = "edit",
              .label = "Edit " + TruncateString(Basename(call.filepath), 30),
              .summary = std::to_string(call.diff.size()) + " lines",
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileReadCall>) {
          return ToolCallDescriptor{
              .tag = "read",
              .label = "Read " + TruncateString(Basename(call.filepath), 30),
              .summary = std::to_string(call.lines_loaded) + " lines",
          };
        } else if constexpr (std::is_same_v<T, tool_data::FileWriteCall>) {
          return ToolCallDescriptor{
              .tag = "write",
              .label = "Write " + TruncateString(Basename(call.filepath), 30),
              .summary = call.is_error
                             ? "failed"
                             : "+" + std::to_string(call.lines_added) + " -" +
                                   std::to_string(call.lines_removed),
          };
        } else if constexpr (std::is_same_v<T, tool_data::ListDirCall>) {
          return ToolCallDescriptor{
              .tag = "list",
              .label = "List directory",
              .summary = call.is_error ? "failed"
                                       : std::to_string(call.entries.size()) +
                                             " entries",
          };
        } else if constexpr (std::is_same_v<T, tool_data::GrepCall>) {
          return ToolCallDescriptor{
              .tag = "grep",
              .label = "Search for \"" + TruncateString(call.pattern, 20) + "\"",
              .summary = std::to_string(call.match_count) + " matches",
          };
        } else if constexpr (std::is_same_v<T, tool_data::GlobCall>) {
          return ToolCallDescriptor{
              .tag = "glob",
              .label = "Find files",
              .summary = std::to_string(call.matched_files.size()) + " files",
          };
        } else if constexpr (std::is_same_v<T, tool_data::WebFetchCall>) {
          return ToolCallDescriptor{
              .tag = "fetch",
              .label = "Fetch URL",
              .summary = call.title.empty() ? std::string{"fetched"} : call.title,
          };
        } else if constexpr (std::is_same_v<T, tool_data::WebSearchCall>) {
          return ToolCallDescriptor{
              .tag = "search",
              .label = "Web search",
              .summary = std::to_string(call.results.size()) + " results",
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspDiagnosticsCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Get diagnostics",
              .summary = call.is_error
                             ? "failed"
                             : std::to_string(call.diagnostics.size()) +
                                   " diagnostics",
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspReferencesCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Find references",
              .summary = call.is_error
                             ? "failed"
                             : std::to_string(call.references.size()) +
                                   " references",
          };
        } else if constexpr (std::is_same_v<T,
                                            tool_data::LspGotoDefinitionCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Go to definition",
              .summary = call.is_error
                             ? "failed"
                             : std::to_string(call.definitions.size()) +
                                   " definitions",
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspRenameCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "Rename symbol",
              .summary = call.is_error
                             ? "failed"
                             : std::to_string(call.changes_count) + " changes",
          };
        } else if constexpr (std::is_same_v<T, tool_data::LspSymbolsCall>) {
          return ToolCallDescriptor{
              .tag = "lsp",
              .label = "List symbols",
              .summary = call.is_error
                             ? "failed"
                             : std::to_string(call.symbols.size()) + " symbols",
          };
        } else if constexpr (std::is_same_v<T, tool_data::SubAgentCall>) {
          return ToolCallDescriptor{
              .tag = "agent",
              .label = "[>] Sub-agent",
              .summary = "Sub-agent: " + TruncateString(call.task, 40) + " - " +
                         SubAgentStatusSummary(call.status),
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
