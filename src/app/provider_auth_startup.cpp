#include "app/provider_auth_startup.hpp"

#include "provider/openai_chat_provider.hpp"

#include <exception>

namespace yac::app {

void AppendProviderAuthStartupIssues(
    const chat::ProviderConfig& config,
    const provider::LanguageModelProvider& provider,
    std::vector<chat::ConfigIssue>& issues) {
  if (config.id.value != "openai") {
    return;
  }

  const auto* openai_provider =
      dynamic_cast<const provider::OpenAiChatProvider*>(&provider);
  if (openai_provider == nullptr) {
    return;
  }

  try {
    if (openai_provider->ResolveEffectiveAuthSource() !=
        provider::OpenAiChatProvider::EffectiveAuthSource::None) {
      return;
    }
  } catch (const std::exception&) {
    issues.push_back({
        .severity = chat::ConfigIssueSeverity::Warning,
        .message = "OpenAI stored auth could not be read",
        .detail = "Stored OpenAI auth is unreadable or invalid. Run `yac auth "
                  "openai login` or `yac auth openai set-api-key --stdin` to "
                  "refresh it.",
    });
    return;
  }

  issues.push_back({
      .severity = chat::ConfigIssueSeverity::Warning,
      .message = "OpenAI auth is not configured",
      .detail = "Set " + config.api_key_env +
                ", run `yac auth openai login`, run `yac auth openai "
                "set-api-key --stdin`, or set provider.api_key before "
                "sending a request.",
  });
}

}  // namespace yac::app
