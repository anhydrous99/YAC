#include "provider/bedrock_chat_provider.hpp"
#include "provider/model_context_windows.hpp"
#include "provider/openai_compatible_chat_provider.hpp"
#include "provider/zai_context_windows.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::provider::BedrockChatProvider;
using yac::provider::KnownZaiContextWindow;
using yac::provider::LookupContextWindow;
using yac::provider::OpenAiCompatibleChatProvider;
using yac::provider::ResolveContextWindow;

namespace {

OpenAiCompatibleChatProvider MakeZaiProvider() {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"zai"};
  config.model = ::yac::ModelId{"glm-5.1"};
  config.base_url = "https://api.z.ai/api/coding/paas/v4";
  config.api_key_env = "ZAI_API_KEY";
  return OpenAiCompatibleChatProvider(std::move(config));
}

OpenAiCompatibleChatProvider MakeOpenAiProvider() {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai-compatible"};
  config.model = ::yac::ModelId{"gpt-4o-mini"};
  return OpenAiCompatibleChatProvider(std::move(config));
}

}  // namespace

TEST_CASE("KnownZaiContextWindow covers documented glm-* models") {
  REQUIRE(KnownZaiContextWindow("glm-5.1") == 200000);
  REQUIRE(KnownZaiContextWindow("glm-5") == 200000);
  REQUIRE(KnownZaiContextWindow("glm-4.7") == 200000);
  REQUIRE(KnownZaiContextWindow("glm-4.6") == 200000);
  REQUIRE(KnownZaiContextWindow("glm-4.5") == 128000);
  REQUIRE(KnownZaiContextWindow("glm-4.5-air") == 128000);
  REQUIRE(KnownZaiContextWindow("glm-4") == 128000);
  REQUIRE(KnownZaiContextWindow("") == 0);
  REQUIRE(KnownZaiContextWindow("not-a-glm-model") == 0);
}

TEST_CASE("KnownZaiContextWindow values match cross-provider table") {
  // The provider built-in table must agree with the cross-provider table for
  // glm-* prefixes; otherwise the source-of-truth chain would disagree once a
  // non-zai provider serves a glm-* model.
  REQUIRE(KnownZaiContextWindow("glm-5.1") == LookupContextWindow("glm-5.1"));
  REQUIRE(KnownZaiContextWindow("glm-5") == LookupContextWindow("glm-5"));
  REQUIRE(KnownZaiContextWindow("glm-4.7") == LookupContextWindow("glm-4.7"));
  REQUIRE(KnownZaiContextWindow("glm-4.6") == LookupContextWindow("glm-4.6"));
  REQUIRE(KnownZaiContextWindow("glm-4.5") == LookupContextWindow("glm-4.5"));
  REQUIRE(KnownZaiContextWindow("glm-4.5-air") ==
          LookupContextWindow("glm-4.5-air"));
}

TEST_CASE(
    "OpenAiCompatibleChatProvider::GetContextWindow consults discovered "
    "cache first") {
  auto provider = MakeOpenAiProvider();
  REQUIRE(provider.GetContextWindow("custom-model") == 0);

  provider.SeedDiscoveredContextWindowForTest("custom-model", 250000);
  REQUIRE(provider.GetContextWindow("custom-model") == 250000);

  // Discovered cache wins even when a hard-coded value exists in the
  // cross-provider table — the cache reflects the live API.
  provider.SeedDiscoveredContextWindowForTest("gpt-4o-mini", 64000);
  REQUIRE(provider.GetContextWindow("gpt-4o-mini") == 64000);
}

TEST_CASE(
    "OpenAiCompatibleChatProvider::GetContextWindow uses zai built-in table "
    "when id == zai and discovery missed") {
  auto provider = MakeZaiProvider();
  REQUIRE(provider.GetContextWindow("glm-5.1") == 200000);
  REQUIRE(provider.GetContextWindow("glm-4.5-air") == 128000);

  // Non-glm model on the zai provider still falls through to 0 — caller is
  // expected to consult LookupContextWindow next.
  REQUIRE(provider.GetContextWindow("not-a-glm-model") == 0);
}

