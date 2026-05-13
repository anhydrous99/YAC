#include "tool_call/file_index.hpp"

#include "tool_call/workspace_walker.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yac::tool_call {

namespace {

std::string ToLower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return out;
}

std::string_view Basename(std::string_view relative_path) {
  const auto slash = relative_path.find_last_of('/');
  if (slash == std::string_view::npos) {
    return relative_path;
  }
  return relative_path.substr(slash + 1);
}

// 1 = basename-prefix, 2 = path-prefix, 3 = substring, 0 = no match. Early
// returns enforce the ranking. Empty needle returns 1 so all rows tie; the
// secondary path-length / lexicographic sort then decides ordering.
int Score(std::string_view path_lower, std::string_view basename_lower,
          std::string_view needle_lower) {
  if (needle_lower.empty()) {
    return 1;
  }
  if (basename_lower.starts_with(needle_lower)) {
    return 1;
  }
  if (path_lower.starts_with(needle_lower)) {
    return 2;
  }
  if (path_lower.find(needle_lower) != std::string_view::npos) {
    return 3;
  }
  return 0;
}

}  // namespace

FileIndex::FileIndex(const WorkspaceFilesystem& fs)
    : fs_(&fs),
      worker_([this](std::stop_token st) { WorkerLoop(st); }) {}

FileIndex::~FileIndex() {
  worker_.request_stop();
  {
    std::scoped_lock lk(mu_);
    rebuild_requested_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void FileIndex::WarmAsync() {
  {
    std::scoped_lock lk(mu_);
    rebuild_requested_ = true;
  }
  wake_.notify_all();
}

void FileIndex::Invalidate() {
  WarmAsync();
}

void FileIndex::SetOnRebuildComplete(RebuildCallback cb) {
  std::scoped_lock lk(mu_);
  on_rebuild_complete_ = std::move(cb);
}

bool FileIndex::WaitForReady(std::chrono::milliseconds timeout) const {
  std::unique_lock lk(mu_);
  return wake_.wait_for(lk, timeout, [this] {
    return state_.load(std::memory_order_acquire) == State::Ready;
  });
}

std::vector<FileMentionRow> FileIndex::QueryLocked(std::string_view prefix,
                                                   std::size_t limit) const {
  if (limit == 0 || rows_.empty()) {
    return {};
  }
  const std::string needle = ToLower(prefix);

  struct Candidate {
    int score = 0;
    std::size_t row_index = 0;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(rows_.size());
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const std::string_view path_lower{path_lower_[i]};
    const std::string_view base_lower = Basename(path_lower);
    const int score = Score(path_lower, base_lower, needle);
    if (score > 0) {
      candidates.push_back({.score = score, .row_index = i});
    }
  }

  std::ranges::sort(
      candidates, [this](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) {
          return a.score < b.score;
        }
        const auto& ap = rows_[a.row_index].relative_path;
        const auto& bp = rows_[b.row_index].relative_path;
        if (ap.size() != bp.size()) {
          return ap.size() < bp.size();
        }
        return ap < bp;
      });

  std::vector<FileMentionRow> out;
  out.reserve(std::min(limit, candidates.size()));
  for (std::size_t i = 0; i < candidates.size() && out.size() < limit; ++i) {
    out.push_back(rows_[candidates[i].row_index]);
  }
  return out;
}

std::vector<FileMentionRow> FileIndex::Query(std::string_view prefix,
                                             std::size_t limit) const {
  std::vector<FileMentionRow> out;
  bool wake_worker = false;
  {
    std::scoped_lock lk(mu_);
    out = QueryLocked(prefix, limit);
    if (state_.load(std::memory_order_acquire) == State::Ready &&
        std::chrono::steady_clock::now() - built_at_ > kTtl &&
        !rebuild_requested_) {
      rebuild_requested_ = true;
      wake_worker = true;
    }
  }
  if (wake_worker) {
    wake_.notify_all();
  }
  return out;
}

void FileIndex::WorkerLoop(std::stop_token st) {
  while (!st.stop_requested()) {
    {
      std::unique_lock lk(mu_);
      wake_.wait(lk, [this, &st] {
        return st.stop_requested() || rebuild_requested_;
      });
      if (st.stop_requested()) {
        return;
      }
      rebuild_requested_ = false;
      state_.store(State::Warming, std::memory_order_release);
    }

    auto walk = WalkWorkspace(fs_->Root(), kMaxEntries, st);
    if (st.stop_requested() || walk.cancelled) {
      return;
    }

    std::vector<std::string> lower;
    lower.reserve(walk.rows.size());
    for (const auto& row : walk.rows) {
      lower.push_back(ToLower(row.relative_path));
    }

    RebuildCallback cb_copy;
    {
      std::scoped_lock lk(mu_);
      rows_ = std::move(walk.rows);
      path_lower_ = std::move(lower);
      built_at_ = std::chrono::steady_clock::now();
      state_.store(State::Ready, std::memory_order_release);
      cb_copy = on_rebuild_complete_;
    }
    wake_.notify_all();

    if (cb_copy) {
      cb_copy();
    }
  }
}

}  // namespace yac::tool_call
