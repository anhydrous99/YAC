#include "app/model_discovery.hpp"
#include "provider/provider_registry.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::provider;

namespace {

class FakeDiscoveryProvider : public LanguageModelProvider {
 public:
  FakeDiscoveryProvider(std::string provider_id, bool supports_discovery,
                        std::vector<ModelInfo> models = {},
                        bool throws_on_list = false)
      : provider_id_(std::move(provider_id)),
        supports_discovery_(supports_discovery),
        models_(std::move(models)),
        throws_on_list_(throws_on_list) {}

  [[nodiscard]] std::string Id() const override { return provider_id_; }

  [[nodiscard]] bool SupportsModelDiscovery() const override {
    return supports_discovery_;
  }

  [[nodiscard]] std::vector<ModelInfo> ListModels(
      std::chrono::milliseconds timeout) override {
    last_timeout_ = timeout;
    ++list_models_calls_;
    if (throws_on_list_) {
      throw std::runtime_error("discovery failed");
    }
    return models_;
  }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      [[maybe_unused]] ChatEventSink sink,
                      [[maybe_unused]] std::stop_token stop_token) override {}

  [[nodiscard]] int ListModelsCalls() const { return list_models_calls_; }

  [[nodiscard]] std::chrono::milliseconds LastTimeout() const {
    return last_timeout_;
  }

 private:
  std::string provider_id_;
  bool supports_discovery_ = false;
  std::vector<ModelInfo> models_;
  bool throws_on_list_ = false;
  int list_models_calls_ = 0;
  std::chrono::milliseconds last_timeout_{0};
};

ChatConfig MakeConfig(std::string provider_id = "openai-compatible",
                      std::string model = "") {
  ChatConfig config;
  config.provider_id = std::move(provider_id);
  config.model = std::move(model);
  return config;
}

}  // namespace

TEST_CASE("DiscoverModels returns provider models when discovery succeeds") {
  FakeDiscoveryProvider provider(
      "openai-compatible", true,
      {{.id = "gpt-4.1", .display_name = "GPT-4.1"},
       {.id = "gpt-4.1-mini", .display_name = "GPT-4.1 Mini"}});

  const auto models = DiscoverModels(provider, MakeConfig("openai-compatible"));

  REQUIRE(models.size() == 2);
  REQUIRE(models[0].id == "gpt-4.1");
  REQUIRE(models[0].display_name == "GPT-4.1");
  REQUIRE(models[1].id == "gpt-4.1-mini");
  REQUIRE(provider.ListModelsCalls() == 1);
  REQUIRE(provider.LastTimeout() == std::chrono::seconds(5));
}

TEST_CASE("DiscoverModelsWithStatus reports successful discovery") {
  FakeDiscoveryProvider provider(
      "openai-compatible", true,
      {{.id = "gpt-4.1", .display_name = "GPT-4.1"}});

  const auto result =
      DiscoverModelsWithStatus(provider, MakeConfig("openai-compatible"));

  REQUIRE(result.status == ModelDiscoveryStatus::Success);
  REQUIRE(result.models.size() == 1);
  REQUIRE(result.message.empty());
}

TEST_CASE("DiscoverModels uses Z.ai fallback models when zai returns empty") {
  FakeDiscoveryProvider provider("zai", true);

  const auto models = DiscoverModels(provider, MakeConfig("zai"));

  REQUIRE(provider.ListModelsCalls() == 1);
  REQUIRE(models.size() == 6);
  REQUIRE(models[0].id == "glm-5.1");
  REQUIRE(models[1].id == "glm-5");
  REQUIRE(models[2].id == "glm-4.7");
}

TEST_CASE(
    "DiscoverModels uses Z.ai fallback models when zai discovery throws") {
  FakeDiscoveryProvider provider("zai", true, {}, true);

  const auto models = DiscoverModels(provider, MakeConfig("zai"));

  REQUIRE(provider.ListModelsCalls() == 1);
  REQUIRE(models.size() == 6);
  REQUIRE(models.front().id == "glm-5.1");
}

