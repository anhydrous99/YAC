#pragma once

#include "provider/language_model_provider.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace yac::provider {

class ProviderRegistry {
 public:
  void Register(std::shared_ptr<LanguageModelProvider> provider);
  [[nodiscard]] std::shared_ptr<LanguageModelProvider> Resolve(
      const std::string& provider_id) const;

 private:
  std::unordered_map<std::string, std::shared_ptr<LanguageModelProvider>>
      providers_;
};

}  // namespace yac::provider
