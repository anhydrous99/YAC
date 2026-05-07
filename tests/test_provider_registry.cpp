#include "provider/provider_registry.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::provider::LanguageModelProvider;
using yac::provider::ProviderRegistry;

namespace {

class MockProvider final : public LanguageModelProvider {
 public:
  explicit MockProvider(std::string id) : id_(std::move(id)) {}

  [[nodiscard]] std::string Id() const override { return id_; }

  void CompleteStream(const yac::chat::ChatRequest&,
                      yac::provider::ChatEventSink, std::stop_token) override {}

 private:
  std::string id_;
};

std::shared_ptr<MockProvider> MakeMock(std::string id) {
  return std::make_shared<MockProvider>(std::move(id));
}

}  // namespace

TEST_CASE("ProviderRegistry: Register then Resolve returns the same provider") {
  ProviderRegistry registry;
  auto provider = MakeMock("alpha");
  registry.Register(provider);

  auto resolved = registry.Resolve(::yac::ProviderId{"alpha"});
  REQUIRE(resolved != nullptr);
  CHECK(resolved.get() == provider.get());
  CHECK(resolved->Id() == "alpha");
}

TEST_CASE("ProviderRegistry: second Register with same id replaces the first") {
  ProviderRegistry registry;
  auto first = MakeMock("p");
  auto second = MakeMock("p");

  registry.Register(first);
  registry.Register(second);

  auto resolved = registry.Resolve(::yac::ProviderId{"p"});
  REQUIRE(resolved != nullptr);
  CHECK(resolved.get() == second.get());
  CHECK(resolved.get() != first.get());
}

TEST_CASE("ProviderRegistry: Resolve of unknown id returns nullptr") {
  ProviderRegistry registry;

  CHECK(registry.Resolve(::yac::ProviderId{"missing"}) == nullptr);
  CHECK(registry.Resolve(::yac::ProviderId{""}) == nullptr);
  CHECK(registry.Resolve(::yac::ProviderId{"ALPHA"}) == nullptr);

  registry.Register(MakeMock("real"));
  CHECK(registry.Resolve(::yac::ProviderId{"REAL"}) == nullptr);
  CHECK(registry.Resolve(::yac::ProviderId{"real2"}) == nullptr);
}

TEST_CASE(
    "ProviderRegistry: multiple providers are independently addressable") {
  ProviderRegistry registry;
  auto a = MakeMock("alpha");
  auto b = MakeMock("beta");
  auto c = MakeMock("gamma");

  registry.Register(a);
  registry.Register(b);
  registry.Register(c);

  CHECK(registry.Resolve(::yac::ProviderId{"alpha"}).get() == a.get());
  CHECK(registry.Resolve(::yac::ProviderId{"beta"}).get() == b.get());
  CHECK(registry.Resolve(::yac::ProviderId{"gamma"}).get() == c.get());
  CHECK(registry.Resolve(::yac::ProviderId{"delta"}) == nullptr);
}

TEST_CASE("ProviderRegistry: Register nullptr is a no-op") {
  ProviderRegistry registry;

  REQUIRE_NOTHROW(registry.Register(nullptr));

  CHECK(registry.Resolve(::yac::ProviderId{""}) == nullptr);
  CHECK(registry.Resolve(::yac::ProviderId{"any"}) == nullptr);
}

TEST_CASE(
    "ProviderRegistry: concurrent Resolve is safe with 4 threads x 100 "
    "iters") {
  ProviderRegistry registry;
  constexpr int kNumThreads = 4;
  constexpr int kItersPerThread = 100;

  // Pre-register providers serially before any concurrent access.
  for (int i = 0; i < kNumThreads; ++i) {
    registry.Register(MakeMock("provider_" + std::to_string(i)));
  }

  std::atomic<int> hit_count{0};

  {
    std::vector<std::jthread> threads;
    threads.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
      threads.emplace_back([&registry, &hit_count, t] {
        const std::string id = "provider_" + std::to_string(t);
        for (int i = 0; i < kItersPerThread; ++i) {
          auto p = registry.Resolve(::yac::ProviderId{id});
          if (p != nullptr) {
            ++hit_count;
          }
        }
      });
    }
  }

  CHECK(hit_count.load() == kNumThreads * kItersPerThread);
}
