#include "chat/sub_agent_manager.hpp"

#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_tool_approval.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
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

int ToolCount(const std::vector<ChatMessage>& history) {
  return static_cast<int>(std::count_if(
      history.begin(), history.end(),
      [](const ChatMessage& message) { return message.role == ChatRole::Tool; }));
}

ChatConfig MakeSubAgentConfig(const ChatConfig& parent_config) {
  ChatConfig config = parent_config;
  config.system_prompt = std::string(kSubAgentSystemPrompt);
  return config;
}

}  // namespace

struct SubAgentManager::SubAgentSession {
  std::string agent_id;
  std::string task;
  tool_call::SubAgentMode mode = tool_call::SubAgentMode::Foreground;
  ChatMessageId card_message_id = 0;
  std::stop_source stop_source;
  std::mutex history_mutex;
  std::vector<ChatMessage> history;
  std::atomic<ChatMessageId> next_id{1};
  std::atomic<uint64_t> generation{0};
  std::unique_ptr<internal::ChatServicePromptProcessor> prompt_processor;
  std::chrono::steady_clock::time_point deadline;
  std::jthread worker;
  std::chrono::steady_clock::time_point started_at;
  std::atomic<bool> finished{false};
  std::atomic<bool> timed_out{false};
  std::atomic<bool> cancelled{false};
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

std::string SubAgentManager::SpawnForeground(const std::string& task) {
  if (IsAtCapacity()) {
    return CapacityError();
  }

  auto session = std::make_unique<SubAgentSession>();
  session->agent_id = MakeAgentId();
  session->task = task;
  session->mode = tool_call::SubAgentMode::Foreground;
  session->card_message_id = parent_next_message_id_();
  session->deadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(timeout_seconds_);
  session->started_at = std::chrono::steady_clock::now();

  const auto filtered_emit = [this, session_ptr = session.get()](ChatEvent event) {
    event.message_id = session_ptr->card_message_id;
    event.sub_agent_id = session_ptr->agent_id;
    event.sub_agent_task = session_ptr->task;
    if (event.type == ChatEventType::ToolApprovalRequested) {
      event.text = "[Sub-agent: " +
                   TruncateWithEllipsis(session_ptr->task, 48) + "] " +
                   event.text;
      parent_emit_(std::move(event));
      return;
    }
    if (event.type == ChatEventType::ToolCallStarted ||
        event.type == ChatEventType::ToolCallDone) {
      if (event.type == ChatEventType::ToolCallDone) {
        event.sub_agent_tool_count = ToolCount(session_ptr->history);
      }
      event.type = ChatEventType::SubAgentProgress;
      parent_emit_(std::move(event));
    }
  };

  const auto next_message_id = [session_ptr = session.get()] {
    return session_ptr->next_id.fetch_add(1);
  };
  const auto config_snapshot = [this] {
    return MakeSubAgentConfig(parent_config_snapshot_());
  };
  const auto generation_value = [session_ptr = session.get()] {
    return session_ptr->generation.load();
  };
  session->prompt_processor =
      std::make_unique<internal::ChatServicePromptProcessor>(
          *registry_, *tool_executor_, *tool_approval_, session->history_mutex,
          session->history, filtered_emit, next_message_id, config_snapshot,
          generation_value, std::set<std::string>{"sub_agent"},
          &approval_gate_);

  const std::string agent_id = session->agent_id;
  {
    std::unique_lock lock(sessions_mutex_);
    active_sessions_.emplace(agent_id, std::move(session));
  }

  SubAgentSession* session_ptr = nullptr;
  {
    std::shared_lock lock(sessions_mutex_);
    session_ptr = active_sessions_.at(agent_id).get();
  }

  parent_emit_(ChatEvent{.type = ChatEventType::SubAgentStarted,
                         .message_id = session_ptr->card_message_id,
                         .role = ChatRole::Assistant,
                         .status = ChatMessageStatus::Active,
                         .sub_agent_id = session_ptr->agent_id,
                         .sub_agent_task = session_ptr->task});

  std::string result;
  int tool_count = 0;
  ChatEventType completion_type = ChatEventType::SubAgentCompleted;
  ChatMessageStatus completion_status = ChatMessageStatus::Complete;
  try {
    session_ptr->prompt_processor->ProcessPrompt(
        next_message_id(), task, 0, session_ptr->stop_source.get_token());
    {
      std::lock_guard lock(session_ptr->history_mutex);
      result = LastAssistantMessage(session_ptr->history);
      tool_count = ToolCount(session_ptr->history);
    }
    if (result.empty() && session_ptr->stop_source.stop_requested()) {
      completion_type = ChatEventType::SubAgentCancelled;
      completion_status = ChatMessageStatus::Cancelled;
      result = "Sub-agent cancelled.";
    }
  } catch (const std::exception& error) {
    completion_type = ChatEventType::SubAgentError;
    completion_status = ChatMessageStatus::Error;
    result = error.what();
  }

  result = TruncateWithEllipsis(result, kMaxResultChars);
  const auto elapsed_ms = static_cast<int>(std::chrono::duration_cast<
      std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                 session_ptr->started_at)
                                            .count());
  parent_emit_(ChatEvent{.type = completion_type,
                         .message_id = session_ptr->card_message_id,
                         .role = ChatRole::Assistant,
                         .text = result,
                         .status = completion_status,
                         .sub_agent_id = session_ptr->agent_id,
                         .sub_agent_task = session_ptr->task,
                         .sub_agent_result = result,
                         .sub_agent_tool_count = tool_count,
                         .sub_agent_elapsed_ms = elapsed_ms});

