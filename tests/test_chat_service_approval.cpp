#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "core_types/typed_ids.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;

namespace {

std::shared_ptr<LambdaMockProvider> MakeFakeProvider(
    ::yac::ModelId expected_model = ::yac::ModelId{"fake-model"}) {
  return std::make_shared<LambdaMockProvider>(
      "fake", [expected_model = std::move(expected_model)](
                  const ChatRequest& request, ChatEventSink sink,
                  std::stop_token stop_token) {
        REQUIRE(request.model == expected_model);
        REQUIRE(request.stream);
        REQUIRE(!request.messages.empty());
        REQUIRE(request.messages.back().role == ChatRole::User);
        if (stop_token.stop_requested()) {
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "hi"}});
        sink(ChatEvent{TextDeltaEvent{.text = " there"}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeApprovalRejectionProvider() {
  auto request_count = std::make_shared<int>(0);
  return std::make_shared<LambdaMockProvider>(
      "approval-rejection",
      [request_count](const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        ++(*request_count);
        if (*request_count == 1) {
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {ToolCallRequest{
                  .id = "tool_1",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"notes.txt","content":"denied\n"})"}}}});
          return;
        }
        REQUIRE(std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == yac::ToolCallId{"tool_1"} &&
                     message.content ==
                         R"({"error":"User rejected tool execution."})";
            }));
        sink(ChatEvent{TextDeltaEvent{.text = "continued after rejection"}});
      });
}

class SequentialApprovalProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override {
    return "sequential-approval";
  }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    if (stop_token.stop_requested()) {
      return;
    }

    ++request_count_;
    if (request_count_ == 1) {
      sink(ChatEvent{ToolCallRequestedEvent{
          .tool_calls = {
              ToolCallRequest{
                  .id = "tool_1",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"first.txt","content":"one\n"})"},
              ToolCallRequest{
                  .id = "tool_2",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"second.txt","content":"two\n"})"},
          }}});
      return;
    }

    REQUIRE(std::count_if(request.messages.begin(), request.messages.end(),
                          [](const ChatMessage& message) {
                            return message.role == ChatRole::Tool;
                          }) == 2);
    sink(ChatEvent{TextDeltaEvent{.text = "all approvals resolved"}});
  }

 private:
  int request_count_ = 0;
};

ChatService MakeService(
    std::shared_ptr<LanguageModelProvider> provider = nullptr,
    ChatConfig config = {}) {
  ProviderRegistry registry;
  if (provider) {
    if (config.provider_id.value == "openai-compatible") {
      config.provider_id = ::yac::ProviderId{provider->Id()};
    }
    registry.Register(std::move(provider));
  } else {
    registry.Register(MakeFakeProvider());
    config.provider_id = ::yac::ProviderId{"fake"};
    config.model = ::yac::ModelId{"fake-model"};
  }
  return ChatService(std::move(registry), config);
}

bool HasEvent(const std::vector<ChatEvent>& events, ChatEventType type) {
  return std::ranges::any_of(
      events, [type](const auto& e) { return e.Type() == type; });
}

const ChatEvent& FindEvent(const std::vector<ChatEvent>& events,
                           ChatEventType type) {
  auto it = std::ranges::find_if(
      events, [type](const auto& e) { return e.Type() == type; });
  REQUIRE(it != events.end());
  return *it;
}

}  // namespace

TEST_CASE("ChatService records rejected approval as tool error and continues") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_tool_approval_rejection";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = MakeApprovalRejectionProvider();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"approval-rejection"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mutex;
  std::condition_variable cv;
  yac::ApprovalId approval_id;
  bool approval_requested = false;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (const auto* approval = event.As<ToolApprovalRequestedEvent>()) {
      approval_id = approval->approval_id;
      approval_requested = true;
      cv.notify_all();
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("write file");

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_requested; });
  }

  service.ResolveToolApproval(approval_id, false);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return finished; });
  }

  REQUIRE_FALSE(std::filesystem::exists(root / "notes.txt"));
  const auto& tool_done = FindEvent(events, ChatEventType::ToolCallDone);
  REQUIRE(tool_done.Get<ToolCallDoneEvent>().status ==
          ChatMessageStatus::Error);
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  REQUIRE(service.History().back().content == "continued after rejection");
  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService sequences approval requests one tool at a time") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_tool_approval_sequencing";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = std::make_shared<SequentialApprovalProvider>();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"sequential-approval"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<yac::ApprovalId> approval_ids;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (const auto* approval = event.As<ToolApprovalRequestedEvent>()) {
      approval_ids.push_back(approval->approval_id);
      cv.notify_all();
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("write two files");

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_ids.size() == 1; });
    REQUIRE(approval_ids.size() == 1);
  }

  service.ResolveToolApproval(approval_ids[0], true);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_ids.size() == 2; });
    REQUIRE(approval_ids.size() == 2);
  }

  service.ResolveToolApproval(approval_ids[1], true);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return finished; });
  }

  REQUIRE(std::filesystem::exists(root / "first.txt"));
  REQUIRE(std::filesystem::exists(root / "second.txt"));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  std::filesystem::remove_all(root);
}
