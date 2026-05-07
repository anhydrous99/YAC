#include "provider/provider_registry.hpp"

#include <utility>

namespace yac::provider {

void ProviderRegistry::Register(
    std::shared_ptr<LanguageModelProvider> provider) {
  if (provider == nullptr) {
    return;
  }
  ::yac::ProviderId id{provider->Id()};
  providers_[std::move(id)] = std::move(provider);
}

std::shared_ptr<LanguageModelProvider> ProviderRegistry::Resolve(
    const ::yac::ProviderId& provider_id) const {
  auto it = providers_.find(provider_id);
  if (it == providers_.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace yac::provider
