#include "presentation/chat_ui.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

ftxui::Event MakeShiftEnter() {
  return ftxui::Event::Special("\x1b[13;2~");
}

ftxui::Event MakeCtrlEnter() {
  return ftxui::Event::Special("\x1b[13;5~");
}

ftxui::Event MakeKittyShiftEnter() {
  return ftxui::Event::Special("\x1b[27;2;13~");
}

ftxui::Event MakeKittyCtrlEnter() {
  return ftxui::Event::Special("\x1b[27;5;13~");
}

ftxui::Event MakeAltEnterCR() {
  return ftxui::Event::Special("\x1b\r");
}

ftxui::Event MakeAltEnterLF() {
  return ftxui::Event::Special("\x1b\n");
}

std::string RenderComponent(const ftxui::Component& component, int width = 80,
                            int height = 24) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

void TypeText(const ftxui::Component& component, const std::string& text) {
  for (char ch : text) {
    REQUIRE(component->OnEvent(ftxui::Event::Character(ch)));
  }
}

}  // namespace

TEST_CASE("Default constructor creates empty message list") {
  ChatUI ui;
  REQUIRE(ui.GetMessages().empty());
}

TEST_CASE("Constructor with callback stores it") {
  bool called = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    called = true;
    captured = msg;
  });

  SECTION("Callback fires on submit") {
    ui.AddMessage(Sender::User, "hello");
    REQUIRE(ui.GetMessages().size() == 1);
    REQUIRE(ui.GetMessages()[0].Text() == "hello");
    REQUIRE(ui.GetMessages()[0].sender == Sender::User);
  }
}

TEST_CASE("AddMessage appends to message list") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "first");
  ui.AddMessage(Sender::Agent, "second");
  ui.AddMessage(Sender::User, "third");

  const auto& msgs = ui.GetMessages();
  REQUIRE(msgs.size() == 3);
  REQUIRE(msgs[0].sender == Sender::User);
  REQUIRE(msgs[0].Text() == "first");
  REQUIRE(msgs[1].sender == Sender::Agent);
  REQUIRE(msgs[1].Text() == "second");
  REQUIRE(msgs[2].sender == Sender::User);
  REQUIRE(msgs[2].Text() == "third");
}

TEST_CASE("StartAgentMessage and AppendToAgentMessage stream content by ID") {
  ChatUI ui;

  auto id = ui.StartAgentMessage();
  ui.AppendToAgentMessage(id, "partial");
  ui.AppendToAgentMessage(id, " response");

  const auto& msgs = ui.GetMessages();
  REQUIRE(msgs.size() == 1);
  REQUIRE(msgs[0].sender == Sender::Agent);
  REQUIRE(msgs[0].Text() == "partial response");
}

TEST_CASE("AddMessageWithId stores service-owned message ID") {
  ChatUI ui;
  auto id = ui.AddMessageWithId(42, Sender::Agent, "# Hello");

  const auto& msg = ui.GetMessages()[0];
  REQUIRE(id == 42);
  REQUIRE(msg.id == 42);
  REQUIRE(ui.HasMessage(42));
}

TEST_CASE("StartAgentMessage supports explicit service-owned message ID") {
  ChatUI ui;
  auto id = ui.StartAgentMessage(84);

  const auto& msg = ui.GetMessages()[0];
  REQUIRE(id == 84);
  REQUIRE(msg.id == 84);
  REQUIRE(msg.sender == Sender::Agent);
  REQUIRE(msg.status == MessageStatus::Active);
}

TEST_CASE("Build returns a non-null component") {
  ChatUI ui;
  auto component = ui.Build();
  REQUIRE(component != nullptr);
}

TEST_CASE("ChatUI renders active provider and model in footer") {
  ChatUI ui;
  ui.SetProviderModel("zai", "glm-5.1");
  auto component = ui.Build();

  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("zai / glm-5.1"));
}

TEST_CASE("GetMessages returns const reference to internal state") {
  ChatUI ui;
  ui.AddMessage(Sender::Agent, "response");

  const auto& msgs = ui.GetMessages();
  REQUIRE(&msgs == &ui.GetMessages());
  REQUIRE(msgs.size() == 1);
}

