#include "chat/reasoning_effort.hpp"

#include <array>

namespace yac::chat {
namespace {

const std::array<std::pair<ReasoningEffort, std::string_view>, 6>&
EffortStrings() {
  static const std::array<std::pair<ReasoningEffort, std::string_view>, 6>
      table = {{{ReasoningEffort::None, "none"},
                {ReasoningEffort::Minimal, "minimal"},
                {ReasoningEffort::Low, "low"},
                {ReasoningEffort::Medium, "medium"},
                {ReasoningEffort::High, "high"},
                {ReasoningEffort::XHigh, "xhigh"}}};
  return table;
}

}  // namespace

std::optional<ReasoningEffort> ParseReasoningEffortValue(
    std::string_view reasoning_effort) {
  for (const auto& [effort, text] : EffortStrings()) {
    if (reasoning_effort == text) {
      return effort;
    }
  }
  return std::nullopt;
}

std::string_view ReasoningEffortToString(ReasoningEffort reasoning_effort) {
  for (const auto& [effort, text] : EffortStrings()) {
    if (effort == reasoning_effort) {
      return text;
    }
  }
  return "";
}

std::vector<ReasoningEffort> AllReasoningEffortValuesList() {
  std::vector<ReasoningEffort> values;
  values.reserve(EffortStrings().size());
  for (const auto& [effort, text] : EffortStrings()) {
    (void)text;
    values.push_back(effort);
  }
  return values;
}

}  // namespace yac::chat
