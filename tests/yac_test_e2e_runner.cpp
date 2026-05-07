#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "core_types/mcp_manager_interface.hpp"
#include "mcp/mcp_manager.hpp"
#include "mock_response_provider.hpp"
#include "provider/provider_registry.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

constexpr std::string_view kMockScriptFlag = "--mock-llm-script=";
constexpr std::string_view kMockRequestLogFlag = "--mock-request-log=";
constexpr auto kMcpReadyTimeout = std::chrono::seconds(30);

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " run <prompt> --auto-approve"
               " --mock-llm-script=<PATH>"
               " [--mock-request-log=<PATH>]"
               " [--cancel-after-ms=<N>]\n";
}

bool WaitForMcpReady(yac::mcp::McpManager& manager) {
  const auto deadline = std::chrono::steady_clock::now() + kMcpReadyTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto status = manager.GetServerStatusSnapshot();
    if (!status.empty()) {
      for (const auto& s : status) {
        std::cerr << "[mcp] " << s.id << " state=" << s.state
                  << " tool_count=" << s.tool_count << " error=" << s.error
                  << '\n';
      }
      const bool all_ready = std::ranges::all_of(status, [](const auto& s) {
        return s.state == "Ready" || s.state == "Failed";
      });
      if (all_ready) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  return false;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    signal(SIGPIPE, SIG_IGN);

    if (argc < 3 || std::string_view(argv[1]) != "run") {
      PrintUsage(argv[0]);
      return 1;
    }

    std::string prompt;
    bool auto_approve = false;
    int cancel_after_ms = 0;
    std::string mock_script_path;
    std::string mock_request_log;

    for (int i = 2; i < argc; ++i) {
      std::string_view arg(argv[i]);
      if (arg == "--auto-approve") {
        auto_approve = true;
      } else if (arg.starts_with(kMockScriptFlag)) {
        mock_script_path = std::string(arg.substr(kMockScriptFlag.size()));
      } else if (arg.starts_with(kMockRequestLogFlag)) {
        mock_request_log = std::string(arg.substr(kMockRequestLogFlag.size()));
      } else if (arg.starts_with("--cancel-after-ms=")) {
        try {
          cancel_after_ms = std::stoi(std::string(
              arg.substr(std::string_view("--cancel-after-ms=").size())));
        } catch (...) {
          std::cerr << "Error: --cancel-after-ms requires a valid integer\n";
          return 1;
        }
      } else {
        if (!prompt.empty()) {
          prompt += ' ';
        }
        prompt += argv[i];
      }
    }

    if (mock_script_path.empty()) {
      std::cerr << "Error: --mock-llm-script=<PATH> is required for "
                   "yac_test_e2e_runner\n";
      PrintUsage(argv[0]);
      return 1;
    }

    auto config_result = yac::chat::LoadChatConfigResult();
    yac::chat::ChatConfig config = config_result.config;
    config.provider_id = ::yac::ProviderId{"mock"};

    auto provider = std::make_shared<yac::provider::MockResponseProvider>(
        mock_script_path, mock_request_log);

    yac::provider::ProviderRegistry registry;
    registry.Register(provider);

    std::unique_ptr<yac::mcp::McpManager> mcp_manager;
    if (!config.mcp.servers.empty()) {
      mcp_manager = std::make_unique<yac::mcp::McpManager>(
          config.mcp, [](yac::chat::ChatEvent event) { (void)event; });
      mcp_manager->Start();
      const bool mcp_ok = WaitForMcpReady(*mcp_manager);
      if (!mcp_ok) {
        std::cerr << "Warning: MCP servers did not reach Ready state within "
                     "timeout\n";
      }
      const auto final_status = mcp_manager->GetServerStatusSnapshot();
      for (const auto& s : final_status) {
        std::cerr << "[mcp-final] id=" << s.id << " state=" << s.state
                  << " tools=" << s.tool_count << " error=" << s.error << '\n';
      }
    }

    yac::chat::ChatService service(std::move(registry), config,
                                   std::move(mcp_manager));

    std::atomic<int> exit_code{0};
    std::mutex done_mutex;
    std::condition_variable done_cv;
    bool done = false;

    service.SetEventCallback([&](yac::chat::ChatEvent event) {
      std::visit(
          [&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, yac::chat::TextDeltaEvent>) {
              std::cout << e.text << std::flush;
            } else if constexpr (std::is_same_v<T, yac::chat::FinishedEvent>) {
              std::cout << '\n' << std::flush;
              std::unique_lock<std::mutex> lock(done_mutex);
              done = true;
              done_cv.notify_one();
            } else if constexpr (std::is_same_v<T, yac::chat::ErrorEvent>) {
              std::cerr << "Error: " << e.text << '\n';
              exit_code = 1;
              std::unique_lock<std::mutex> lock(done_mutex);
              done = true;
              done_cv.notify_one();
            } else if constexpr (std::is_same_v<
                                     T,
                                     yac::chat::ToolApprovalRequestedEvent>) {
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

    std::thread cancel_timer;
    if (cancel_after_ms > 0) {
      cancel_timer = std::thread([&service, cancel_after_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(cancel_after_ms));
        service.CancelActiveResponse();
      });
    }

    {
      std::unique_lock<std::mutex> lock(done_mutex);
      done_cv.wait(lock, [&] { return done; });
    }

    if (cancel_timer.joinable()) {
      cancel_timer.join();
    }

    return exit_code.load();
  } catch (const std::exception& ex) {
    std::cerr << "Fatal: " << ex.what() << '\n';
    return 1;
  }
}
