#include "app/headless.hpp"

#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "presentation/file_mention_inliner.hpp"
#include "provider/bedrock_aws_api_guard.hpp"
#include "provider/bedrock_chat_provider.hpp"
#include "provider/openai_compatible_chat_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/workspace_filesystem.hpp"
#include "util/log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace yac::app {

int RunHeadless(const std::string& prompt, bool auto_approve,
                int cancel_after_ms) {
  auto config_result = chat::LoadChatConfigResult();
  const auto& config = config_result.config;

  std::shared_ptr<provider::LanguageModelProvider> provider;
  if (config.provider_id.value == "bedrock") {
    provider::EnsureAwsApiGuardInstalled();
    provider =
        std::make_shared<provider::BedrockChatProvider>(chat::ProviderConfig{
            .id = config.provider_id,
            .model = config.model,
            .api_key = config.api_key,
            .api_key_env = config.api_key_env,
            .base_url = config.base_url,
            .options = config.options,
            .context_window = config.context_window,
        });
  } else {
    provider = std::make_shared<provider::OpenAiCompatibleChatProvider>(
        chat::ProviderConfig{.id = config.provider_id,
                             .model = config.model,
                             .api_key = config.api_key,
                             .api_key_env = config.api_key_env,
                             .base_url = config.base_url});
  }

  provider::ProviderRegistry registry;
  registry.Register(provider);

  chat::ChatService service(std::move(registry), config);

  std::atomic<int> exit_code{0};
  std::mutex done_mutex;
  std::condition_variable done_cv;
  bool done = false;

  service.SetEventCallback([&](chat::ChatEvent event) {
    std::visit(
        [&](auto&& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, chat::TextDeltaEvent>) {
            std::cout << e.text << std::flush;
          } else if constexpr (std::is_same_v<T, chat::FinishedEvent>) {
            std::cout << '\n' << std::flush;
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          } else if constexpr (std::is_same_v<T, chat::ErrorEvent>) {
            yac::log::Error("headless", "{}", e.text);
            exit_code = 1;
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          } else if constexpr (std::is_same_v<
                                   T, chat::ToolApprovalRequestedEvent>) {
            if (auto_approve) {
              service.ResolveToolApproval(e.approval_id, true);
            } else {
              yac::log::Error("headless",
                              "tool approval required (use --auto-approve): {}",
                              e.tool_name);
              service.ResolveToolApproval(e.approval_id, false);
            }
          }
        },
        event.payload);
  });

  tool_call::WorkspaceFilesystem workspace_fs(config.workspace_root);
  auto inlined = presentation::InlineFileMentions(prompt, workspace_fs);
  service.SubmitUserMessage(std::move(inlined.text));

  std::jthread cancel_timer;
  if (cancel_after_ms > 0) {
    cancel_timer = std::jthread([&service,
                                 cancel_after_ms](std::stop_token st) {
      std::this_thread::sleep_for(std::chrono::milliseconds(cancel_after_ms));
      if (!st.stop_requested()) {
        service.CancelActiveResponse();
      }
    });
  }

  std::unique_lock<std::mutex> lock(done_mutex);
  done_cv.wait(lock, [&] { return done; });

  return exit_code.load();
}

}  // namespace yac::app
