#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <vector>

namespace yac::app {

void AppendProviderAuthStartupIssues(
    const chat::ProviderConfig& config,
    const provider::LanguageModelProvider& provider,
    std::vector<chat::ConfigIssue>& issues);

}
