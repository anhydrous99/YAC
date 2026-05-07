#include "provider/bedrock_chat_provider.hpp"

#include "chat/types.hpp"
#include "provider/bedrock_chat_protocol.hpp"
#include "tool_call/subprocess_runner.hpp"

#include <aws/bedrock-runtime/BedrockRuntimeClient.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/auth/AWSCredentialsProviderChain.h>
#include <aws/core/client/ClientConfiguration.h>
#include <chrono>
#include <exception>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace yac::provider {

namespace {

// Thin subclass of BedrockRuntimeClient that promotes DisableRequestProcessing
// to the public interface. The method is public in aws-sdk-cpp 1.11.580 (via
// the legacy AWSClient base) but protected in 1.11.790+ (after the client was
// rebased onto smithy::client::AwsSmithyClientBase). A using-declaration
// compiles cleanly in both versions: it's a no-op promotion when the base
// method is already public, and a real promotion when it is protected.
class YacBedrockRuntimeClient
    : public Aws::BedrockRuntime::BedrockRuntimeClient {
 public:
  using BedrockRuntimeClient::BedrockRuntimeClient;
  using BedrockRuntimeClient::DisableRequestProcessing;
};

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
                       YacBedrockRuntimeClient* client) {
    thread_ = std::jthread(
        [external = std::move(stop_token), client](std::stop_token completion) {
          while (!external.stop_requested() && !completion.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
          if (external.stop_requested()) {
            client->DisableRequestProcessing();
          }
        });
  }

  CancellationWatchdog(const CancellationWatchdog&) = delete;
  CancellationWatchdog& operator=(const CancellationWatchdog&) = delete;
  CancellationWatchdog(CancellationWatchdog&&) = delete;
  CancellationWatchdog& operator=(CancellationWatchdog&&) = delete;

 private:
  std::jthread thread_;
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

    // When options.profile is non-empty, use the named profile from
    // ~/.aws/credentials directly. Otherwise fall back to the SDK's default
    // chain (env vars, instance metadata, SSO, default profile).
    std::string profile_name;
    if (auto it = opts.find("profile");
        it != opts.end() && !it->second.empty()) {
      profile_name = it->second;
    }

    std::string refresh_cmd;
    if (auto it = opts.find("credential_refresh_command");
        it != opts.end() && !it->second.empty()) {
      refresh_cmd = it->second;
    }
    const int max_attempts = refresh_cmd.empty() ? 1 : 2;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
      if (stop_token.stop_requested()) {
        sink(chat::ChatEvent{chat::CancelledEvent{}});
        return;
      }

      std::unique_ptr<YacBedrockRuntimeClient> client;
      if (!profile_name.empty()) {
        auto creds =
            Aws::MakeShared<Aws::Auth::ProfileConfigFileAWSCredentialsProvider>(
                "yac-bedrock", profile_name.c_str());
        client =
            std::make_unique<YacBedrockRuntimeClient>(creds, client_config);
      } else {
        client = std::make_unique<YacBedrockRuntimeClient>(client_config);
      }

      CancellationWatchdog watchdog(stop_token, client.get());

      auto req_data = BuildConverseStreamRequest(request, impl_->config);
      auto handler_handle =
          MakeStreamHandler(sink, impl_->config.id, request.model);
      req_data.request.SetEventStreamHandler(GetSdkHandler(handler_handle));

      auto outcome = client->ConverseStream(req_data.request);

      if (!outcome.IsSuccess()) {
        const auto& err = outcome.GetError();
        std::string name(err.GetExceptionName().c_str(),
                         err.GetExceptionName().size());
        std::string message(err.GetMessage().c_str(), err.GetMessage().size());

        if (attempt == 0 && !refresh_cmd.empty() && IsCredentialError(name)) {
          tool_call::SubprocessOptions sub_opts{
              .argv = {"/bin/sh", "-c", refresh_cmd.c_str()},
          };
          auto result = tool_call::RunSubprocessCapture(sub_opts, stop_token);
          if (result.exit_code == 0 && !stop_token.stop_requested()) {
            continue;
          }
        }

        auto error_event = MapBedrockSyncError(name, message);
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
      return;
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
  if (model_id.empty()) {
    return 0;
  }
  if (impl_->config.context_window > 0) {
    return impl_->config.context_window;
  }
  // Defer to provider::LookupContextWindow at the call site (see
  // provider/model_context_windows.cpp::ResolveContextWindow), which knows the
  // Bedrock model table and inference-profile prefix stripping.
  return 0;
}

}  // namespace yac::provider
