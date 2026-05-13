#include "presentation/file_mention_inliner.hpp"

#include "presentation/file_mention_pattern.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace yac::presentation {

namespace {

constexpr std::size_t kBinarySniffBytes = 8UL * 1024UL;

bool LooksBinary(std::string_view content) {
  const auto n = std::min(content.size(), kBinarySniffBytes);
  for (std::size_t i = 0; i < n; ++i) {
    if (content[i] == '\0') {
      return true;
    }
  }
  return false;
}

}  // namespace

InlineResult InlineFileMentions(std::string_view user_text,
                                const tool_call::WorkspaceFilesystem& fs,
                                std::size_t per_file_cap,
                                std::size_t total_cap) {
  InlineResult result;

  const auto spans = FindMentionSpans(user_text);

  std::vector<std::string> ordered_unique;
  std::set<std::string> seen;
  for (const auto& span : spans) {
    if (!IsMentionAtTokenBoundary(user_text, span)) {
      continue;
    }
    std::string token{MentionPath(user_text, span)};
    if (token.empty()) {
      continue;
    }
    if (seen.insert(token).second) {
      ordered_unique.push_back(std::move(token));
    }
  }

  if (ordered_unique.empty()) {
    result.text.assign(user_text);
    return result;
  }

  std::string attachments;
  std::size_t total_bytes = 0;

  for (const auto& token : ordered_unique) {
    if (total_bytes >= total_cap) {
      attachments +=
          "[remaining files skipped: total attachment cap reached]\n";
      break;
    }

    attachments += "--- BEGIN @";
    attachments += token;
    attachments += " ---\n";

    std::string content;
    std::uintmax_t full_size = 0;
    std::string error;
    try {
      const auto resolved = fs.ResolvePath(token);
      std::error_code stat_ec;
      if (std::filesystem::is_directory(resolved, stat_ec)) {
        error = "is a directory";
      } else if (!std::filesystem::is_regular_file(resolved, stat_ec)) {
        error = "file not found";
      } else {
        std::error_code size_ec;
        full_size = std::filesystem::file_size(resolved, size_ec);
        if (size_ec) {
          error = "unable to stat file";
        } else {
          // Only read what we might keep (per_file_cap) plus enough to
          // detect binary content (kBinarySniffBytes). Avoids loading
          // multi-MB files just to discover they're binary.
          const std::size_t to_read = std::min<std::uintmax_t>(
              per_file_cap + kBinarySniffBytes, full_size);
          content =
              tool_call::WorkspaceFilesystem::ReadFilePrefix(resolved, to_read);
        }
      }
    } catch (const std::exception& e) {
      error = e.what();
    }

    if (!error.empty()) {
      attachments += "[error: ";
      attachments += error;
      attachments += "]\n";
      result.diagnostics.push_back(
          InlineDiagnostic{.path = token, .message = error});
    } else if (LooksBinary(content)) {
      const std::string msg = "binary file";
      attachments += "[error: ";
      attachments += msg;
      attachments += "]\n";
      result.diagnostics.push_back(
          InlineDiagnostic{.path = token, .message = msg});
    } else {
      const std::size_t take = std::min(content.size(), per_file_cap);
      attachments.append(content, 0, take);
      if (take > 0 && content[take - 1] != '\n') {
        attachments += '\n';
      }
      if (take < full_size) {
        attachments += "[truncated: ";
        attachments += std::to_string(full_size - take);
        attachments += " bytes omitted]\n";
      }
      total_bytes += take;
    }

    attachments += "--- END @";
    attachments += token;
    attachments += " ---\n";
  }

  result.text.reserve(user_text.size() + attachments.size() + 32);
  result.text.assign(user_text);
  result.text += "\n\nAttached files:\n\n";
  result.text += attachments;
  return result;
}

}  // namespace yac::presentation
