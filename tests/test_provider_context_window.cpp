#include "app/model_context_windows.hpp"
#include "provider/openai_compatible_chat_provider.hpp"
#include "provider/zai_context_windows.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::app::LookupContextWindow;
using yac::app::ResolveContextWindow;
using yac::provider::KnownZaiContextWindow;
using yac::provider::OpenAiCompatibleChatProvider;

namespace {

OpenAiCompatibleChatProvider MakeZaiProvider() {
  yac::chat::ProviderConfig config;
  config.id = "zai";
  config.model = "glm-5.1";
  config.base_url = "https://api.z.ai/api/coding/paas/v4";
  config.api_key_env = "ZAI_API_KEY";
  return OpenAiCompatibleChatProvider(std::move(config));
}

OpenAiCompatibleChatProvider MakeOpenAiProvider() {
  yac::chat::ProviderConfig config;
  config.id = "openai-compatible";
  config.model = "gpt-4o-mini";
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
