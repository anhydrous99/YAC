#include "app/provider_factory.hpp"
#include "chat/types.hpp"
#include "provider/bedrock_chat_provider.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("MakeLanguageModelProvider returns OpenAI provider for openai") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::OpenAiChatProvider*>(provider.get()) !=
          nullptr);
  REQUIRE(provider->Id() == "openai");
}

TEST_CASE("MakeLanguageModelProvider returns OpenAI-compatible provider for generic ids") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"custom-provider"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::OpenAiCompatibleChatProvider*>(
              provider.get()) != nullptr);
  REQUIRE(provider->Id() == "custom-provider");
}

TEST_CASE("MakeLanguageModelProvider returns Bedrock provider for bedrock") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"bedrock"};

  auto provider = yac::app::MakeLanguageModelProvider(config);

  REQUIRE(dynamic_cast<yac::provider::BedrockChatProvider*>(provider.get()) !=
          nullptr);
  REQUIRE(provider->Id() == "bedrock");
}
