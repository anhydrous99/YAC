#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "core_types/typed_ids.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <openai.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;
using Json = openai::_detail::Json;

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

class PlanExitProvider : public LanguageModelProvider {
 public:
  explicit PlanExitProvider(bool expect_approved)
      : expect_approved_(expect_approved) {}

  [[nodiscard]] std::string Id() const override { return "plan-exit"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    if (stop_token.stop_requested()) {
      return;
    }
    ++request_count_;
    if (request_count_ == 1) {
      REQUIRE(std::ranges::any_of(
          request.tools,
          [](const ToolDefinition& tool) { return tool.name == "plan_exit"; }));
      sink(ChatEvent{ToolCallRequestedEvent{
          .tool_calls = {ToolCallRequest{
              .id = "plan-exit-1",
              .name = "plan_exit",
              .arguments_json = Json{{"plan", kFinalPlan}}.dump(),
          }}}});
      return;
    }

    const auto tool_message =
        std::ranges::find_if(request.messages, [](const ChatMessage& message) {
          return message.role == ChatRole::Tool &&
                 message.tool_call_id == ::yac::ToolCallId{"plan-exit-1"};
        });
    REQUIRE(tool_message != request.messages.end());
    const auto result = Json::parse(tool_message->content);
    if (expect_approved_) {
      REQUIRE(bool(result["approved"]));
      REQUIRE(std::string(result["plan_path"]).find(".opencode/plans/") !=
              std::string::npos);
      const auto system_message = std::ranges::find_if(
          request.messages, [](const ChatMessage& message) {
            return message.role == ChatRole::System;
          });
      REQUIRE(system_message != request.messages.end());
      REQUIRE(system_message->content.find(
                  "switched from Plan mode to Build mode") !=
              std::string::npos);
      REQUIRE(std::ranges::none_of(
          request.tools,
          [](const ToolDefinition& tool) { return tool.name == "plan_exit"; }));
      sink(ChatEvent{TextDeltaEvent{.text = "ready to build"}});
    } else {
      REQUIRE(std::string(result["error"]) == "User rejected tool execution.");
      REQUIRE(std::ranges::any_of(
          request.tools,
          [](const ToolDefinition& tool) { return tool.name == "plan_exit"; }));
      sink(ChatEvent{TextDeltaEvent{.text = "plan rejected"}});
    }
  }

  int RequestCount() const { return request_count_; }

  static constexpr const char* kFinalPlan = "# Final plan\n- Build safely\n";

 private:
  bool expect_approved_ = false;
  int request_count_ = 0;
};

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

class PlanPermissionProvider : public LanguageModelProvider {
 public:
  PlanPermissionProvider(std::string tool_name, std::string arguments_json,
                         std::string expected_error = {})
      : tool_name_(std::move(tool_name)),
        arguments_json_(std::move(arguments_json)),
        expected_error_(std::move(expected_error)) {}

  [[nodiscard]] std::string Id() const override { return "plan-permission"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    if (stop_token.stop_requested()) {
      return;
    }
    ++request_count_;
    if (request_count_ == 1) {
      REQUIRE(
          std::ranges::none_of(request.tools, [](const ToolDefinition& tool) {
            return tool.name == "file_write" || tool.name == "file_edit";
          }));
      sink(ChatEvent{
          ToolCallRequestedEvent{.tool_calls = {ToolCallRequest{
                                     .id = "plan-tool-1",
                                     .name = tool_name_,
                                     .arguments_json = arguments_json_,
                                 }}}});
      return;
    }
    const auto tool_message =
        std::ranges::find_if(request.messages, [](const ChatMessage& message) {
          return message.role == ChatRole::Tool &&
                 message.tool_call_id == ::yac::ToolCallId{"plan-tool-1"};
        });
    REQUIRE(tool_message != request.messages.end());
    if (!expected_error_.empty()) {
      const auto result = Json::parse(tool_message->content);
      REQUIRE(std::string(result["error"]).find(expected_error_) !=
              std::string::npos);
    }
    sink(ChatEvent{TextDeltaEvent{.text = "plan permission checked"}});
  }

 private:
  std::string tool_name_;
  std::string arguments_json_;
  std::string expected_error_;
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

std::chrono::system_clock::time_point LocalTime(int year, int month, int day,
                                                int hour, int minute,
                                                int second) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = -1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
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

TEST_CASE(
    "Approved write is denied if mode switches to Plan before execution") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_tool_approval_mode_switch";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto request_count = std::make_shared<int>(0);
  auto provider = std::make_shared<LambdaMockProvider>(
      "stale-approval",
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
                      R"({"filepath":"notes.txt","content":"approved\n"})"}}}});
          return;
        }

        const auto tool_message = std::ranges::find_if(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == yac::ToolCallId{"tool_1"};
            });
        REQUIRE(tool_message != request.messages.end());
        const auto result = Json::parse(tool_message->content);
        REQUIRE(std::string(result["error"]) ==
                "Tool 'file_write' is not allowed in Plan mode.");
        sink(ChatEvent{TextDeltaEvent{.text = "write blocked in plan"}});
      });
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"stale-approval"};
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
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(5),
                        [&] { return approval_requested; }));
  }

  service.SetAgentMode(AgentMode::Plan);
  service.ResolveToolApproval(approval_id, true);
  {
    std::unique_lock lock(mutex);
    REQUIRE(
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
  }

  REQUIRE(service.GetAgentMode() == AgentMode::Plan);
  REQUIRE_FALSE(std::filesystem::exists(root / "notes.txt"));
  const auto& tool_done = FindEvent(events, ChatEventType::ToolCallDone);
  REQUIRE(tool_done.Get<ToolCallDoneEvent>().status ==
          ChatMessageStatus::Error);
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  std::filesystem::remove_all(root);
}