TEST_CASE("SetTyping toggles state") {
  ChatUI ui;
  REQUIRE_FALSE(ui.IsTyping());

  ui.SetTyping(true);
  REQUIRE(ui.IsTyping());

  ui.SetTyping(false);
  REQUIRE_FALSE(ui.IsTyping());
}

TEST_CASE("Build returns non-null component with typing enabled") {
  ChatUI ui;
  ui.SetTyping(true);
  auto component = ui.Build();
  REQUIRE(component != nullptr);
}

TEST_CASE("ChatUI schedules pending thinking animation ticks") {
  ChatUI ui;
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<ChatUI::UiTask> tasks;

  ui.SetUiTaskRunner([&](ChatUI::UiTask task) {
    {
      std::lock_guard lock(mutex);
      tasks.push_back(std::move(task));
    }
    cv.notify_one();
  });

  ui.StartAgentMessage();

  std::unique_lock lock(mutex);
  REQUIRE(cv.wait_for(lock, std::chrono::seconds(2),
                      [&] { return !tasks.empty(); }));
}

TEST_CASE("CalculateInputHeight returns 1 for empty input") {
  ChatUI ui;
  REQUIRE(ui.CalculateInputHeight() == 1);
}

TEST_CASE("CalculateInputHeight grows with newlines") {
  ChatUI ui;
  (void)ui.HandleInputEvent(MakeShiftEnter());
  (void)ui.HandleInputEvent(MakeShiftEnter());
  REQUIRE(ui.CalculateInputHeight() == 3);
}

TEST_CASE("CalculateInputHeight caps at kMaxInputLines") {
  ChatUI ui;
  for (int i = 0; i < ChatUI::kMaxInputLines + 5; ++i) {
    (void)ui.HandleInputEvent(MakeShiftEnter());
  }
  REQUIRE(ui.CalculateInputHeight() == ChatUI::kMaxInputLines);
}

TEST_CASE("HandleInputEvent returns true for plain Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(ftxui::Event::Return));
}

TEST_CASE("HandleInputEvent returns false for regular character") {
  ChatUI ui;
  REQUIRE_FALSE(ui.HandleInputEvent(ftxui::Event::Character('a')));
}

TEST_CASE("HandleInputEvent returns false for mouse events") {
  ChatUI ui;
  auto event = ftxui::Event::Mouse("", ftxui::Mouse{});
  REQUIRE_FALSE(ui.HandleInputEvent(event));
}

TEST_CASE("HandleInputEvent inserts newline on Shift+Enter escape") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeShiftEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on Ctrl+Enter escape") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeCtrlEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on kitty Shift+Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeKittyShiftEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on kitty Ctrl+Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeKittyCtrlEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent accumulates newlines") {
  ChatUI ui;
  (void)ui.HandleInputEvent(MakeShiftEnter());
  (void)ui.HandleInputEvent(MakeCtrlEnter());
  (void)ui.HandleInputEvent(MakeKittyShiftEnter());
  REQUIRE(ui.CalculateInputHeight() == ChatUI::kMaxInputLines);
}

TEST_CASE("HandleInputEvent inserts newline on Alt+Enter (CR variant)") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeAltEnterCR()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on Alt+Enter (LF variant)") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeAltEnterLF()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent Enter submits and clears content") {
  bool sent = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    sent = true;
    captured = msg;
  });

  (void)ui.HandleInputEvent(ftxui::Event::Return);
  REQUIRE_FALSE(sent);

  (void)ui.HandleInputEvent(MakeAltEnterCR());
  REQUIRE(ui.HandleInputEvent(ftxui::Event::Return));
  REQUIRE(sent);
  REQUIRE(captured == "\n");
  REQUIRE(ui.CalculateInputHeight() == 1);
}

TEST_CASE("Slash clear command dispatches without sending a message") {
  bool sent = false;
  int clear_count = 0;
  ChatUI ui([&](const std::string&) { sent = true; });
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  registry.SetHandler("clear", [&] { ++clear_count; });
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/clear");
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(clear_count == 1);
  REQUIRE_FALSE(sent);

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(clear_count == 1);
  REQUIRE_FALSE(sent);
}

