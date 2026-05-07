#pragma once

#include "core_types/typed_ids.hpp"
#include "provider/language_model_provider.hpp"

#include <memory>
#include <unordered_map>

namespace yac::provider {

class ProviderRegistry {
 public:
  void Register(std::shared_ptr<LanguageModelProvider> provider);
  [[nodiscard]] std::shared_ptr<LanguageModelProvider> Resolve(
      const ::yac::ProviderId& provider_id) const;

 private:
  std::unordered_map<::yac::ProviderId, std::shared_ptr<LanguageModelProvider>>
      providers_;
};

}  // namespace yac::provider
