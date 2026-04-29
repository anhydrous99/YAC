#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::testing {

// LambdaMockProvider replaces ad-hoc per-test fakes. The behaviour passed in
// the Handler closes over any test-specific state; the provider itself owns
// only the recording boilerplate (mutex + per-call request log + Id).
//
// Recording is always-on. The cost is one mutex + push_back per call.
class LambdaMockProvider final : public yac::provider::LanguageModelProvider {
 public:
  using Handler =
      std::function<void(const yac::chat::ChatRequest&,
                         yac::provider::ChatEventSink, std::stop_token)>;

  LambdaMockProvider(std::string id, Handler handler)
      : id_(std::move(id)), handler_(std::move(handler)) {}

  [[nodiscard]] std::string Id() const override { return id_; }

  void CompleteStream(const yac::chat::ChatRequest& request,
                      yac::provider::ChatEventSink sink,
                      std::stop_token stop_token) override {
    {
      std::lock_guard lock(mutex_);
      requests_.push_back(request);
    }
    if (handler_) {
      handler_(request, std::move(sink), stop_token);
    }
  }

  [[nodiscard]] std::vector<yac::chat::ChatRequest> Requests() const {
    std::lock_guard lock(mutex_);
    return requests_;
  }

  [[nodiscard]] yac::chat::ChatRequest LastRequest() const {
    std::lock_guard lock(mutex_);
    REQUIRE_FALSE(requests_.empty());
    return requests_.back();
  }

  [[nodiscard]] size_t RequestCount() const {
    std::lock_guard lock(mutex_);
    return requests_.size();
  }

 private:
  std::string id_;
  Handler handler_;
  mutable std::mutex mutex_;
  std::vector<yac::chat::ChatRequest> requests_;
};

}  // namespace yac::testing
