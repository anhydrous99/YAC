#pragma once

#include <string>
#include <variant>
#include <vector>

namespace yac::presentation::tool_call {

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

using ToolCallBlock =
    std::variant<BashCall, FileEditCall, FileReadCall, GrepCall, GlobCall,
                 WebFetchCall, WebSearchCall>;

}  // namespace yac::presentation::tool_call
