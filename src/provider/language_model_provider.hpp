#pragma once

#include "chat/types.hpp"

#include <functional>
#include <stop_token>
#include <string>

namespace yac::provider {

using ChatEventSink = std::function<void(chat::ChatEvent)>;

class LanguageModelProvider {
 public:
  LanguageModelProvider() = default;
  virtual ~LanguageModelProvider() = default;

  LanguageModelProvider(const LanguageModelProvider&) = delete;
  LanguageModelProvider& operator=(const LanguageModelProvider&) = delete;
  LanguageModelProvider(LanguageModelProvider&&) = delete;
  LanguageModelProvider& operator=(LanguageModelProvider&&) = delete;

  [[nodiscard]] virtual std::string Id() const = 0;
  virtual void CompleteStream(const chat::ChatRequest& request,
                              ChatEventSink sink,
                              std::stop_token stop_token) = 0;
};

}  // namespace yac::provider
