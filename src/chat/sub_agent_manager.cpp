#include "chat/sub_agent_manager.hpp"

#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_tool_approval.hpp"

#include <atomic>
#include <chrono>
#include <ranges>
#include <set>
#include <stop_token>
#include <thread>
#include <utility>

namespace yac::chat {

namespace {

constexpr std::string_view kSubAgentSystemPrompt =
    "You are a focused assistant completing a specific task. Execute the task "
    "and report your findings concisely.";

std::string MakeAgentId() {
  static std::atomic<uint64_t> next_agent_id{1};
  return "sub-agent-" + std::to_string(next_agent_id.fetch_add(1));
}

std::string TruncateWithEllipsis(const std::string& text, size_t max_chars) {
  if (text.size() <= max_chars) {
    return text;
  }
  if (max_chars <= 3) {
    return text.substr(0, max_chars);
  }
  return text.substr(0, max_chars - 3) + "...";
}

std::string CapacityError() {
  return "Sub-agent capacity reached (max " +
         std::to_string(kMaxConcurrentSubAgents) + ").";
}

std::string LastAssistantMessage(const std::vector<ChatMessage>& history) {
  for (const auto& message : history | std::views::reverse) {
    if (message.role == ChatRole::Assistant && !message.content.empty()) {
      return message.content;
    }
  }
  return {};
}

ChatConfig MakeSubAgentConfig(const ChatConfig& parent_config) {
  ChatConfig config = parent_config;
  config.system_prompt = std::string(kSubAgentSystemPrompt);
  return config;
}

}  // namespace

struct SubAgentManager::SubAgentSession {
  SubAgentSession() = default;

  ~SubAgentSession() {
    stop_source.request_stop();
    if (worker.joinable()) {
      worker.join();
    }
  }

  SubAgentSession(const SubAgentSession&) = delete;
  SubAgentSession& operator=(const SubAgentSession&) = delete;
  SubAgentSession(SubAgentSession&&) = delete;
  SubAgentSession& operator=(SubAgentSession&&) = delete;

