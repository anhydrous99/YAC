#include "provider/reasoning_effort_capability.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::chat::ProviderConfig;
using yac::chat::ReasoningEffort;
using yac::provider::AllReasoningEffortValues;
using yac::provider::LookupReasoningEffortCapability;
using yac::provider::ParseReasoningEffort;
using yac::provider::ReasoningEffortCapability;
using yac::provider::ReasoningEffortProtocol;
using yac::provider::ToString;

namespace {

ProviderConfig MakeConfig(std::string provider_id,
                          std::string base_url = "https://api.openai.com/v1/") {
  ProviderConfig config;
  config.id = yac::ProviderId(provider_id);
  config.base_url = base_url;
  return config;
}

void RequireCapability(const std::optional<ReasoningEffortCapability>& cap,
                       std::vector<ReasoningEffort> allowed_values,
                       std::vector<ReasoningEffortProtocol> protocols) {
  REQUIRE(cap.has_value());
  REQUIRE(cap->allowed_values == allowed_values);
  REQUIRE(cap->protocols == protocols);
}

}  // namespace

TEST_CASE("ParseReasoningEffort accepts lowercase values") {
  REQUIRE(ParseReasoningEffort("none") == ReasoningEffort::None);
  REQUIRE(ParseReasoningEffort("minimal") == ReasoningEffort::Minimal);
  REQUIRE(ParseReasoningEffort("low") == ReasoningEffort::Low);
  REQUIRE(ParseReasoningEffort("medium") == ReasoningEffort::Medium);
  REQUIRE(ParseReasoningEffort("high") == ReasoningEffort::High);
  REQUIRE(ParseReasoningEffort("xhigh") == ReasoningEffort::XHigh);
}

TEST_CASE("ParseReasoningEffort rejects unknown casing and values") {
  REQUIRE_FALSE(ParseReasoningEffort(""));
  REQUIRE_FALSE(ParseReasoningEffort("None"));
  REQUIRE_FALSE(ParseReasoningEffort("highest"));
}

TEST_CASE("ToString returns lowercase effort values") {
  REQUIRE(ToString(ReasoningEffort::None) == "none");
  REQUIRE(ToString(ReasoningEffort::Minimal) == "minimal");
  REQUIRE(ToString(ReasoningEffort::Low) == "low");
  REQUIRE(ToString(ReasoningEffort::Medium) == "medium");
  REQUIRE(ToString(ReasoningEffort::High) == "high");
  REQUIRE(ToString(ReasoningEffort::XHigh) == "xhigh");
}

TEST_CASE("AllReasoningEffortValues returns every effort in order") {
  REQUIRE(AllReasoningEffortValues() ==
          std::vector<ReasoningEffort>{
              ReasoningEffort::None, ReasoningEffort::Minimal,
              ReasoningEffort::Low, ReasoningEffort::Medium,
              ReasoningEffort::High, ReasoningEffort::XHigh});
}

TEST_CASE("LookupReasoningEffortCapability returns exact openai allowlists") {
  SECTION("gpt-5.5") {
    const auto capability =
        LookupReasoningEffortCapability(MakeConfig("openai"), "gpt-5.5");
    RequireCapability(capability,
                      {ReasoningEffort::None, ReasoningEffort::Minimal,
                       ReasoningEffort::Low, ReasoningEffort::Medium,
                       ReasoningEffort::High, ReasoningEffort::XHigh},
                      {ReasoningEffortProtocol::OpenAiResponses,
                       ReasoningEffortProtocol::OpenAiChatCompletions});
  }

  SECTION("gpt-5-pro") {
    const auto capability =
        LookupReasoningEffortCapability(MakeConfig("openai"), "gpt-5-pro");
    RequireCapability(capability, {ReasoningEffort::High},
                      {ReasoningEffortProtocol::OpenAiResponses,
                       ReasoningEffortProtocol::OpenAiChatCompletions});
  }

  SECTION("gpt-5.3-codex") {
    const auto capability =
        LookupReasoningEffortCapability(MakeConfig("openai"), "gpt-5.3-codex");
    RequireCapability(capability,
                      {ReasoningEffort::Minimal, ReasoningEffort::Low,
                       ReasoningEffort::Medium, ReasoningEffort::High,
                       ReasoningEffort::XHigh},
                      {ReasoningEffortProtocol::OpenAiResponses,
                       ReasoningEffortProtocol::OpenAiChatCompletions});
  }

  SECTION("o3") {
    const auto capability =
        LookupReasoningEffortCapability(MakeConfig("openai"), "o3");
    RequireCapability(
        capability,
        {ReasoningEffort::Low, ReasoningEffort::Medium, ReasoningEffort::High},
        {ReasoningEffortProtocol::OpenAiResponses,
         ReasoningEffortProtocol::OpenAiChatCompletions});
  }
}

TEST_CASE("LookupReasoningEffortCapability recognizes openai-compatible") {
  const auto capability = LookupReasoningEffortCapability(
      MakeConfig("openai-compatible"), "gpt-5.4-mini");
  RequireCapability(capability,
                    {ReasoningEffort::Minimal, ReasoningEffort::Low,
                     ReasoningEffort::Medium, ReasoningEffort::High},
                    {ReasoningEffortProtocol::OpenAiChatCompletions});
}

TEST_CASE("LookupReasoningEffortCapability rejects unsupported providers") {
  REQUIRE_FALSE(LookupReasoningEffortCapability(MakeConfig("zai"), "gpt-5.5"));
  REQUIRE_FALSE(
      LookupReasoningEffortCapability(MakeConfig("bedrock"), "gpt-5.5"));
  REQUIRE_FALSE(LookupReasoningEffortCapability(
      MakeConfig("openai-compatible", "https://example.com/v1/"), "gpt-5.5"));
}

TEST_CASE("LookupReasoningEffortCapability rejects unsupported models") {
  REQUIRE_FALSE(
      LookupReasoningEffortCapability(MakeConfig("openai"), "gpt-5.6"));
  REQUIRE_FALSE(
      LookupReasoningEffortCapability(MakeConfig("openai"), "codex-mini"));
}
