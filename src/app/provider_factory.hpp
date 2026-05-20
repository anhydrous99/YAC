#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <memory>

namespace yac::app {

[[nodiscard]] std::shared_ptr<provider::LanguageModelProvider>
MakeLanguageModelProvider(const chat::ProviderConfig& config);

}  // namespace yac::app
