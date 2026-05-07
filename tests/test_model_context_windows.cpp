#include "provider/model_context_windows.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::provider::LookupContextWindow;

TEST_CASE("LookupContextWindow returns zero for unknown ids") {
  REQUIRE(LookupContextWindow("") == 0);
  REQUIRE(LookupContextWindow("totally-made-up-model") == 0);
}

TEST_CASE("LookupContextWindow resolves exact matches") {
  REQUIRE(LookupContextWindow("gpt-4o") == 128000);
  REQUIRE(LookupContextWindow("gpt-4o-mini") == 128000);
}

TEST_CASE("LookupContextWindow falls back to prefix families") {
  REQUIRE(LookupContextWindow("gpt-4o-2024-11-20") == 128000);
  REQUIRE(LookupContextWindow("gpt-4.1-preview") == 1000000);
  REQUIRE(LookupContextWindow("o3-mini") == 200000);
  REQUIRE(LookupContextWindow("claude-sonnet-4-6") == 200000);
  REQUIRE(LookupContextWindow("glm-4.6-air") == 200000);
  REQUIRE(LookupContextWindow("glm-4.5-flash") == 128000);
  REQUIRE(LookupContextWindow("glm-5.1") == 200000);
  REQUIRE(LookupContextWindow("glm-5") == 200000);
  REQUIRE(LookupContextWindow("glm-4.7") == 200000);
}

TEST_CASE("LookupContextWindow resolves Bedrock models") {
  REQUIRE(LookupContextWindow("anthropic.claude-3-5-haiku-20241022-v1:0") ==
          200000);
  REQUIRE(LookupContextWindow("amazon.nova-pro-v1:0") == 300000);
  REQUIRE(LookupContextWindow("meta.llama3-1-70b-instruct-v1:0") == 128000);
  REQUIRE(LookupContextWindow("mistral.mistral-large-2407-v1:0") == 128000);
}

TEST_CASE("LookupContextWindow strips inference profile prefixes") {
  REQUIRE(LookupContextWindow("us.anthropic.claude-3-5-haiku-20241022-v1:0") ==
          200000);
  REQUIRE(LookupContextWindow("eu.anthropic.claude-3-5-haiku-20241022-v1:0") ==
          200000);
  REQUIRE(LookupContextWindow(
              "apac.anthropic.claude-3-5-haiku-20241022-v1:0") == 200000);
  REQUIRE(LookupContextWindow("global.amazon.nova-pro-v1:0") == 300000);
  REQUIRE(LookupContextWindow("us.meta.llama3-1-70b-instruct-v1:0") == 128000);
}

TEST_CASE("LookupContextWindow returns zero for unknown Bedrock models") {
  REQUIRE(LookupContextWindow("nonexistent.model:0") == 0);
  REQUIRE(LookupContextWindow("us.nonexistent.model:0") == 0);
}
