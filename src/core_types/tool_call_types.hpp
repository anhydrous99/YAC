#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace yac::tool_call {

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
};

struct GlobCall {
  std::string pattern;
  std::vector<std::string> matched_files;
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

using ToolCallBlock =
    std::variant<BashCall, FileEditCall, FileReadCall, FileWriteCall,
                 ListDirCall, GrepCall, GlobCall, WebFetchCall, WebSearchCall,
                 LspDiagnosticsCall, LspReferencesCall, LspGotoDefinitionCall,
                 LspRenameCall, LspSymbolsCall, SubAgentCall>;

}  // namespace yac::tool_call
