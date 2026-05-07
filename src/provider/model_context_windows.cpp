#include "provider/model_context_windows.hpp"

#include "provider/language_model_provider.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::provider {
namespace {

std::string_view StripInferenceProfilePrefix(std::string_view model_id) {
  for (std::string_view prefix : {"us.", "eu.", "apac.", "global."}) {
    if (model_id.starts_with(prefix)) {
      return model_id.substr(prefix.size());
    }
  }
  return model_id;
}

const std::unordered_map<std::string, int>& ExactTable() {
  static const std::unordered_map<std::string, int> table = {
      {"gpt-4o", 128000},
      {"gpt-4o-mini", 128000},
      {"gpt-4-turbo", 128000},
      {"gpt-3.5-turbo", 16385},
      {"anthropic.claude-3-5-sonnet-20241022-v2:0", 200000},
      {"anthropic.claude-3-5-sonnet-20240620-v1:0", 200000},
      {"anthropic.claude-3-5-haiku-20241022-v1:0", 200000},
      {"anthropic.claude-3-opus-20240229-v1:0", 200000},
      {"anthropic.claude-3-sonnet-20240229-v1:0", 200000},
      {"anthropic.claude-3-haiku-20240307-v1:0", 200000},
      {"amazon.nova-pro-v1:0", 300000},
      {"amazon.nova-lite-v1:0", 300000},
      {"amazon.nova-micro-v1:0", 128000},
      {"meta.llama3-1-70b-instruct-v1:0", 128000},
      {"meta.llama3-1-8b-instruct-v1:0", 128000},
      {"mistral.mistral-large-2407-v1:0", 128000},
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
      {"claude-", 200000},      {"glm-5.1", 200000},
      {"glm-5", 200000},        {"glm-4.7", 200000},
      {"glm-4.6", 200000},      {"glm-4.5", 128000},
      {"glm-4", 128000},        {"glm-", 128000},
      {"gemini-2", 1000000},    {"gemini-1.5", 1000000},
      {"gemini-", 32768},       {"deepseek-", 128000},
      {"qwen", 131072},         {"llama-3.1", 128000},
      {"llama-3", 8192},
  };
  return table;
}

}  // namespace

int LookupContextWindow(std::string_view model_id) {
  if (model_id.empty()) {
    return 0;
  }
  const std::string_view stripped = StripInferenceProfilePrefix(model_id);
  const std::string key(stripped);
  if (const auto it = ExactTable().find(key); it != ExactTable().end()) {
    return it->second;
  }
  for (const auto& [prefix, tokens] : PrefixTable()) {
    if (key.size() >= prefix.size() && key.starts_with(prefix)) {
      return tokens;
    }
  }
  return 0;
}

int ResolveContextWindow(const LanguageModelProvider* provider,
                         const std::string& model_id) {
  if (provider != nullptr) {
    if (const int advertised = provider->GetContextWindow(model_id);
        advertised > 0) {
      return advertised;
    }
  }
  return LookupContextWindow(model_id);
}

}  // namespace yac::provider
