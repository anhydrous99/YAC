#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace yac::chat {

class PlanSession {
 public:
  using Clock = std::function<std::chrono::system_clock::time_point()>;

  PlanSession();

  void SetClock(Clock clock);
  [[nodiscard]] std::optional<std::filesystem::path> EnsurePlanPath(
      const std::string& workspace_root, const std::string& first_user_prompt);
  static bool WriteApprovedPlan(const std::filesystem::path& plan_path,
                                const std::string& plan);

 private:
  Clock clock_;
};

}  // namespace yac::chat
