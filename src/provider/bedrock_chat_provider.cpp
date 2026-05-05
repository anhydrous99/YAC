#include "provider/bedrock_chat_provider.hpp"

#include "chat/types.hpp"
#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/BedrockRuntimeClient.h>
#include <aws/core/client/ClientConfiguration.h>
#include <chrono>
#include <exception>
#include <stop_token>
#include <string>
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
//
// On normal completion (no cancellation), the destructor signals an internal
// stop_source so the polling loop exits promptly and join() returns within at
// most one poll interval. DisableRequestProcessing() is only invoked when the
// caller-supplied stop_token was actually requested.
class CancellationWatchdog {
 public:
  CancellationWatchdog(std::stop_token stop_token,
                       Aws::BedrockRuntime::BedrockRuntimeClient* client) {
    std::stop_token completion_token = completion_source_.get_token();
    thread_ = std::thread([external = std::move(stop_token),
                           completion = std::move(completion_token), client]() {
      while (!external.stop_requested() && !completion.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (external.stop_requested()) {
        client->DisableRequestProcessing();
      }
    });
  }

  ~CancellationWatchdog() {
    completion_source_.request_stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  CancellationWatchdog(const CancellationWatchdog&) = delete;
  CancellationWatchdog& operator=(const CancellationWatchdog&) = delete;
  CancellationWatchdog(CancellationWatchdog&&) = delete;
  CancellationWatchdog& operator=(CancellationWatchdog&&) = delete;

 private:
  std::stop_source completion_source_;
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
  try {
    Aws::Client::ClientConfiguration client_config;
    const auto& opts = impl_->config.options;
    if (auto it = opts.find("region");
        it != opts.end() && !it->second.empty()) {
      client_config.region = it->second;
    }
    if (auto it = opts.find("endpoint_override");
        it != opts.end() && !it->second.empty()) {
      client_config.endpointOverride = it->second;
    }

    Aws::BedrockRuntime::BedrockRuntimeClient client(client_config);

    CancellationWatchdog watchdog(stop_token, &client);

    auto req_data = BuildConverseStreamRequest(request, impl_->config);

    auto handler_handle =
        MakeStreamHandler(sink, impl_->config.id, request.model);
    req_data.request.SetEventStreamHandler(GetSdkHandler(handler_handle));

    auto outcome = client.ConverseStream(req_data.request);

    if (!outcome.IsSuccess()) {
      const auto& err = outcome.GetError();
      const auto& name = err.GetExceptionName();
      const auto& message = err.GetMessage();
      auto error_event =
          MapBedrockSyncError(std::string(name.c_str(), name.size()),
                              std::string(message.c_str(), message.size()));
      error_event.provider_id = impl_->config.id;
      error_event.model = request.model;
      sink(chat::ChatEvent{std::move(error_event)});
      sink(chat::ChatEvent{chat::FinishedEvent{}});
      return;
    }

    if (stop_token.stop_requested()) {
      sink(chat::ChatEvent{chat::CancelledEvent{}});
    } else {
      sink(chat::ChatEvent{chat::FinishedEvent{}});
    }
  } catch (const std::exception& e) {
    sink(chat::ChatEvent{
        chat::ErrorEvent{.text = std::string("Bedrock exception: ") + e.what(),
                         .provider_id = impl_->config.id,
                         .model = request.model}});
    sink(chat::ChatEvent{chat::FinishedEvent{}});
  }
}

bool BedrockChatProvider::SupportsModelDiscovery() const {
  return false;
}

int BedrockChatProvider::GetContextWindow(const std::string& model_id) const {
  (void)model_id;
  return 0;
}

}  // namespace yac::provider
