#include "chat/chat_service.hpp"

#include "chat/agent_mode.hpp"
#include "chat/chat_history_store.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_request_builder.hpp"
#include "chat/reasoning_effort.hpp"
#include "chat/state_store.hpp"
#include "tool_call/executor_arguments.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string_view>
#include <utility>

namespace yac::chat {

namespace {

std::string FormatRuntimeStateTimestamp(
    std::chrono::system_clock::time_point time) {
  const auto raw_time = std::chrono::system_clock::to_time_t(time);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &raw_time);
#else
  gmtime_r(&raw_time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string RuntimeStateTimestamp() {
  return FormatRuntimeStateTimestamp(std::chrono::system_clock::now());
}

void SaveRuntimeAppState(const std::shared_ptr<StateStore>& state_store,
                         std::string key, std::string value) {
  if (state_store == nullptr) {
    return;
  }
  try {
    state_store->SaveAppState(AppStateEntry{.key = std::move(key),
                                            .value = std::move(value),
                                            .updated_at =
                                                RuntimeStateTimestamp()});
  } catch (const std::exception& error) {
    yac::log::Warn("chat.runtime_state", "Could not save SQLite app state: {}",
                   error.what());
  }
}

void DeleteRuntimeAppState(const std::shared_ptr<StateStore>& state_store,
                           std::string_view key) {
  if (state_store == nullptr) {
    return;
  }
  try {
    state_store->DeleteAppState(key);
  } catch (const std::exception& error) {
    yac::log::Warn("chat.runtime_state",
                   "Could not delete SQLite app state: {}", error.what());
  }
}

void SaveRuntimeSelection(const std::shared_ptr<StateStore>& state_store,
                          const ProviderId& provider_id,
                          const std::optional<std::string>& profile_id,
                          const ModelId& model) {
  SaveRuntimeAppState(state_store, std::string(kAppStateLastProviderId),
                      provider_id.value);
  if (profile_id.has_value()) {
    SaveRuntimeAppState(state_store, std::string(kAppStateLastProfileId),
                        *profile_id);
  } else {
    DeleteRuntimeAppState(state_store, kAppStateLastProfileId);
  }
  SaveRuntimeAppState(state_store, std::string(kAppStateLastModel), model.value);
}

std::string GenerateUuidV4() {
  std::array<unsigned char, 16> bytes{};
  std::random_device random;
  for (auto& byte : bytes) {
    byte = static_cast<unsigned char>(random());
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  std::size_t index = 0;
  for (const unsigned char byte : bytes) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      out << '-';
    }
    out << std::setw(2) << static_cast<int>(byte);
    ++index;
  }
  return out.str();
}

}  // namespace

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config,
                         std::unique_ptr<core_types::IMcpManager> mcp_manager)
    : registry_(std::move(registry)),
      config_(std::move(config)),
      mcp_manager_(std::move(mcp_manager)),
      mcp_helper_(mcp_manager_ ? std::make_unique<internal::ChatServiceMcp>(
                                     mcp_manager_.get())
                               : nullptr),
      tool_executor_(internal::MakeChatToolExecutor(config_, todo_state_)),
      tool_approval_(std::make_unique<ToolApprovalManager>()),
      sub_agent_manager_(std::make_unique<SubAgentManager>(
          registry_, tool_executor_, *tool_approval_,
          [this](ChatEvent event) { EmitEvent(std::move(event)); },
          [this] { return ConfigSnapshot(); })),
      history_store_(std::make_unique<ChatHistoryStore>(mutex_)),
      prompt_processor_(std::make_unique<internal::ChatServicePromptProcessor>(
          internal::ChatServicePromptProcessor::PromptRunContext{
              .registry = &registry_,
              .tool_executor = tool_executor_.get(),
              .tool_approval = tool_approval_.get(),
              .chat_service_mcp = mcp_helper_.get(),
              .history_mutex = &mutex_,
              .history = &history_store_->MutableView(),
              .emit_event =
                  [this](ChatEvent event) { EmitEvent(std::move(event)); },
              .next_message_id = [this] { return NextMessageId(); },
              .config_snapshot = [this] { return ConfigSnapshot(); },
              .generation_value = [this] { return generation_.load(); },
              .excluded_tools = {},
              .approval_gate = sub_agent_manager_->GetApprovalGate(),
              .mode_excluded_tools =
                  [this] { return ExcludedToolsForMode(config_.agent_mode); },
              .on_usage_reported =
                  [this](const TokenUsage& usage) {
                    std::scoped_lock lock(mutex_);
                    last_usage_ = usage;
                  },
              .last_usage = [this] { return LastUsage(); },
              .on_build_switch_reminder_used =
                  [this](std::string plan_path) {
                    std::scoped_lock lock(mutex_);
                    if (config_.build_switch_plan_path == plan_path) {
                      config_.build_switch_plan_path.reset();
                    }
                  },
              .on_plan_exit_approved =
                  [this](const ::yac::tool_call::PreparedToolCall& prepared) {
                    return ApplyApprovedPlanExit(prepared);
                  }})) {
  session_id_generator_ = [] { return GenerateUuidV4(); };
  tool_executor_->SetSubAgentManager(sub_agent_manager_.get());
  tool_executor_->SetToolApproval(tool_approval_.get());
  sub_agent_manager_->SetMcpManager(mcp_manager_.get());
  sub_agent_manager_->SetNextMessageIdFn([this] { return NextMessageId(); });
  sub_agent_manager_->SetContinuationInjector([this](std::string body) {
    InjectSubAgentContinuation(std::move(body));
  });
  worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

ChatService::~ChatService() {
  {
    std::scoped_lock lock(mutex_);
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }
  tool_approval_->CancelPending();
  worker_.request_stop();
  wake_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  sub_agent_manager_.reset();
}

void ChatService::SetEventCallback(ChatEventCallback callback) {
  std::scoped_lock lock(mutex_);
  callback_ = std::move(callback);
}

ChatMessageId ChatService::SubmitUserMessage(std::string content) {
  auto id = NextMessageId();
  auto queued_content = content;
  {
    std::scoped_lock lock(mutex_);
    EnsureChatSessionIdLocked();
    if (config_.agent_mode == AgentMode::Plan) {
      EnsurePlanSessionLocked(queued_content);
    }
    pending_.push_back({id, std::move(content)});
  }
  wake_.notify_one();

  EmitEvent(
      ChatEvent{UserMessageQueuedEvent{.message_id = id,
                                       .role = ChatRole::User,
                                       .text = std::move(queued_content),
                                       .status = ChatMessageStatus::Queued}});
  EmitQueueDepth();
  return id;
}

void ChatService::SetModel(ModelId model) {
  ProviderId provider_id;
  std::optional<std::string> profile_id;
  ModelId new_model;
  std::shared_ptr<StateStore> state_store;
  {
    std::scoped_lock lock(mutex_);
    if (config_.model == model) {
      return;
    }
    config_.model = model;
    provider_id = config_.provider_id;
    profile_id = config_.profile_id;
    new_model = config_.model;
    state_store = config_.state_store;
  }

  SaveRuntimeSelection(state_store, provider_id, profile_id, new_model);
  EmitEvent(ChatEvent{ModelChangedEvent{.provider_id = std::move(provider_id),
                                        .model = std::move(new_model)}});
}

void ChatService::SetProvider(ProviderId provider_id) {
  ProviderId active_provider;
  std::optional<std::string> active_profile;
  ModelId active_model;
  std::shared_ptr<StateStore> state_store;
  {
    std::scoped_lock lock(mutex_);
    if (config_.provider_id == provider_id) {
      return;
    }

    if (history_store_->HasNonSystemMessages()) {
      EmitEvent(ChatEvent{ErrorEvent{
          .text = "Cannot change provider mid-session. Run /clear to start a "
                  "new conversation, then change provider.",
          .provider_id = config_.provider_id,
          .model = config_.model}});
      return;
    }

    config_.provider_id = std::move(provider_id);
    active_provider = config_.provider_id;
    active_profile = config_.profile_id;
    active_model = config_.model;
    state_store = config_.state_store;
  }

  SaveRuntimeSelection(state_store, active_provider, active_profile,
                       active_model);
  EmitEvent(ChatEvent{ModelChangedEvent{.provider_id =
                                            std::move(active_provider),
                                        .model = std::move(active_model)}});
}

void ChatService::SetReasoningEffort(std::optional<ReasoningEffort> effort) {
  ProviderId active_provider;
  ModelId active_model;
  std::shared_ptr<StateStore> state_store;
  {
    std::scoped_lock lock(mutex_);
    active_provider = config_.provider_id;
    active_model = config_.model;
    state_store = config_.state_store;
    const auto matches_active = [&](const ProviderModelSettings& settings) {
      return settings.provider_id == active_provider &&
             settings.model == active_model;
    };
    const auto it =
        std::ranges::find_if(config_.model_settings, matches_active);
    if (it != config_.model_settings.end()) {
      it->effort = effort;
    } else if (effort.has_value()) {
      config_.model_settings.push_back(
          ProviderModelSettings{.provider_id = active_provider,
                                .model = active_model,
                                .effort = effort});
    }
  }

  const auto key = LastModelEffortAppStateKey(active_provider, active_model);
  if (effort.has_value()) {
    SaveRuntimeAppState(state_store, key,
                        std::string(ReasoningEffortToString(*effort)));
  } else {
    DeleteRuntimeAppState(state_store, key);
  }
}

std::optional<ReasoningEffort> ChatService::ConfiguredReasoningEffort() const {
  std::scoped_lock lock(mutex_);
  const auto active_provider = config_.provider_id;
  const auto active_model = config_.model;
  const auto matches_active = [&](const ProviderModelSettings& settings) {
    return settings.provider_id == active_provider &&
           settings.model == active_model;
  };
  const auto it = std::ranges::find_if(config_.model_settings, matches_active);
  if (it == config_.model_settings.end()) {
    return std::nullopt;
  }
  return it->effort;
}

void ChatService::CancelActiveResponse() {
  std::scoped_lock lock(mutex_);
  if (!active_) {
    return;
  }
  generation_.fetch_add(1);
  if (active_stop_source_.has_value()) {
    active_stop_source_->request_stop();
  }
  tool_approval_->CancelPending();
}

void ChatService::ResolveToolApproval(ApprovalId approval_id, bool approved) {
  tool_approval_->ResolveToolApproval(approval_id, approved);
}

void ChatService::ResolveAskUser(const ApprovalId& approval_id,
                                 std::string response) {
  tool_approval_->ResolveAskUser(approval_id, std::move(response));
}

AgentMode ChatService::GetAgentMode() const {
  std::scoped_lock lock(mutex_);
  return config_.agent_mode;
}

bool ChatService::HasEnteredPlanMode() const {
  std::scoped_lock lock(mutex_);
  return config_.has_entered_plan_mode;
}

std::optional<std::filesystem::path> ChatService::ActivePlanPath() const {
  std::scoped_lock lock(mutex_);
  if (!config_.active_plan_path.has_value()) {
    return std::nullopt;
  }
  return std::filesystem::path(*config_.active_plan_path);
}

void ChatService::QueueBuildSwitchReminderForTest(
    std::filesystem::path plan_path) {
  std::scoped_lock lock(mutex_);
  config_.build_switch_plan_path = std::move(plan_path).string();
}

bool ChatService::HasQueuedBuildSwitchReminderForTest() const {
  std::scoped_lock lock(mutex_);
  return config_.build_switch_plan_path.has_value();
}

void ChatService::SetAgentMode(AgentMode mode) {
  {
    std::scoped_lock lock(mutex_);
    if (config_.agent_mode == mode) {
      return;
    }
    config_.agent_mode = mode;
    if (mode == AgentMode::Plan) {
      config_.has_entered_plan_mode = true;
    }
  }
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = mode}});
}

