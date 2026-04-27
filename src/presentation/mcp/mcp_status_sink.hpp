#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace yac::presentation {

struct McpServerInfo {
  std::string id;
  std::string state;
  std::string error;
};

struct McpProgressEntry {
  uint64_t message_id = 0;
  std::string text;
  double progress = 0.0;
  double total = 0.0;
};

struct McpStatusSink {
  mutable std::mutex mutex;
  std::vector<McpServerInfo> servers;
  McpProgressEntry last_progress;

  void UpdateServer(std::string id, std::string state, std::string error) {
    std::lock_guard lock(mutex);
    for (auto& srv : servers) {
      if (srv.id == id) {
        srv.state = std::move(state);
        srv.error = std::move(error);
        return;
      }
    }
    servers.push_back({std::move(id), std::move(state), std::move(error)});
  }

  void UpdateProgress(uint64_t message_id, std::string text, double progress,
                      double total) {
    std::lock_guard lock(mutex);
    last_progress = {message_id, std::move(text), progress, total};
  }

  [[nodiscard]] std::vector<McpServerInfo> GetSnapshot() const {
    std::lock_guard lock(mutex);
    return servers;
  }

  [[nodiscard]] McpProgressEntry GetProgress() const {
    std::lock_guard lock(mutex);
    return last_progress;
  }
};

}  // namespace yac::presentation