TEST_CASE(
    "OpenAiCompatibleChatProvider::GetContextWindow returns 0 for unknown "
    "models on a non-zai provider") {
  auto provider = MakeOpenAiProvider();
  REQUIRE(provider.GetContextWindow("glm-5.1") == 0);
  REQUIRE(provider.GetContextWindow("totally-made-up") == 0);
  REQUIRE(provider.GetContextWindow("") == 0);
}

TEST_CASE(
    "OpenAiCompatibleChatProvider::GetContextWindow manual override wins over "
    "all other sources") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"openai-compatible"};
  config.model = ::yac::ModelId{"unknown-model"};
  config.context_window = 32768;
  OpenAiCompatibleChatProvider provider(std::move(config));

  REQUIRE(provider.GetContextWindow("unknown-model") == 32768);

  // Override wins even over discovered cache.
  provider.SeedDiscoveredContextWindowForTest("unknown-model", 999);
  REQUIRE(provider.GetContextWindow("unknown-model") == 32768);

  REQUIRE(ResolveContextWindow(&provider, "unknown-model") == 32768);
}

TEST_CASE("ResolveContextWindow chains provider then cross-provider table") {
  auto provider = MakeZaiProvider();

  SECTION("Provider-advertised value wins over cross-provider table") {
    provider.SeedDiscoveredContextWindowForTest("gpt-4o-mini", 64000);
    REQUIRE(ResolveContextWindow(&provider, "gpt-4o-mini") == 64000);
  }

  SECTION("zai built-in table wins for glm-* on zai provider") {
    REQUIRE(ResolveContextWindow(&provider, "glm-5.1") == 200000);
  }

  SECTION("Falls through to cross-provider table when provider returns 0") {
    auto plain = MakeOpenAiProvider();
    REQUIRE(ResolveContextWindow(&plain, "claude-sonnet-4-6") == 200000);
  }

  SECTION("Null provider falls back to cross-provider table") {
    REQUIRE(ResolveContextWindow(nullptr, "gpt-4o") == 128000);
    REQUIRE(ResolveContextWindow(nullptr, "totally-unknown") == 0);
  }
}

TEST_CASE(
    "BedrockChatProvider::GetContextWindow returns 0 by default and defers "
    "to LookupContextWindow via ResolveContextWindow") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"bedrock"};
  config.model = ::yac::ModelId{"anthropic.claude-3-5-haiku-20241022-v1:0"};
  BedrockChatProvider provider(config);

  REQUIRE(provider.GetContextWindow("") == 0);
  // No override and no discovered cache -> provider returns 0 so the app
  // chain can consult LookupContextWindow.
  REQUIRE(provider.GetContextWindow(
              "anthropic.claude-3-5-haiku-20241022-v1:0") == 0);
  REQUIRE(ResolveContextWindow(
              &provider, "anthropic.claude-3-5-haiku-20241022-v1:0") == 200000);
  REQUIRE(ResolveContextWindow(&provider, "amazon.nova-pro-v1:0") == 300000);
  // Inference-profile prefix is handled by LookupContextWindow.
  REQUIRE(ResolveContextWindow(&provider,
                               "us.anthropic.claude-3-5-haiku-20241022-v1:0") ==
          200000);
}

TEST_CASE("BedrockChatProvider::GetContextWindow honours the manual override") {
  yac::chat::ProviderConfig config;
  config.id = ::yac::ProviderId{"bedrock"};
  config.model = ::yac::ModelId{"anthropic.claude-3-5-haiku-20241022-v1:0"};
  config.context_window = 50000;
  BedrockChatProvider provider(config);

  REQUIRE(provider.GetContextWindow(
              "anthropic.claude-3-5-haiku-20241022-v1:0") == 50000);
  // Override wins even for an unknown model id.
  REQUIRE(provider.GetContextWindow("totally-made-up.model") == 50000);
  // Empty model id still returns 0 — matches the OpenAI provider.
  REQUIRE(provider.GetContextWindow("") == 0);
  REQUIRE(ResolveContextWindow(
              &provider, "anthropic.claude-3-5-haiku-20241022-v1:0") == 50000);
}