void ChatService::ResetConversation() {
  // Bump generation up front so the worker's ProcessPrompt observes
  // cancellation at every guarded site. Then signal the active prompt
  // to stop and drop any queued prompts so they don't get processed
  // under the new generation.
  generation_.fetch_add(1);
  {
    std::scoped_lock lock(mutex_);
    pending_.clear();
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }

  // Wait for the worker to finish whatever ProcessPrompt is in flight.
  // The worker notifies wake_ after setting active_=false. Bounded so a
  // misbehaving provider that ignores stop_token can't pin the UI thread;
  // if the budget expires, the per-append generation guards in
  // ProcessPrompt prevent the worker from corrupting the cleared
  // history that follows.
  {
    std::unique_lock lock(mutex_);
    const auto budget = std::chrono::milliseconds(
        reset_drain_budget_ms_.load(std::memory_order_relaxed));
    wake_.wait_for(lock, budget, [this] { return !active_; });
  }

  // Now safe to mutate history. We do not touch active_ or
  // active_stop_source_ — those are owned by the worker thread.
  //
  // Bump generation a second time, under the lock, before clearing
  // history. This closes a window where a worker that captured the
  // first post-bump generation has already passed its append guard
  // (so history_ has its user_msg) but hasn't yet entered
  // BuildRoundRequest. Without the second bump, a budget-expired
  // wait_for above would let us clear history while the worker still
  // sees a matching generation — the next BuildRoundRequest would
  // then issue an empty messages vector to the provider.
  {
    std::scoped_lock lock(mutex_);
    generation_.fetch_add(1);
    history_store_->Clear();
    last_usage_.reset();
    config_.agent_mode = AgentMode::Build;
    config_.has_entered_plan_mode = false;
    config_.active_plan_path.reset();
    config_.build_switch_plan_path.reset();
    config_.chat_session_id = GenerateChatSessionId();
  }

  sub_agent_manager_->CancelAll();
  tool_approval_->CancelPending();
  todo_state_.Clear();

  EmitEvent(ChatEvent{ConversationClearedEvent{}});
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = AgentMode::Build}});
}

