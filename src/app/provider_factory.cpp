#include "app/provider_factory.hpp"

#include "provider/bedrock_aws_api_guard.hpp"
#include "provider/bedrock_chat_provider.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <memory>

namespace yac::app {

namespace {

chat::ProviderConfig ToProviderConfig(const chat::ProviderConfig& config) {
  return chat::ProviderConfig{.id = config.id,
                              .model = config.model,
                              .api_key = config.api_key,
                              .api_key_env = config.api_key_env,
                              .base_url = config.base_url,
                              .system_prompt = config.system_prompt,
                              .options = config.options,
                              .context_window = config.context_window};
}

}  // namespace

std::shared_ptr<provider::LanguageModelProvider> MakeLanguageModelProvider(
    const chat::ProviderConfig& config) {
  if (config.id.value == "bedrock") {
    provider::EnsureAwsApiGuardInstalled();
    return std::make_shared<provider::BedrockChatProvider>(
        ToProviderConfig(config));
  }
  if (config.id.value == "openai") {
    return std::make_shared<provider::OpenAiChatProvider>(
        ToProviderConfig(config));
  }
  return std::make_shared<provider::OpenAiCompatibleChatProvider>(
      ToProviderConfig(config));
}

}  // namespace yac::app
