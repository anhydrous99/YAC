#pragma once

#include "chat/types.hpp"

#include <chrono>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

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
  [[nodiscard]] virtual bool SupportsModelDiscovery() const { return false; }
  [[nodiscard]] virtual std::vector<chat::ModelInfo> ListModels(
      std::chrono::milliseconds timeout) {
    (void)timeout;
    return {};
  }
  // Best-effort context-window lookup for a model id. Returns 0 for unknown
  // ids; callers fall through to the cross-provider table in
  // `app/model_context_windows.cpp`. Implementations should consult any
  // discovered cache (populated from `ListModels`) before any built-in table.
  [[nodiscard]] virtual int GetContextWindow(
      const std::string& model_id) const {
    (void)model_id;
    return 0;
  }
  virtual void CompleteStream(const chat::ChatRequest& request,
                              ChatEventSink sink,
                              std::stop_token stop_token) = 0;
};

}  // namespace yac::provider