void ChatService::SetResetDrainBudgetForTest(std::chrono::milliseconds budget) {
  reset_drain_budget_ms_.store(std::max<int64_t>(1, budget.count()),
                               std::memory_order_relaxed);
}

void ChatService::SetPlanClockForTest(
    std::function<std::chrono::system_clock::time_point()> clock) {
  std::scoped_lock lock(mutex_);
  plan_session_.SetClock(std::move(clock));
}

void ChatService::SetSessionIdGeneratorForTest(
    std::function<std::string()> generator) {
  std::scoped_lock lock(mutex_);
  session_id_generator_ = std::move(generator);
  if (!active_ && pending_.empty() && history_store_->View().empty()) {
    config_.chat_session_id = GenerateChatSessionId();
  }
}

void ChatService::CompactConversation(decltype(sizeof(0)) keep_last) {
  std::size_t removed = 0;
  {
    std::scoped_lock lock(mutex_);
    if (active_ || !pending_.empty()) {
      return;
    }
    removed = history_store_->Compact(keep_last);
  }

  EmitEvent(ChatEvent{ConversationCompactedEvent{
      .reason = CompactReason::Manual,
      .messages_removed = static_cast<int>(removed),
  }});
}

std::vector<ChatMessage> ChatService::History() const {
  return history_store_->Snapshot();
}

