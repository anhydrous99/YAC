#include "app/model_context_windows.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::app {
namespace {

const std::unordered_map<std::string, int>& ExactTable() {
  static const std::unordered_map<std::string, int> table = {
      {"gpt-4o", 128000},
      {"gpt-4o-mini", 128000},
      {"gpt-4-turbo", 128000},
      {"gpt-3.5-turbo", 16385},
  };
  return table;
}

const std::vector<std::pair<std::string, int>>& PrefixTable() {
  // Ordered most-specific → least-specific. First matching prefix wins.
  static const std::vector<std::pair<std::string, int>> table = {
      {"gpt-4.1", 1000000},     {"gpt-4o-mini", 128000},
      {"gpt-4o", 128000},       {"gpt-4-turbo", 128000},
      {"gpt-4", 8192},          {"gpt-5", 400000},
      {"gpt-3.5", 16385},       {"o4", 200000},
      {"o3", 200000},           {"o1", 200000},
      {"claude-opus", 200000},  {"claude-sonnet", 200000},
      {"claude-haiku", 200000}, {"claude-3", 200000},
      {"claude-", 200000},      {"glm-4.6", 200000},
      {"glm-4.5", 128000},      {"glm-4", 128000},
      {"glm-", 128000},         {"gemini-2", 1000000},
      {"gemini-1.5", 1000000},  {"gemini-", 32768},
      {"deepseek-", 128000},    {"qwen", 131072},
      {"llama-3.1", 128000},    {"llama-3", 8192},
  };
  return table;
}

}  // namespace

int LookupContextWindow(std::string_view model_id) {
  if (model_id.empty()) {
    return 0;
  }
  const std::string key(model_id);
  if (const auto it = ExactTable().find(key); it != ExactTable().end()) {
    return it->second;
  }
  for (const auto& [prefix, tokens] : PrefixTable()) {
    if (key.size() >= prefix.size() &&
        key.compare(0, prefix.size(), prefix) == 0) {
      return tokens;
    }
  }
  return 0;
}

}  // namespace yac::app
