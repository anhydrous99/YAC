#pragma once

#include "core_types/file_mention.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace yac::tool_call {

// Background-warmed file index of the workspace for the composer's @file
// autocomplete. The index owns a worker thread that performs the filesystem
// walk; Query() is non-blocking and scores against whatever snapshot the
// worker has most recently published. Empty snapshot before the first
// rebuild — UI surfaces this as an "Indexing workspace…" state via
// GetState().
class FileIndex {
 public:
  static constexpr auto kTtl = std::chrono::seconds(5);
  static constexpr std::size_t kMaxEntries = 20'000;

  enum class State { Cold, Warming, Ready };
  using RebuildCallback = std::function<void()>;

  explicit FileIndex(const WorkspaceFilesystem& fs);
  ~FileIndex();

  FileIndex(const FileIndex&) = delete;
  FileIndex& operator=(const FileIndex&) = delete;
  FileIndex(FileIndex&&) = delete;
  FileIndex& operator=(FileIndex&&) = delete;

  // Snapshot query — never touches disk. Returns up to `limit` rows scored
  // against the current snapshot. Empty before the first rebuild. If the
  // snapshot is older than kTtl, schedules a background refresh as a
  // side-effect.
  [[nodiscard]] std::vector<FileMentionRow> Query(std::string_view prefix,
                                                  std::size_t limit) const;

  // Schedules a rebuild on the worker thread. Idempotent — concurrent calls
  // are coalesced into a single rebuild pass. Returns immediately.
  void WarmAsync();

  // Marks the current snapshot stale and schedules a fresh rebuild. Existing
  // rows remain queryable until the rebuild publishes.
  void Invalidate();

  // Set before WarmAsync. Invoked on the worker thread (outside the lock)
  // after each successful rebuild. The bootstrap wires this to a
  // screen.Post(...) call that triggers a UI redraw.
  void SetOnRebuildComplete(RebuildCallback cb);

  [[nodiscard]] State GetState() const {
    return state_.load(std::memory_order_acquire);
  }

  // Blocks until state_ == Ready or timeout elapses. Intended for tests and
  // headless callers; UI code should rely on the SetOnRebuildComplete
  // callback instead.
  [[nodiscard]] bool WaitForReady(std::chrono::milliseconds timeout) const;

 private:
  void WorkerLoop(std::stop_token st);
  [[nodiscard]] std::vector<FileMentionRow> QueryLocked(std::string_view prefix,
                                                        std::size_t limit) const;

  const WorkspaceFilesystem* fs_;

  mutable std::mutex mu_;
  mutable std::condition_variable wake_;
  mutable std::vector<FileMentionRow> rows_;
  mutable std::vector<std::string> path_lower_;
  mutable std::chrono::steady_clock::time_point built_at_;
  mutable bool rebuild_requested_{false};
  std::atomic<State> state_{State::Cold};
  RebuildCallback on_rebuild_complete_;

  // Declared last so it is destroyed first: ~jthread joins before the mutex
  // and cv vanish out from under the worker thread.
  std::jthread worker_;
};

}  // namespace yac::tool_call
