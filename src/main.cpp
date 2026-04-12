
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <ftxui/component/app.hpp>

namespace {

class EventBridge {
 public:
  EventBridge(yac::presentation::ChatUI& chat_ui,
              yac::chat::ChatService& chat_service)
      : chat_ui_(chat_ui), chat_service_(chat_service) {}

  void HandleEvent(yac::chat::ChatEvent event) {
    using yac::chat::ChatEventType;
    using yac::chat::ChatMessageStatus;
    using yac::presentation::MessageStatus;
    using yac::presentation::Sender;

    switch (event.type) {
      case ChatEventType::UserMessageQueued:
        break;

      case ChatEventType::UserMessageActive:
        chat_ui_.SetMessageStatus(event.message_id, MessageStatus::Active);
        break;

      case ChatEventType::Started: {
        auto agent_id = chat_ui_.StartAgentMessage();
        agent_ids_[event.message_id] = agent_id;
        chat_ui_.SetTyping(true);
        break;
      }

      case ChatEventType::TextDelta: {
        auto it = agent_ids_.find(event.message_id);
        if (it != agent_ids_.end()) {
          chat_ui_.AppendToAgentMessage(it->second, event.text);
        }
        break;
      }

      case ChatEventType::Error: {
        chat_ui_.SetTyping(false);
        auto it = agent_ids_.find(event.message_id);
        if (it != agent_ids_.end()) {
          chat_ui_.AppendToAgentMessage(it->second, "Error: " + event.text);
          chat_ui_.SetMessageStatus(it->second, MessageStatus::Error);
          agent_ids_.erase(it);
        } else {
          chat_ui_.AddMessage(Sender::Agent, "Error: " + event.text,
                              MessageStatus::Error);
        }
        break;
      }

      case ChatEventType::AssistantMessageDone:
        chat_ui_.SetTyping(false);
        (void)SetMappedAgentStatus(event.message_id, MessageStatus::Complete);
        break;

      case ChatEventType::Finished:
        chat_ui_.SetTyping(false);
        (void)SetMappedAgentStatus(event.message_id, MessageStatus::Complete);
        break;

      case ChatEventType::Cancelled:
        chat_ui_.SetTyping(false);
        if (!SetMappedAgentStatus(event.message_id, MessageStatus::Cancelled)) {
          chat_ui_.SetMessageStatus(event.message_id, MessageStatus::Cancelled);
        }
        break;

      case ChatEventType::MessageStatusChanged:
        if (!SetMappedAgentStatus(event.message_id, event.status)) {
          chat_ui_.SetMessageStatus(event.message_id, event.status);
        }
        break;

      case ChatEventType::ConversationCleared:
        chat_ui_.ClearMessages();
        chat_ui_.SetTyping(false);
        agent_ids_.clear();
        break;

      case ChatEventType::QueueDepthChanged:
        break;

      case ChatEventType::ToolCallStarted:
      case ChatEventType::ToolCallDone:
        break;
    }
  }

 private:
  bool SetMappedAgentStatus(yac::chat::ChatMessageId service_message_id,
                            yac::presentation::MessageStatus status) {
    auto it = agent_ids_.find(service_message_id);
    if (it == agent_ids_.end()) {
      return false;
    }

    chat_ui_.SetMessageStatus(it->second, status);
    if (IsTerminalStatus(status)) {
      agent_ids_.erase(it);
    }
    return true;
  }

  static bool IsTerminalStatus(yac::presentation::MessageStatus status) {
    using yac::presentation::MessageStatus;
    return status == MessageStatus::Complete ||
           status == MessageStatus::Cancelled || status == MessageStatus::Error;
  }

  yac::presentation::ChatUI& chat_ui_;
  yac::chat::ChatService& chat_service_;
  std::unordered_map<yac::chat::ChatMessageId, yac::presentation::MessageId>
      agent_ids_;
};

}  // namespace

int main() {
  auto config = yac::chat::LoadChatConfigFromEnv();

  yac::provider::ProviderRegistry registry;
  registry.Register(std::make_shared<yac::provider::OpenAiChatProvider>(
      yac::chat::ProviderConfig{
          .id = config.provider_id,
          .model = config.model,
          .api_key = config.api_key,
          .api_key_env = config.api_key_env,
          .base_url = config.base_url,
      }));
  yac::chat::ChatService chat_service(std::move(registry), config);

  yac::presentation::ChatUI chat_ui;
  auto screen = ftxui::App::Fullscreen();

  EventBridge bridge(chat_ui, chat_service);

  chat_service.SetEventCallback([&bridge, &screen](yac::chat::ChatEvent event) {
    screen.Post(
        [&bridge, event = std::move(event)] { bridge.HandleEvent(event); });
  });

  chat_ui.SetOnSend([&chat_service](const std::string& message) {
    chat_service.SubmitUserMessage(message);
  });

  chat_ui.SetOnCommand([&chat_service](const std::string& command) {
    if (command == "New Chat" || command == "Clear Messages") {
      chat_service.ResetConversation();
    } else if (command == "Cancel Response") {
      chat_service.CancelActiveResponse();
    }
  });

  chat_ui.SetCommands({
      {"New Chat", "Start a fresh conversation"},
      {"Clear Messages", "Remove all messages from the view"},
      {"Cancel Response", "Stop the current assistant response"},
      {"Toggle Theme", "Switch between light and dark themes"},
      {"Export Chat", "Save the current transcript"},
      {"Help", "Show keyboard shortcuts and tips"},
  });

  auto exit_loop = screen.ExitLoopClosure();
  yac::presentation::SlashCommandRegistry slash_registry;
  slash_registry.Register({"quit", "Exit the application", exit_loop});
  slash_registry.Register({"exit", "Exit the application", exit_loop});
  chat_ui.SetSlashCommands(std::move(slash_registry));

  auto component = chat_ui.Build();
  screen.Loop(component);

  return 0;
}