  {
    std::unique_lock lock(sessions_mutex_);
    active_sessions_.erase(agent_id);
  }
  return result;
}

std::string SubAgentManager::SpawnBackground(const std::string& task) {
  if (IsAtCapacity()) {
    return CapacityError();
  }

  auto session = std::make_unique<SubAgentSession>();
  session->agent_id = MakeAgentId();
  session->task = task;
  session->mode = tool_call::SubAgentMode::Background;
  session->card_message_id = parent_next_message_id_();
  session->deadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(timeout_seconds_);
  session->started_at = std::chrono::steady_clock::now();

  const auto filtered_emit = [this, session_ptr = session.get()](ChatEvent event) {
    if (std::chrono::steady_clock::now() > session_ptr->deadline) {
      session_ptr->timed_out = true;
      session_ptr->stop_source.request_stop();
    }
    event.message_id = session_ptr->card_message_id;
    event.sub_agent_id = session_ptr->agent_id;
    event.sub_agent_task = session_ptr->task;
    if (event.type == ChatEventType::ToolApprovalRequested) {
      event.text = "[Sub-agent: " +
                   TruncateWithEllipsis(session_ptr->task, 48) + "] " +
                   event.text;
      parent_emit_(std::move(event));
    }
  };

  const auto next_message_id = [session_ptr = session.get()] {
    return session_ptr->next_id.fetch_add(1);
  };
  const auto config_snapshot = [this] {
    return MakeSubAgentConfig(parent_config_snapshot_());
  };
  const auto generation_value = [session_ptr = session.get()] {
    return session_ptr->generation.load();
  };
  session->prompt_processor =
      std::make_unique<internal::ChatServicePromptProcessor>(
          *registry_, *tool_executor_, *tool_approval_, session->history_mutex,
          session->history, filtered_emit, next_message_id, config_snapshot,
          generation_value, std::set<std::string>{"sub_agent"},
          &approval_gate_);

  const std::string agent_id = session->agent_id;
  SubAgentSession* session_ptr = session.get();
  {
    std::unique_lock lock(sessions_mutex_);
    active_sessions_.emplace(agent_id, std::move(session));
  }

  parent_emit_(ChatEvent{.type = ChatEventType::SubAgentStarted,
                         .message_id = session_ptr->card_message_id,
                         .role = ChatRole::Assistant,
                         .status = ChatMessageStatus::Active,
                         .sub_agent_id = session_ptr->agent_id,
                         .sub_agent_task = session_ptr->task});

  session_ptr->worker = std::jthread([this, session_ptr](std::stop_token) {
    std::string result;
    int tool_count = 0;
    ChatEventType completion_type = ChatEventType::SubAgentCompleted;
    ChatMessageStatus completion_status = ChatMessageStatus::Complete;
    try {
      session_ptr->prompt_processor->ProcessPrompt(
          session_ptr->next_id.fetch_add(1), session_ptr->task,
          session_ptr->generation.load(), session_ptr->stop_source.get_token());
      {
        std::lock_guard lock(session_ptr->history_mutex);
        result = LastAssistantMessage(session_ptr->history);
        tool_count = ToolCount(session_ptr->history);
      }
      if (session_ptr->timed_out.load()) {
        completion_type = ChatEventType::SubAgentError;
        completion_status = ChatMessageStatus::Error;
        result = "Sub-agent timed out after " +
                 std::to_string(timeout_seconds_) + " seconds.";
      } else if (session_ptr->cancelled.load()) {
        completion_type = ChatEventType::SubAgentCancelled;
        completion_status = ChatMessageStatus::Cancelled;
        result = "Sub-agent cancelled.";
      }
    } catch (const std::exception& error) {
      completion_type = ChatEventType::SubAgentError;
      completion_status = ChatMessageStatus::Error;
      result = error.what();
    }

    result = TruncateWithEllipsis(result, kMaxResultChars);
    const auto elapsed_ms = static_cast<int>(std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                   session_ptr->started_at)
                                              .count());
    parent_emit_(ChatEvent{.type = completion_type,
                           .message_id = session_ptr->card_message_id,
                           .role = ChatRole::Assistant,
                           .text = result,
                           .status = completion_status,
                           .sub_agent_id = session_ptr->agent_id,
                           .sub_agent_task = session_ptr->task,
                           .sub_agent_result = result,
                           .sub_agent_tool_count = tool_count,
                           .sub_agent_elapsed_ms = elapsed_ms});
    session_ptr->finished = true;
  });

  return agent_id;
}

void SubAgentManager::Cancel(const std::string& agent_id) {
  CleanupFinishedSessions();

  std::shared_lock lock(sessions_mutex_);
  const auto it = active_sessions_.find(agent_id);
  if (it == active_sessions_.end()) {
    return;
  }
  it->second->cancelled = true;
  it->second->generation.fetch_add(1);
  it->second->stop_source.request_stop();
}

void SubAgentManager::CancelAll() {
  std::unique_lock lock(sessions_mutex_);
  for (auto& [agent_id, session] : active_sessions_) {
    (void)agent_id;
    session->cancelled = true;
    session->generation.fetch_add(1);
    session->stop_source.request_stop();
  }
  active_sessions_.clear();
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
  std::vector<std::unique_ptr<SubAgentSession>> finished_sessions;
  {
    std::unique_lock lock(sessions_mutex_);
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
      if (!it->second->finished.load()) {
        ++it;
        continue;
      }
      finished_sessions.push_back(std::move(it->second));
      it = active_sessions_.erase(it);
    }
  }
}

}  // namespace yac::chat