  std::string agent_id;
  std::string task;
  tool_call::SubAgentMode mode = tool_call::SubAgentMode::Foreground;
  ChatMessageId card_message_id = 0;
  std::stop_source stop_source;
  std::mutex history_mutex;
  std::vector<ChatMessage> history;
  std::atomic<ChatMessageId> next_id{1};
  std::atomic<uint64_t> generation{0};
  std::atomic<int> completed_tool_count{0};
  std::unique_ptr<internal::ChatServicePromptProcessor> prompt_processor;
  std::chrono::steady_clock::time_point deadline;
  std::chrono::steady_clock::time_point started_at;
  std::atomic<bool> finished{false};
  std::atomic<bool> timed_out{false};
  std::atomic<bool> cancelled{false};
  std::jthread worker;
};

struct SubAgentManager::SubAgentCompletion {
  ChatEventType type = ChatEventType::SubAgentCompleted;
  ChatMessageStatus status = ChatMessageStatus::Complete;
  std::string result;
  int tool_count = 0;
};

SubAgentManager::SubAgentManager(
    provider::ProviderRegistry& registry,
    std::shared_ptr<tool_call::ToolExecutor> tool_executor,
    internal::ChatServiceToolApproval& tool_approval, EmitEventFn parent_emit,
    ConfigSnapshotFn parent_config_snapshot,
    NextMessageIdFn parent_next_message_id, int timeout_seconds)
    : registry_(&registry),
      tool_executor_(std::move(tool_executor)),
      tool_approval_(&tool_approval),
      parent_emit_(std::move(parent_emit)),
      parent_config_snapshot_(std::move(parent_config_snapshot)),
      parent_next_message_id_(std::move(parent_next_message_id)),
      timeout_seconds_(timeout_seconds) {}

SubAgentManager::~SubAgentManager() {
  CancelAll();
}

std::shared_ptr<SubAgentManager::SubAgentSession>
SubAgentManager::CreateSession(const std::string& task,
                               tool_call::SubAgentMode mode) {
  auto session = std::make_shared<SubAgentSession>();
  session->agent_id = MakeAgentId();
  session->task = task;
  session->mode = mode;
  session->card_message_id = parent_next_message_id_();
  const auto now = std::chrono::steady_clock::now();
  session->deadline = now + std::chrono::seconds(timeout_seconds_);
  session->started_at = now;
  AttachPromptProcessor(*session);
  return session;
}

bool SubAgentManager::TryStoreSession(
    const std::shared_ptr<SubAgentSession>& session) {
  std::vector<std::shared_ptr<SubAgentSession>> finished_sessions;
  {
    std::unique_lock lock(sessions_mutex_);
    MoveFinishedSessionsLocked(finished_sessions);
    if (active_sessions_.size() >= kMaxConcurrentSubAgents) {
      return false;
    }
    return active_sessions_.emplace(session->agent_id, session).second;
  }
}

void SubAgentManager::RemoveSession(const std::string& agent_id) {
  std::shared_ptr<SubAgentSession> removed_session;
  {
    std::unique_lock lock(sessions_mutex_);
    const auto it = active_sessions_.find(agent_id);
    if (it == active_sessions_.end()) {
      return;
    }
    removed_session = std::move(it->second);
    active_sessions_.erase(it);
  }
}

void SubAgentManager::AttachPromptProcessor(SubAgentSession& session) {
  const auto filtered_emit = MakeFilteredEmit(session);

  const auto next_message_id = [&session] {
    return session.next_id.fetch_add(1);
  };
  const auto config_snapshot = [this] {
    return MakeSubAgentConfig(parent_config_snapshot_());
  };
  const auto generation_value = [&session] {
    return session.generation.load();
  };
  session.prompt_processor =
      std::make_unique<internal::ChatServicePromptProcessor>(
          *registry_, *tool_executor_, *tool_approval_, session.history_mutex,
          session.history, filtered_emit, next_message_id, config_snapshot,
          generation_value, std::set<std::string>{"sub_agent"},
          &approval_gate_);
}

SubAgentManager::EmitEventFn SubAgentManager::MakeFilteredEmit(
    SubAgentSession& session) {
  return [this, &session](ChatEvent event) {
    if (std::chrono::steady_clock::now() > session.deadline &&
        !session.stop_source.stop_requested()) {
      session.timed_out = true;
      RequestSessionStop(session, false);
    }

    event.message_id = session.card_message_id;
    event.sub_agent_id = session.agent_id;
    event.sub_agent_task = session.task;

    if (event.type == ChatEventType::ToolApprovalRequested) {
      event.text = "[Sub-agent: " + TruncateWithEllipsis(session.task, 48) +
                   "] " + event.text;
      parent_emit_(std::move(event));
      return;
    }

    if (event.type == ChatEventType::ToolCallStarted ||
        event.type == ChatEventType::ToolCallDone) {
      if (event.type == ChatEventType::ToolCallDone) {
        event.sub_agent_tool_count =
            session.completed_tool_count.fetch_add(1) + 1;
      } else {
        event.sub_agent_tool_count = session.completed_tool_count.load();
      }
      event.type = ChatEventType::SubAgentProgress;
      parent_emit_(std::move(event));
    }
  };
}

void SubAgentManager::EmitSessionStarted(const SubAgentSession& session) {
  parent_emit_(ChatEvent{.type = ChatEventType::SubAgentStarted,
                         .message_id = session.card_message_id,
                         .role = ChatRole::Assistant,
                         .status = ChatMessageStatus::Active,
                         .sub_agent_id = session.agent_id,
                         .sub_agent_task = session.task});
}

SubAgentManager::SubAgentCompletion SubAgentManager::RunSession(
    SubAgentSession& session, std::stop_token parent_stop_token) {
  auto request_parent_stop = [this, &session] {
    RequestSessionStop(session, true);
  };
  std::stop_callback<decltype(request_parent_stop)> parent_stop_callback(
      parent_stop_token, request_parent_stop);

  SubAgentCompletion completion;
  try {
    session.prompt_processor->ProcessPrompt(
        session.next_id.fetch_add(1), session.task, session.generation.load(),
        session.stop_source.get_token());
    {
      std::lock_guard lock(session.history_mutex);
      completion.result = LastAssistantMessage(session.history);
    }
    if (session.timed_out.load()) {
      completion.type = ChatEventType::SubAgentError;
      completion.status = ChatMessageStatus::Error;
      completion.result = "Sub-agent timed out after " +
                          std::to_string(timeout_seconds_) + " seconds.";
    } else if (session.cancelled.load() ||
               (completion.result.empty() &&
                session.stop_source.stop_requested())) {
      completion.type = ChatEventType::SubAgentCancelled;
      completion.status = ChatMessageStatus::Cancelled;
      completion.result = "Sub-agent cancelled.";
    }
  } catch (const std::exception& error) {
    completion.type = ChatEventType::SubAgentError;
    completion.status = ChatMessageStatus::Error;
    completion.result = error.what();
  }

  completion.tool_count = session.completed_tool_count.load();
  completion.result = TruncateWithEllipsis(completion.result, kMaxResultChars);
  return completion;
}

void SubAgentManager::EmitSessionCompleted(
    const SubAgentSession& session, const SubAgentCompletion& completion) {
  const auto elapsed_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - session.started_at)
          .count());
  parent_emit_(ChatEvent{.type = completion.type,
                         .message_id = session.card_message_id,
                         .role = ChatRole::Assistant,
                         .text = completion.result,
                         .status = completion.status,
                         .sub_agent_id = session.agent_id,
                         .sub_agent_task = session.task,
                         .sub_agent_result = completion.result,
                         .sub_agent_tool_count = completion.tool_count,
                         .sub_agent_elapsed_ms = elapsed_ms});
}

