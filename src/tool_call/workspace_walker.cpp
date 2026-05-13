#include "tool_call/workspace_walker.hpp"

#include "tool_call/gitignore_filter.hpp"
#include "tool_call/subprocess_runner.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace yac::tool_call {

namespace {

// rg --files on a 20k-path workspace at ~60 bytes per path is ~1.2 MB; round
// up to 4 MB so we never silently truncate on real repos. The default
// `kMaxToolOutputBytes` (16 KB) is sized for tool-call previews and would
// drop most of the listing.
constexpr std::size_t kMaxWalkOutputBytes = 4UL * 1024UL * 1024UL;
constexpr int kRipgrepTimeoutMs = 5000;

// Once rg fails to spawn (exit 127 or fork failure) we cache the verdict so
// subsequent rebuilds don't pay the fork/exec round-trip. The test-only
// override below lets tests force the fallback walker on hosts that have rg.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_rg_unavailable{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_rg_disabled_for_testing{false};

void SortRows(std::vector<FileMentionRow>& rows) {
  std::ranges::sort(rows,
                    [](const FileMentionRow& a, const FileMentionRow& b) {
                      if (a.relative_path.size() != b.relative_path.size()) {
                        return a.relative_path.size() < b.relative_path.size();
                      }
                      return a.relative_path < b.relative_path;
                    });
}

std::string NormalizeRelative(const std::filesystem::path& relative) {
  std::string text = relative.string();
  std::ranges::replace(text, '\\', '/');
  return text;
}

WalkResult WalkWithStdFs(const std::filesystem::path& root,
                         std::size_t max_entries, std::stop_token st) {
  WalkResult result;
  GitignoreFilter filter(root);

  std::error_code iter_ec;
  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied,
      iter_ec);
  const std::filesystem::recursive_directory_iterator end{};

  while (!iter_ec && it != end) {
    if (st.stop_requested()) {
      result.cancelled = true;
      break;
    }
    const auto& entry = *it;
    std::error_code ec;

    if (entry.is_directory(ec) && !ec) {
      const auto rel_dir =
          std::filesystem::relative(entry.path(), root, ec).string();
      if (!ec && filter.ShouldSkip(rel_dir + "/")) {
        it.disable_recursion_pending();
      }
    } else if (entry.is_regular_file(ec) && !ec) {
      const auto rel_path = std::filesystem::relative(entry.path(), root, ec);
      if (!ec) {
        std::string rel = NormalizeRelative(rel_path);
        if (!filter.ShouldSkip(rel)) {
          std::error_code size_ec;
          const auto size = entry.file_size(size_ec);
          result.rows.push_back(FileMentionRow{
              .relative_path = std::move(rel),
              .size_bytes = size_ec ? 0 : size,
          });
          if (result.rows.size() >= max_entries) {
            result.truncated = true;
            break;
          }
        }
      }
    }
    it.increment(iter_ec);
  }

  SortRows(result.rows);
  return result;
}

WalkResult ParseRipgrepFiles(const std::filesystem::path& root,
                             std::string_view payload,
                             std::size_t max_entries,
                             bool subprocess_truncated) {
  WalkResult result;
  result.used_rg = true;
  result.truncated = subprocess_truncated;

  std::size_t start = 0;
  while (start <= payload.size() && result.rows.size() < max_entries) {
    const auto end = payload.find('\0', start);
    const auto len = end == std::string_view::npos ? payload.size() - start
                                                   : end - start;
    if (len > 0) {
      std::string rel(payload.substr(start, len));
      if (rel.starts_with("./")) {
        rel.erase(0, 2);
      }
      std::ranges::replace(rel, '\\', '/');

      std::error_code size_ec;
      const auto size = std::filesystem::file_size(root / rel, size_ec);
      result.rows.push_back(FileMentionRow{
          .relative_path = std::move(rel),
          .size_bytes = size_ec ? 0 : size,
      });
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }

  if (!subprocess_truncated && result.rows.size() >= max_entries &&
      start < payload.size() &&
      payload.find('\0', start) != std::string_view::npos) {
    result.truncated = true;
  }

  SortRows(result.rows);
  return result;
}

}  // namespace

WalkResult WalkWorkspace(const std::filesystem::path& root,
                         std::size_t max_entries, std::stop_token st) {
  const bool skip_rg =
      g_rg_unavailable.load(std::memory_order_relaxed) ||
      g_rg_disabled_for_testing.load(std::memory_order_relaxed);
  if (skip_rg) {
    return WalkWithStdFs(root, max_entries, st);
  }

  // `--no-require-git` makes rg honor .gitignore / .ignore even when the
  // workspace isn't a git repo — matches the previous in-process walker's
  // behavior and avoids surprising "ignored files surface in the menu" for
  // non-git workspaces.
  SubprocessOptions opts{
      .argv = {"rg", "--files", "--hidden", "--no-follow", "--no-require-git",
               "--null"},
      .cwd = root,
      .timeout_ms = kRipgrepTimeoutMs,
      .max_output_bytes = kMaxWalkOutputBytes,
  };
  auto run = RunSubprocessCapture(opts, st);

  if (run.cancelled || st.stop_requested()) {
    WalkResult r;
    r.cancelled = true;
    return r;
  }
  if (run.spawn_failed || run.exit_code == 127) {
    g_rg_unavailable.store(true, std::memory_order_relaxed);
    return WalkWithStdFs(root, max_entries, st);
  }
  // rg --files returns 0 on success; 1 on "no files matched" (still valid for
  // an empty workspace, output is empty). Anything else is unexpected — fall
  // back so the user still sees a listing.
  if (run.exit_code != 0 && run.exit_code != 1) {
    return WalkWithStdFs(root, max_entries, st);
  }

  return ParseRipgrepFiles(root, run.output, max_entries, run.truncated);
}

void SetRipgrepDisabledForTesting(bool disabled) {
  g_rg_disabled_for_testing.store(disabled, std::memory_order_relaxed);
}

}  // namespace yac::tool_call