bool ChatService::IsBusy() const {
  std::scoped_lock lock(mutex_);
  return active_ || !pending_.empty();
}

int ChatService::QueueDepth() const {
  std::scoped_lock lock(mutex_);
  return static_cast<int>(pending_.size());
}

std::optional<TokenUsage> ChatService::LastUsage() const {
  std::scoped_lock lock(mutex_);
  return last_usage_;
}

void ChatService::WorkerLoop(std::stop_token stop_token) {
  // libc++ < 18.1 (llvm/llvm-project#76807) does not wake the stop_token-aware
  // wait overload on request_stop; route around it with an explicit
  // stop_callback that triggers notify_all and a predicate-form wait that
  // folds stop_requested() into the predicate.
  std::stop_callback wake_on_stop(stop_token, [this] { wake_.notify_all(); });
  while (!stop_token.stop_requested()) {
    PendingPrompt prompt;
    std::stop_source request_stop_source;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] {
        return !pending_.empty() || stop_token.stop_requested();
      });
      if (stop_token.stop_requested()) {
        return;
      }
      if (pending_.empty()) {
        continue;
      }
      prompt = std::move(pending_.front());
      pending_.pop_front();
      active_ = true;
      request_stop_source = std::stop_source{};
      active_stop_source_ = request_stop_source;
    }

    EmitEvent(
        ChatEvent{UserMessageActiveEvent{.message_id = prompt.id,
                                         .role = ChatRole::User,
                                         .status = ChatMessageStatus::Active}});
    EmitQueueDepth();

    prompt_processor_->ProcessPrompt(prompt.id, prompt.content,
                                     generation_.load(),
                                     request_stop_source.get_token());

    {
      std::scoped_lock lock(mutex_);
      active_ = false;
      active_stop_source_.reset();
    }
    // Wake any thread (notably ResetConversation) waiting for this prompt
    // to drain. Cheap; the wait predicate filters spurious wakes.
    wake_.notify_all();
  }
}

void ChatService::EmitEvent(ChatEvent event) const {
  ChatEventCallback cb;
  {
    std::scoped_lock lock(mutex_);
    cb = callback_;
  }
  if (cb) {
    cb(std::move(event));
  }
}