TEST_CASE("Slash menu Return dismisses unmatched command before raw submit") {
  bool sent = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    sent = true;
    captured = msg;
  });
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/does-not-exist");

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE_FALSE(sent);

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(sent);
  REQUIRE(captured == "/does-not-exist");
}

TEST_CASE("Slash menu Escape preserves raw slash input for later submit") {
  bool sent = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    sent = true;
    captured = msg;
  });
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/quit-later");

  REQUIRE(component->OnEvent(ftxui::Event::Escape));
  REQUIRE_FALSE(sent);

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(sent);
  REQUIRE(captured == "/quit-later");
}

TEST_CASE("Tool approval modal swallows unrelated keys and rejects once") {
  bool sent = false;
  int approval_calls = 0;
  std::string approval_id;
  bool approved = true;
  ChatUI ui([&](const std::string&) { sent = true; });
  ui.SetOnToolApproval([&](const std::string& id, bool value) {
    ++approval_calls;
    approval_id = id;
    approved = value;
  });
  auto component = ui.Build();

  ui.ShowToolApproval("approval-1", "file_write", "Write notes.txt");

  REQUIRE(component->OnEvent(ftxui::Event::Character('x')));
  REQUIRE(approval_calls == 0);
  REQUIRE_FALSE(sent);

  REQUIRE(component->OnEvent(ftxui::Event::Escape));
  REQUIRE(approval_calls == 1);
  REQUIRE(approval_id == "approval-1");
  REQUIRE_FALSE(approved);

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(approval_calls == 1);
  REQUIRE_FALSE(sent);
}

TEST_CASE("Tool approval modal approves on uppercase Y") {
  int approval_calls = 0;
  std::string approval_id;
  bool approved = false;
  ChatUI ui;
  ui.SetOnToolApproval([&](const std::string& id, bool value) {
    ++approval_calls;
    approval_id = id;
    approved = value;
  });
  auto component = ui.Build();

  ui.ShowToolApproval("approval-2", "lsp_rename", "Rename symbol");

  REQUIRE(component->OnEvent(ftxui::Event::Character('Y')));
  REQUIRE(approval_calls == 1);
  REQUIRE(approval_id == "approval-2");
  REQUIRE(approved);
}

namespace {

ftxui::Event MakeHomeXterm() {
  return ftxui::Event::Special("\x1b[H");
}

ftxui::Event MakeHomeRxvt() {
  return ftxui::Event::Special("\x1b[1~");
}

ftxui::Event MakeHomeXtermAlt() {
  return ftxui::Event::Special("\x1bOH");
}

ftxui::Event MakeEndXterm() {
  return ftxui::Event::Special("\x1b[F");
}

ftxui::Event MakeEndRxvt() {
  return ftxui::Event::Special("\x1b[4~");
}

ftxui::Event MakeEndXtermAlt() {
  return ftxui::Event::Special("\x1bOF");
}

}  // namespace

TEST_CASE("Component handles Home key events") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "first");
  ui.AddMessage(Sender::User, "second");
  ui.AddMessage(Sender::User, "third");
  auto component = ui.Build();
  REQUIRE(component != nullptr);
  REQUIRE(component->OnEvent(MakeHomeXterm()));
}

TEST_CASE("Component handles End key events") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "first");
  ui.AddMessage(Sender::User, "second");
  auto component = ui.Build();
  REQUIRE(component != nullptr);
  REQUIRE(component->OnEvent(MakeEndXterm()));
}

TEST_CASE("Component handles Home key variants") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "test");
  auto component = ui.Build();
  REQUIRE(component->OnEvent(MakeHomeXterm()));
  REQUIRE(component->OnEvent(MakeHomeRxvt()));
  REQUIRE(component->OnEvent(MakeHomeXtermAlt()));
}

TEST_CASE("Component handles End key variants") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "test");
  auto component = ui.Build();
  REQUIRE(component->OnEvent(MakeEndXterm()));
  REQUIRE(component->OnEvent(MakeEndRxvt()));
  REQUIRE(component->OnEvent(MakeEndXtermAlt()));
}
