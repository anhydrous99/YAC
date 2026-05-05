#include "provider/bedrock_chat_provider.hpp"

#include <aws/bedrock-runtime/BedrockRuntimeClient.h>
#include <chrono>
#include <thread>
#include <utility>

namespace yac::provider {

namespace {

// Polls a stop_token at 50 ms cadence and, on cancellation, calls
// BedrockRuntimeClient::DisableRequestProcessing() from a sibling thread to
// abort any in-flight Bedrock streaming request synchronously running on the
// caller thread.
//
// Owned per-call by CompleteStream(); RAII-joins on destruction so the worker
// never outlives the BedrockRuntimeClient pointer it holds.
class CancellationWatchdog {
 public:
  CancellationWatchdog(std::stop_token stop_token,
                       Aws::BedrockRuntime::BedrockRuntimeClient* client)
      : thread_([stop_token = std::move(stop_token), client]() {
          while (!stop_token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (stop_token.stop_requested()) {
              client->DisableRequestProcessing();
              return;
            }
          }
          client->DisableRequestProcessing();
        }) {}

  ~CancellationWatchdog() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  CancellationWatchdog(const CancellationWatchdog&) = delete;
  CancellationWatchdog& operator=(const CancellationWatchdog&) = delete;
  CancellationWatchdog(CancellationWatchdog&&) = delete;
  CancellationWatchdog& operator=(CancellationWatchdog&&) = delete;

 private:
  std::thread thread_;
};

}  // namespace

struct BedrockChatProvider::Impl {
  chat::ProviderConfig config;
};

BedrockChatProvider::BedrockChatProvider(const chat::ProviderConfig& config)
    : impl_(std::make_unique<Impl>(Impl{config})) {}

BedrockChatProvider::~BedrockChatProvider() = default;

std::string BedrockChatProvider::Id() const {
  return "bedrock";
}

void BedrockChatProvider::CompleteStream(const chat::ChatRequest& request,
                                         ChatEventSink sink,
                                         std::stop_token stop_token) {
  (void)request;
  (void)stop_token;

  sink(chat::ChatEvent{
      chat::ErrorEvent{.text = "BedrockChatProvider not yet implemented"}});
  sink(chat::ChatEvent{chat::FinishedEvent{}});
}

bool BedrockChatProvider::SupportsModelDiscovery() const {
  return false;
}

int BedrockChatProvider::GetContextWindow(const std::string& model_id) const {
  (void)model_id;
  return 0;
}

}  // namespace yac::provider