void ChatService::EmitQueueDepth() {
  int depth = 0;
  {
    std::scoped_lock lock(mutex_);
    depth = static_cast<int>(pending_.size());
  }
  EmitEvent(ChatEvent{QueueDepthChangedEvent{.queue_depth = depth}});
}

ChatMessageId ChatService::NextMessageId() {
  return next_id_.fetch_add(1);
}

ChatConfig ChatService::ConfigSnapshot() const {
  std::scoped_lock lock(mutex_);
  return config_;
}

void ChatService::InjectSubAgentContinuation(std::string body) {
  auto id = NextMessageId();
  auto queued_text = body;

  bool was_idle = false;
  {
    std::scoped_lock lock(mutex_);
    was_idle = !active_ && pending_.empty();
    if (was_idle) {
      // Worker will append to history via AppendActiveUserMessage when it
      // picks the prompt up; don't push to history here to avoid duplicating.
      pending_.push_back({id, std::move(body)});
    } else {
      // Worker is busy on another turn; append directly to history so the
      // next request includes this continuation in context.
      history_store_->Append(ChatMessage{.id = id,
                                         .role = ChatRole::User,
                                         .status = ChatMessageStatus::Complete,
                                         .content = std::move(body)});
    }
  }

  EmitEvent(
      ChatEvent{UserMessageQueuedEvent{.message_id = id,
                                       .role = ChatRole::User,
                                       .text = std::move(queued_text),
                                       .status = ChatMessageStatus::Complete,
                                       .role_label = kSubAgentRoleLabel}});

  if (was_idle) {
    wake_.notify_one();
    EmitQueueDepth();
  }
}

::yac::tool_call::ToolExecutionResult ChatService::ApplyApprovedPlanExit(
    const ::yac::tool_call::PreparedToolCall& prepared) {
  auto call = std::get<::yac::tool_call::PlanExitCall>(prepared.preview);
  std::filesystem::path plan_path;
  {
    std::scoped_lock lock(mutex_);
    if (config_.agent_mode != AgentMode::Plan ||
        !config_.active_plan_path.has_value()) {
      return ::yac::tool_call::ToolExecutionResult{
          .block =
              ::yac::tool_call::PlanExitCall{
                  .plan = std::move(call.plan),
                  .is_error = true,
                  .error = "plan_exit requires an active Plan session."},
          .result_json =
              ::yac::tool_call::Json{
                  {"error", "plan_exit requires an active Plan session."}}
                  .dump(),
          .is_error = true,
      };
    }
    plan_path = std::filesystem::path(*config_.active_plan_path);
  }

  if (!PlanSession::WriteApprovedPlan(plan_path, call.plan)) {
    return ::yac::tool_call::ToolExecutionResult{
        .block =
            ::yac::tool_call::PlanExitCall{
                .plan = std::move(call.plan),
                .plan_path = plan_path.string(),
                .is_error = true,
                .error = "Failed to write active plan file."},
        .result_json =
            ::yac::tool_call::Json{
                {"error", "Failed to write active plan file."}}
                .dump(),
        .is_error = true,
    };
  }

  {
    std::scoped_lock lock(mutex_);
    config_.agent_mode = AgentMode::Build;
    config_.build_switch_plan_path = plan_path.string();
  }
  call.plan_path = plan_path.string();
  call.approved = true;
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = AgentMode::Build}});
  return ::yac::tool_call::ToolExecutionResult{
      .block = call,
      .result_json = ::yac::tool_call::Json{{"approved", true},
                                            {"plan_path", plan_path.string()}}
                         .dump(),
  };
}

void ChatService::EnsurePlanSessionLocked(
    const std::string& first_user_prompt) {
  config_.has_entered_plan_mode = true;
  if (config_.active_plan_path.has_value()) {
    return;
  }

  const auto plan_path =
      plan_session_.EnsurePlanPath(config_.workspace_root, first_user_prompt);
  if (plan_path.has_value()) {
    config_.active_plan_path = plan_path->string();
  }
}

void ChatService::EnsureChatSessionIdLocked() {
  if (!config_.chat_session_id.has_value() ||
      config_.chat_session_id->empty()) {
    config_.chat_session_id = GenerateChatSessionId();
  }
}

std::string ChatService::GenerateChatSessionId() {
  if (!session_id_generator_) {
    return GenerateUuidV4();
  }
  auto id = session_id_generator_();
  if (id.empty()) {
    return GenerateUuidV4();
  }
  return id;
}

}  // namespace yac::chat