TEST_CASE("DiscoverModelsWithStatus reports Z.ai fallback") {
  FakeDiscoveryProvider provider("zai", true, {}, true);

  const auto result = DiscoverModelsWithStatus(provider, MakeConfig("zai"));

  REQUIRE(result.status == ModelDiscoveryStatus::Fallback);
  REQUIRE(result.models.size() == 6);
  REQUIRE_FALSE(result.message.empty());
}

TEST_CASE(
    "DiscoverModels returns empty when provider does not support discovery") {
  FakeDiscoveryProvider provider(
      "openai-compatible", false,
      {{.id = "ignored", .display_name = "Ignored"}});

  const auto models = DiscoverModels(provider, MakeConfig("openai-compatible"));

  REQUIRE(models.empty());
  REQUIRE(provider.ListModelsCalls() == 0);
}

TEST_CASE("DiscoverModelsWithStatus reports unsupported discovery") {
  FakeDiscoveryProvider provider("openai-compatible", false);

  const auto result =
      DiscoverModelsWithStatus(provider, MakeConfig("openai-compatible"));

  REQUIRE(result.status == ModelDiscoveryStatus::Unsupported);
  REQUIRE(result.models.empty());
  REQUIRE(provider.ListModelsCalls() == 0);
}

TEST_CASE(
    "DiscoverModels preserves configured model when missing from results") {
  FakeDiscoveryProvider provider(
      "openai-compatible", true,
      {{.id = "gpt-4.1", .display_name = "GPT-4.1"},
       {.id = "gpt-4.1-mini", .display_name = "GPT-4.1 Mini"}});

  const auto models = DiscoverModels(
      provider, MakeConfig("openai-compatible", "configured-model"));

  REQUIRE(models.size() == 3);
  REQUIRE(models[0].id == "configured-model");
  REQUIRE(models[0].display_name == "configured-model");
  REQUIRE(models[1].id == "gpt-4.1");
}

TEST_CASE("DiscoverModels does not apply Z.ai fallback to non-zai providers") {
  FakeDiscoveryProvider provider("openai-compatible", true);

  const auto models = DiscoverModels(provider, MakeConfig("openai-compatible"));

  REQUIRE(provider.ListModelsCalls() == 1);
  REQUIRE(models.empty());
}

TEST_CASE(
    "DiscoverModels preserves configured model for non-Z.ai empty results") {
  FakeDiscoveryProvider provider("openai-compatible", true);

  const auto models =
      DiscoverModels(provider, MakeConfig("openai-compatible", "my-model"));

  REQUIRE(models.size() == 1);
  REQUIRE(models[0].id == "my-model");
}

TEST_CASE(
    "DiscoverModels preserves configured model for non-Z.ai discovery throw") {
  FakeDiscoveryProvider provider("openai-compatible", true, {}, true);

  const auto models =
      DiscoverModels(provider, MakeConfig("openai-compatible", "my-model"));

  REQUIRE(models.size() == 1);
  REQUIRE(models[0].id == "my-model");
}

TEST_CASE(
    "DiscoverModelsWithStatus keeps configured model when discovery fails") {
  FakeDiscoveryProvider provider("openai-compatible", true, {}, true);

  const auto result = DiscoverModelsWithStatus(
      provider, MakeConfig("openai-compatible", "my-model"));

  REQUIRE(result.status == ModelDiscoveryStatus::Failed);
  REQUIRE(result.models.size() == 1);
  REQUIRE(result.models[0].id == "my-model");
  REQUIRE_FALSE(result.message.empty());
}

TEST_CASE(
    "ProviderRegistry resolves a registered fake provider for discovery") {
  auto provider = std::make_shared<FakeDiscoveryProvider>(
      "zai", true,
      std::vector<ModelInfo>{{.id = "glm-4.6", .display_name = "GLM-4.6"}});
  ProviderRegistry registry;

  registry.Register(provider);
  const auto resolved = registry.Resolve("zai");

  REQUIRE(resolved == provider);
  const auto models = DiscoverModels(*resolved, MakeConfig("zai"));
  REQUIRE(models.size() == 1);
  REQUIRE(models[0].id == "glm-4.6");
}

TEST_CASE("ProviderRegistry ignores null providers and missing IDs") {
  ProviderRegistry registry;

  registry.Register(nullptr);

  REQUIRE(registry.Resolve("missing") == nullptr);
}
