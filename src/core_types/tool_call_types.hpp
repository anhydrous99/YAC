#pragma once

#include "core_types/typed_ids.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace yac::tool_call {

inline constexpr std::string_view kFileReadToolName = "file_read";
inline constexpr std::string_view kFileWriteToolName = "file_write";
inline constexpr std::string_view kListDirToolName = "list_dir";
inline constexpr std::string_view kLspDiagnosticsToolName = "lsp_diagnostics";
inline constexpr std::string_view kLspReferencesToolName = "lsp_references";
inline constexpr std::string_view kLspGotoDefinitionToolName =
    "lsp_goto_definition";
inline constexpr std::string_view kLspRenameToolName = "lsp_rename";
inline constexpr std::string_view kLspSymbolsToolName = "lsp_symbols";
inline constexpr std::string_view kSubAgentToolName = "sub_agent";
inline constexpr std::string_view kTodoWriteToolName = "todo_write";
inline constexpr std::string_view kAskUserToolName = "ask_user";
inline constexpr std::string_view kBashToolName = "bash";
inline constexpr std::string_view kFileEditToolName = "file_edit";
inline constexpr std::string_view kGrepToolName = "grep";
inline constexpr std::string_view kGlobToolName = "glob";
inline constexpr std::string_view kMcpToolNamePrefix = "mcp_";
inline constexpr std::string_view kMcpToolNameSeparator = "__";

struct DiffLine {
  enum Type { Add, Remove, Context };

  Type type{};
  std::string content;
};

struct GrepMatch {
  std::string filepath;
  int line{};
  std::string content;
};

struct SearchResult {
  std::string title;
  std::string url;
  std::string snippet;
};

enum class DirectoryEntryType { File, Directory, Other };

struct DirectoryEntry {
  std::string name;
  DirectoryEntryType type = DirectoryEntryType::File;
  uintmax_t size = 0;
};

enum class DiagnosticSeverity { Error, Warning, Information, Hint };

struct LspDiagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Information;
  std::string message;
  int line = 1;
};

struct LspLocation {
  std::string filepath;
  int line = 1;
  int character = 1;
};

struct LspSymbol {
  std::string name;
  std::string kind;
  int line = 1;
};

struct LspTextEdit {
  std::string filepath;
  int start_line = 1;
  int start_character = 1;
  int end_line = 1;
  int end_character = 1;
  std::string new_text;
};

struct BashCall {
  std::string command;
  std::string output;
  int exit_code{};
  bool is_error{};
};

struct FileEditCall {
  std::string filepath;
  std::vector<DiffLine> diff;
  bool is_error{};
  std::string error;
};

struct FileReadCall {
  std::string filepath;
  int lines_loaded{};
  std::string excerpt;
};

struct FileWriteCall {
  std::string filepath;
  std::string content_preview;
  std::string content_tail;
  int lines_added{};
  int lines_removed{};
  bool is_error{};
  bool is_streaming{};
  std::string error;
};

struct ListDirCall {
  std::string path;
  std::vector<DirectoryEntry> entries;
  bool truncated{};
  bool is_error{};
  std::string error;
};

struct GrepCall {
  std::string pattern;
  int match_count{};
  std::vector<GrepMatch> matches;
  bool is_error{};
  std::string error;
};

struct GlobCall {
  std::string pattern;
  std::vector<std::string> matched_files;
  bool is_error{};
  std::string error;
};

struct WebFetchCall {
  std::string url;
  std::string title;
  std::string excerpt;
};

struct WebSearchCall {
  std::string query;
  std::vector<SearchResult> results;
};

struct LspDiagnosticsCall {
  std::string file_path;
  std::vector<LspDiagnostic> diagnostics;
  bool is_error{};
  std::string error;
};

struct LspReferencesCall {
  std::string symbol;
  std::string file_path;
  std::vector<LspLocation> references;
  bool is_error{};
  std::string error;
};

struct LspGotoDefinitionCall {
  std::string symbol;
  std::string file_path;
  int line = 1;
  int character = 1;
  std::vector<LspLocation> definitions;
  bool is_error{};
  std::string error;
};

struct LspRenameCall {
  std::string file_path;
  int line = 1;
  int character = 1;
  std::string old_name;
  std::string new_name;
  int changes_count{};
  std::vector<LspTextEdit> changes;
  bool is_error{};
  std::string error;
};

struct LspSymbolsCall {
  std::string file_path;
  std::vector<LspSymbol> symbols;
  bool is_error{};
  std::string error;
};

enum class SubAgentMode { Foreground, Background };

enum class SubAgentStatus {
  Pending,
  Running,
  Complete,
  Error,
  Cancelled,
  Timeout,
};

struct SubAgentCall {
  std::string task;
  SubAgentMode mode = SubAgentMode::Foreground;
  SubAgentStatus status = SubAgentStatus::Pending;
  std::string agent_id;
  std::string result;
  std::string result_summary;
  int tool_count{};
  int elapsed_ms{};
};

struct TodoItem {
  std::string content;
  std::string status;
  std::string priority;
};

struct TodoWriteCall {
  std::vector<TodoItem> todos;
  bool is_error{};
  std::string error;
};

struct AskUserCall {
  std::string question;
  std::vector<std::string> options;
  std::string response;
  bool is_error{};
  std::string error;
};

enum class McpResultBlockKind {
  Text,
  Image,
  Audio,
  ResourceLink,
  EmbeddedResource,
};

struct McpResultBlock {
  McpResultBlockKind kind = McpResultBlockKind::Text;
  std::string text;
  std::string mime_type;
  std::string uri;
  std::string name;
  uintmax_t bytes = 0;
};

struct McpToolCall {
  ::yac::McpServerId server_id;
  std::string tool_name;
  std::string original_tool_name;
  std::string arguments_json;
  std::vector<McpResultBlock> result_blocks;
  bool is_error = false;
  std::string error;
  bool is_truncated = false;
  uintmax_t result_bytes = 0;
  // Approval context (populated before approval display).
  bool server_requires_approval = false;
  std::vector<std::string> approval_required_tools;
};

using ToolCallBlock =
    std::variant<BashCall, FileEditCall, FileReadCall, FileWriteCall,
                 ListDirCall, GrepCall, GlobCall, WebFetchCall, WebSearchCall,
                 LspDiagnosticsCall, LspReferencesCall, LspGotoDefinitionCall,
                 LspRenameCall, LspSymbolsCall, SubAgentCall, TodoWriteCall,
                 AskUserCall, McpToolCall>;

namespace core_types {

struct ToolExecutionResult {
  ToolCallBlock block;
  std::string result_json;
  bool is_error = false;
};

}  // namespace core_types

using ToolExecutionResult = core_types::ToolExecutionResult;

}  // namespace yac::tool_call

namespace yac::core_types {

using ToolExecutionResult = ::yac::tool_call::core_types::ToolExecutionResult;

}  // namespace yac::core_types
