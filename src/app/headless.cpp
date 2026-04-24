#include "app/headless.hpp"

#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace yac::app {

int RunHeadless(const std::string& prompt, bool auto_approve) {
  auto config_result = chat::LoadChatConfigResult();
  const auto& config = config_result.config;

  auto provider = std::make_shared<provider::OpenAiChatProvider>(
      chat::ProviderConfig{.id = config.provider_id,
                           .model = config.model,
                           .api_key = config.api_key,
                           .api_key_env = config.api_key_env,
                           .base_url = config.base_url});

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
            std::cerr << "Error: " << e.text << '\n';
            exit_code = 1;
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          } else if constexpr (std::is_same_v<
                                   T, chat::ToolApprovalRequestedEvent>) {
            if (auto_approve) {
              service.ResolveToolApproval(e.approval_id, true);
            } else {
              std::cerr << "Tool approval required (use --auto-approve): "
                        << e.tool_name << '\n';
              service.ResolveToolApproval(e.approval_id, false);
            }
          }
        },
        event.payload);
  });

  service.SubmitUserMessage(prompt);

  std::unique_lock<std::mutex> lock(done_mutex);
  done_cv.wait(lock, [&] { return done; });

  return exit_code.load();
}

}  // namespace yac::app
