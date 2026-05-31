#include "chat/plan_session.hpp"

#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace yac::chat {

namespace {

constexpr std::size_t kMaxPlanSlugLength = 40;

std::string SlugForPlanPrompt(const std::string& prompt) {
  std::string slug;
  bool pending_separator = false;
  for (unsigned char ch : prompt) {
    if (std::isalnum(ch) != 0) {
      if (pending_separator && !slug.empty() &&
          slug.size() < kMaxPlanSlugLength) {
        slug.push_back('-');
      }
      pending_separator = false;
      if (slug.size() < kMaxPlanSlugLength) {
        slug.push_back(static_cast<char>(std::tolower(ch)));
      }
    } else {
      pending_separator = true;
    }
    if (slug.size() == kMaxPlanSlugLength) {
      break;
    }
  }
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  if (slug.empty()) {
    return "plan";
  }
  return slug;
}

std::string FormatPlanTimestamp(std::chrono::system_clock::time_point time) {
  const auto raw_time = std::chrono::system_clock::to_time_t(time);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &raw_time);
#else
  localtime_r(&raw_time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return out.str();
}

bool IsInsideWorkspace(const std::filesystem::path& workspace,
                       const std::filesystem::path& path) {
  const auto normal_workspace =
      std::filesystem::absolute(workspace).lexically_normal();
  const auto normal_path = std::filesystem::absolute(path).lexically_normal();
  const auto relative = normal_path.lexically_relative(normal_workspace);
  return !relative.empty() && *relative.begin() != "..";
}

}  // namespace

PlanSession::PlanSession()
    : clock_([] { return std::chrono::system_clock::now(); }) {}

void PlanSession::SetClock(Clock clock) {
  clock_ = std::move(clock);
}

std::optional<std::filesystem::path> PlanSession::EnsurePlanPath(
    const std::string& workspace_root_config,
    const std::string& first_user_prompt) {
  auto workspace_root = workspace_root_config.empty()
                            ? std::filesystem::current_path()
                            : std::filesystem::path(workspace_root_config);
  workspace_root = std::filesystem::absolute(workspace_root).lexically_normal();
  const auto plans_dir = workspace_root / ".opencode" / "plans";
  const auto filename = FormatPlanTimestamp(clock_()) + "-" +
                        SlugForPlanPrompt(first_user_prompt) + ".md";
  const auto plan_path = (plans_dir / filename).lexically_normal();
  if (!IsInsideWorkspace(workspace_root, plan_path)) {
    return std::nullopt;
  }

  std::filesystem::create_directories(plans_dir);
  std::ofstream plan_file(plan_path, std::ios::app);
  return plan_path;
}

bool PlanSession::WriteApprovedPlan(const std::filesystem::path& plan_path,
                                    const std::string& plan) {
  std::ofstream plan_file(plan_path, std::ios::trunc);
  if (!plan_file) {
    return false;
  }
  plan_file << plan;
  return true;
}

}  // namespace yac::chat