TEST_CASE("Plan mode denies active plan file writes before approval") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_plan_permission_file_write";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const std::string plan_path = ".opencode/plans/20260522-143045-make-plan.md";
  auto provider = std::make_shared<PlanPermissionProvider>(
      "file_write",
      Json{{"filepath", plan_path}, {"content", "# Plan\n"}}.dump(),
      "Tool 'file_write' is not allowed in Plan mode.");
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"plan-permission"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);
  service.SetPlanClockForTest(
      [] { return LocalTime(2026, 5, 22, 14, 30, 45); });
  service.SetAgentMode(AgentMode::Plan);

  std::mutex mutex;
  std::condition_variable cv;
  bool approval_requested = false;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (event.Type() == ChatEventType::ToolApprovalRequested) {
      approval_requested = true;
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
  });

  service.SubmitUserMessage("make plan");
  {
    std::unique_lock lock(mutex);
    REQUIRE(
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
  }

  REQUIRE(std::filesystem::exists(root / plan_path));
  REQUIRE(std::filesystem::file_size(root / plan_path) == 0);
  REQUIRE_FALSE(approval_requested);
  std::filesystem::remove_all(root);
}

TEST_CASE("Plan mode denies source file edits before approval") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_plan_permission_deny";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "src");
  {
    std::ofstream file(root / "src" / "main.cpp");
    file << "int main() { return 0; }\n";
  }

  auto provider = std::make_shared<PlanPermissionProvider>(
      "file_edit",
      Json{{"filepath", "src/main.cpp"},
           {"old_string", "return 0"},
           {"new_string", "return 1"}}
          .dump(),
      "Tool 'file_edit' is not allowed in Plan mode.");
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"plan-permission"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);
  service.SetPlanClockForTest(
      [] { return LocalTime(2026, 5, 22, 14, 30, 45); });
  service.SetAgentMode(AgentMode::Plan);

  std::mutex mutex;
  std::condition_variable cv;
  bool approval_requested = false;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (event.Type() == ChatEventType::ToolApprovalRequested) {
      approval_requested = true;
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
  });

  service.SubmitUserMessage("make plan");
  {
    std::unique_lock lock(mutex);
    REQUIRE(
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
  }

  REQUIRE_FALSE(approval_requested);
  REQUIRE(std::filesystem::exists(root / ".opencode" / "plans" /
                                  "20260522-143045-make-plan.md"));
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "Plan mode plan_exit approval switches to Build and queues reminder") {
  auto root = std::filesystem::temp_directory_path() / "yac_plan_exit_approve";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = std::make_shared<PlanExitProvider>(true);
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"plan-exit"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);
  service.SetPlanClockForTest(
      [] { return LocalTime(2026, 5, 22, 14, 30, 45); });
  service.SetAgentMode(AgentMode::Plan);

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

  service.SubmitUserMessage("make plan");
  {
    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(5),
                        [&] { return approval_requested; }));
    const auto& approval =
        FindEvent(events, ChatEventType::ToolApprovalRequested)
            .Get<ToolApprovalRequestedEvent>();
    REQUIRE(approval.tool_name == "plan_exit");
    REQUIRE(approval.text == "Approve plan and switch to Build mode?");
  }

  service.ResolveToolApproval(approval_id, true);
  {
    std::unique_lock lock(mutex);
    REQUIRE(
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
  }

  const auto active_path = service.ActivePlanPath();
  REQUIRE(active_path.has_value());
  REQUIRE(*active_path ==
          root / ".opencode" / "plans" / "20260522-143045-make-plan.md");
  REQUIRE(service.GetAgentMode() == AgentMode::Build);
  REQUIRE_FALSE(service.HasQueuedBuildSwitchReminderForTest());
  REQUIRE(ReadFile(*active_path) == PlanExitProvider::kFinalPlan);
  REQUIRE(provider->RequestCount() == 2);
  REQUIRE(HasEvent(events, ChatEventType::AgentModeChanged));
  std::filesystem::remove_all(root);
}

TEST_CASE("Plan mode plan_exit rejection stays in Plan with tool error") {
  auto root = std::filesystem::temp_directory_path() / "yac_plan_exit_reject";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = std::make_shared<PlanExitProvider>(false);
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"plan-exit"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);
  service.SetPlanClockForTest(
      [] { return LocalTime(2026, 5, 22, 14, 30, 45); });
  service.SetAgentMode(AgentMode::Plan);

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

  service.SubmitUserMessage("make plan");
  {
    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(5),
                        [&] { return approval_requested; }));
  }

  service.ResolveToolApproval(approval_id, false);
  {
    std::unique_lock lock(mutex);
    REQUIRE(
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return finished; }));
  }

  const auto active_path = service.ActivePlanPath();
  REQUIRE(active_path.has_value());
  REQUIRE(*active_path ==
          root / ".opencode" / "plans" / "20260522-143045-make-plan.md");
  REQUIRE(service.GetAgentMode() == AgentMode::Plan);
  REQUIRE_FALSE(service.HasQueuedBuildSwitchReminderForTest());
  REQUIRE(std::filesystem::exists(*active_path));
  REQUIRE(ReadFile(*active_path).empty());
  const auto& tool_done =
      FindEvent(events, ChatEventType::ToolCallDone).Get<ToolCallDoneEvent>();
  REQUIRE(tool_done.status == ChatMessageStatus::Error);
  REQUIRE_FALSE(HasEvent(events, ChatEventType::AgentModeChanged));
  REQUIRE(provider->RequestCount() == 2);
  std::filesystem::remove_all(root);
}
