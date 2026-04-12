
#include "chat/chat_service.hpp"
#include "presentation/chat_ui.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <memory>
#include <string>
#include <utility>

#include <ftxui/component/app.hpp>

namespace {

void ApplyChatEvent(yac::presentation::ChatUI& chat_ui,
                    const yac::chat::ChatEvent& event) {
  using yac::chat::ChatEventType;
  using yac::presentation::Sender;

  switch (event.type) {
    case ChatEventType::Started:
      chat_ui.SetTyping(true);
      break;
    case ChatEventType::TextDelta:
      chat_ui.AppendToLastAgentMessage(event.text);
      break;
    case ChatEventType::Error:
      chat_ui.SetTyping(false);
      chat_ui.AddMessage(Sender::Agent, "Error: " + event.text);
      break;
    case ChatEventType::Finished:
    case ChatEventType::AssistantMessageDone:
    case ChatEventType::Cancelled:
      chat_ui.SetTyping(false);
      break;
    case ChatEventType::ToolCallStarted:
    case ChatEventType::ToolCallDone:
      break;
  }
}

}  // namespace

int main() {
  yac::provider::ProviderRegistry registry;
  registry.Register(std::make_shared<yac::provider::OpenAiChatProvider>());
  yac::chat::ChatService chat_service(std::move(registry));

  yac::presentation::ChatUI chat_ui;
  auto screen = ftxui::App::Fullscreen();

  chat_ui.SetOnSend(
      [&chat_service, &chat_ui, &screen](const std::string& message) {
        chat_service.SubmitUserMessage(
            message, [&chat_ui, &screen](yac::chat::ChatEvent event) {
              screen.Post([&chat_ui, event = std::move(event)] {
                ApplyChatEvent(chat_ui, event);
              });
            });
      });

  chat_ui.SetCommands({
      {"New Chat", "Start a fresh conversation"},
      {"Clear Messages", "Remove all messages from the view"},
      {"Toggle Theme", "Switch between light and dark themes"},
      {"Export Chat", "Save the current transcript"},
      {"Help", "Show keyboard shortcuts and tips"},
  });

  auto component = chat_ui.Build();
  screen.Loop(component);

  return 0;
}
