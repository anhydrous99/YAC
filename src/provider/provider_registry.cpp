#include "provider/provider_registry.hpp"

#include <utility>

namespace yac::provider {

void ProviderRegistry::Register(
    std::shared_ptr<LanguageModelProvider> provider) {
  if (provider == nullptr) {
    return;
  }
  providers_[provider->Id()] = std::move(provider);
}

std::shared_ptr<LanguageModelProvider> ProviderRegistry::Resolve(
    const std::string& provider_id) const {
  auto it = providers_.find(provider_id);
  if (it == providers_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace yac::provider