void SubAgentManager::RequestSessionStop(SubAgentSession& session,
                                         bool mark_cancelled) {
  if (mark_cancelled) {
    session.cancelled = true;
  }
  if (!session.stop_source.stop_requested()) {
    session.generation.fetch_add(1);
  }
  session.stop_source.request_stop();
}

std::string SubAgentManager::SpawnForeground(
    const std::string& task, std::stop_token parent_stop_token) {
  auto session = CreateSession(task, tool_call::SubAgentMode::Foreground);
  if (!TryStoreSession(session)) {
    return CapacityError();
  }

  EmitSessionStarted(*session);
  const auto completion = RunSession(*session, parent_stop_token);
  EmitSessionCompleted(*session, completion);
  RemoveSession(session->agent_id);
  return completion.result;
}

std::string SubAgentManager::SpawnBackground(const std::string& task) {
  auto session = CreateSession(task, tool_call::SubAgentMode::Background);
  if (!TryStoreSession(session)) {
    return CapacityError();
  }

  EmitSessionStarted(*session);

  SubAgentSession* session_ptr = session.get();
  session_ptr->worker = std::jthread([this, session_ptr](std::stop_token) {
    const auto completion = RunSession(*session_ptr);
    EmitSessionCompleted(*session_ptr, completion);
    session_ptr->finished = true;
  });

  return session->agent_id;
}

void SubAgentManager::Cancel(const std::string& agent_id) {
  CleanupFinishedSessions();

  std::shared_ptr<SubAgentSession> session;
  {
    std::shared_lock lock(sessions_mutex_);
    const auto it = active_sessions_.find(agent_id);
    if (it != active_sessions_.end()) {
      session = it->second;
    }
  }
  if (session != nullptr) {
    RequestSessionStop(*session, true);
  }
}

void SubAgentManager::CancelAll() {
  std::vector<std::shared_ptr<SubAgentSession>> sessions;
  {
    std::unique_lock lock(sessions_mutex_);
    sessions.reserve(active_sessions_.size());
    for (auto& [agent_id, session] : active_sessions_) {
      (void)agent_id;
      RequestSessionStop(*session, true);
      sessions.push_back(std::move(session));
    }
    active_sessions_.clear();
  }
}

bool SubAgentManager::IsAtCapacity() {
  CleanupFinishedSessions();

  std::shared_lock lock(sessions_mutex_);
  return active_sessions_.size() >= kMaxConcurrentSubAgents;
}

std::mutex* SubAgentManager::GetApprovalGate() {
  return &approval_gate_;
}

void SubAgentManager::CleanupFinishedSessions() {
  std::vector<std::shared_ptr<SubAgentSession>> finished_sessions;
  {
    std::unique_lock lock(sessions_mutex_);
    MoveFinishedSessionsLocked(finished_sessions);
  }
}

void SubAgentManager::MoveFinishedSessionsLocked(
    std::vector<std::shared_ptr<SubAgentSession>>& finished_sessions) {
  for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
    if (!it->second->finished.load()) {
      ++it;
      continue;
    }
    finished_sessions.push_back(std::move(it->second));
    it = active_sessions_.erase(it);
  }
}

}  // namespace yac::chat
