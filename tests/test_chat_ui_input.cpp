#include "core_types/typed_ids.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/theme.hpp"
#include "util/mock_chat_actions.hpp"

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
using yac::ApprovalId;
using yac::test::MockChatActions;

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

ftxui::Event MakeKittyShiftTab() {
  return ftxui::Event::Special("\x1b[9;2u");
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

TEST_CASE("Default constructor creates empty message list") {
  ChatUI ui;
  REQUIRE(ui.GetMessages().empty());
}

TEST_CASE("Constructor with actions injects send-handler") {
  MockChatActions actions;
  ChatUI ui(actions);

  SECTION("AddMessage stores text under user sender") {
    ui.AddMessage(Sender::User, "hello");
    REQUIRE(ui.GetMessages().size() == 1);
    REQUIRE(ui.GetMessages()[0].CombinedText() == "hello");
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
  REQUIRE(msgs[0].CombinedText() == "first");
  REQUIRE(msgs[1].sender == Sender::Agent);
  REQUIRE(msgs[1].CombinedText() == "second");
  REQUIRE(msgs[2].sender == Sender::User);
  REQUIRE(msgs[2].CombinedText() == "third");
}

TEST_CASE("StartAgentMessage and AppendToAgentMessage stream content by ID") {
  ChatUI ui;

  auto id = ui.StartAgentMessage();
  ui.AppendToAgentMessage(id, "partial");
  ui.AppendToAgentMessage(id, " response");

  const auto& msgs = ui.GetMessages();
  REQUIRE(msgs.size() == 1);
  REQUIRE(msgs[0].sender == Sender::Agent);
  REQUIRE(msgs[0].CombinedText() == "partial response");
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

TEST_CASE("Composer and status rail share the canvas background color") {
  // Regression for the "docked chrome panel" seam: the composer and the
  // status rail used semantic.surface_panel, leaving a visible color step
  // against the terminal's OSC 11 background (chrome.canvas_bg). Both must
  // match chrome.canvas_bg so they blend with the real terminal background.
  ChatUI ui;
  auto component = ui.Build();

  constexpr int kWidth = 80;
  constexpr int kHeight = 24;
  auto screen = ftxui::Screen(kWidth, kHeight);
  ftxui::Render(screen, component->Render());

  const auto expected_bg =
      yac::presentation::theme::CurrentTheme().chrome.canvas_bg;
  const int x = kWidth / 2;
  // Layout (top→bottom): message list (flex) / separator / status rail /
  // composer (kMaxInputLines + 2*kComposerPadY = 5 rows).
  const int status_rail_row = kHeight - 6;
  const int composer_row = kHeight - 3;

  REQUIRE(screen.PixelAt(x, status_rail_row).background_color == expected_bg);
  REQUIRE(screen.PixelAt(x, composer_row).background_color == expected_bg);
}

TEST_CASE("ChatUI renders active provider and model in footer") {
  ChatUI ui;
  ui.SetProviderModel(::yac::ProviderId{"zai"}, ::yac::ModelId{"glm-5.1"});
  auto component = ui.Build();

  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("glm-5.1"));
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

TEST_CASE("ChatUI renders startup status in empty transcript") {
  ChatUI ui;
  ui.SetStartupStatus(StartupStatus{
      .provider_id = ::yac::ProviderId{"openai-compatible"},
      .model = ::yac::ModelId{"gpt-4o-mini"},
      .workspace_root = "/workspace",
      .api_key_env = "OPENAI_API_KEY",
      .api_key_configured = false,
      .lsp_command = "clangd",
      .lsp_available = false,
      .notices = {{.severity = UiSeverity::Warning,
                   .title = "OPENAI_API_KEY is not set",
                   .detail = "Set it before sending a request."}},
  });
  auto component = ui.Build();

  auto output = RenderComponent(component, 100, 30);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Ready"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("OPENAI_API_KEY"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[? help]"));
}

TEST_CASE("ChatUI renders queue depth") {
  ChatUI ui;
  ui.SetQueueDepth(2);
  auto component = ui.Build();

  auto output = RenderComponent(component, 100, 30);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("queued 2"));
}

TEST_CASE("ChatUI renders inline notice in transcript") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "hello");
  ui.AppendNotice(UiNotice{.severity = UiSeverity::Warning,
                           .title = "Model discovery failed",
                           .detail = "using configured model"});
  auto component = ui.Build();

  auto output = RenderComponent(component, 100, 30);

  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Model discovery failed"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("using configured model"));
}

TEST_CASE("ChatUI collapses consecutive duplicate notices") {
  ChatUI ui;
  ui.AppendNotice(
      UiNotice{.severity = UiSeverity::Info, .title = "Model switched"});
  ui.AppendNotice(
      UiNotice{.severity = UiSeverity::Info, .title = "Model switched"});
  ui.AppendNotice(
      UiNotice{.severity = UiSeverity::Info, .title = "Model switched"});

  REQUIRE(ui.GetNotices().size() == 1);
  REQUIRE(ui.GetNotices()[0].repeat == 3);
}

TEST_CASE("ChatUI schedules pending thinking animation ticks") {
  ChatUI ui;
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<ChatUI::UiTask> tasks;

  ui.SetUiTaskRunner([&](ChatUI::UiTask task) {
    {
      std::scoped_lock lock(mutex);
      tasks.push_back(std::move(task));
    }
    cv.notify_one();
  });

  ui.StartAgentMessage();

  std::unique_lock lock(mutex);
  REQUIRE(cv.wait_for(lock, std::chrono::seconds(2),
                      [&] { return !tasks.empty(); }));
}

TEST_CASE("ChatUI keeps thinking animation active after text streams") {
  ChatUI ui;
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<ChatUI::UiTask> tasks;

  ui.SetUiTaskRunner([&](ChatUI::UiTask task) {
    {
      std::scoped_lock lock(mutex);
      tasks.push_back(std::move(task));
    }
    cv.notify_one();
  });

  auto id = ui.StartAgentMessage();

  {
    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(2),
                        [&] { return !tasks.empty(); }));
    tasks.clear();
  }

  ui.AppendToAgentMessage(id, "partial");

  std::unique_lock lock(mutex);
  REQUIRE(cv.wait_for(lock, std::chrono::seconds(2),
                      [&] { return !tasks.empty(); }));
}

TEST_CASE("ChatUI renders large history without error and keeps tail visible") {
  ChatUI ui;
  for (int i = 0; i < 40; ++i) {
    ui.AddMessage(Sender::User, "msg-" + std::to_string(i));
  }
  ui.AddMessage(Sender::User, "tail-sentinel");

  auto component = ui.Build();
  // First render populates DynamicMessageStack's height cache.
  auto output1 = RenderComponent(component, 80, 12);
  REQUIRE_THAT(output1, Catch::Matchers::ContainsSubstring("tail-sentinel"));

  // Second render should be served by the virtualization fast path
  // (off-screen messages replaced by fillers) but must still show the tail
  // and a stable total layout.
  auto output2 = RenderComponent(component, 80, 12);
  REQUIRE_THAT(output2, Catch::Matchers::ContainsSubstring("tail-sentinel"));
}

TEST_CASE("ChatUI handles width change across renders with large history") {
  ChatUI ui;
  for (int i = 0; i < 30; ++i) {
    ui.AddMessage(Sender::Agent, "line-" + std::to_string(i));
  }
  ui.AddMessage(Sender::Agent, "wide-tail");

  auto component = ui.Build();
  auto wide = RenderComponent(component, 120, 15);
  auto narrow = RenderComponent(component, 40, 15);

  REQUIRE_THAT(wide, Catch::Matchers::ContainsSubstring("wide-tail"));
  REQUIRE_THAT(narrow, Catch::Matchers::ContainsSubstring("wide-tail"));
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

TEST_CASE("ChatUI composer soft-wraps long typed input") {
  ChatUI ui;
  auto component = ui.Build();

  std::string text;
  for (int i = 0; i < 30; ++i) {
    text += "word ";
  }
  text += "TAIL";

  TypeText(component, text);

  auto output = RenderComponent(component, 40, 12);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("3/3"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("TAIL"));
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

TEST_CASE("HandleInputEvent toggles mode on Shift+Tab") {
  MockChatActions actions;
  ChatUI ui(actions);

  REQUIRE(ui.HandleInputEvent(ftxui::Event::TabReverse));
  REQUIRE(actions.mode_toggles == 1);

  REQUIRE(ui.HandleInputEvent(MakeKittyShiftTab()));
  REQUIRE(actions.mode_toggles == 2);
}

TEST_CASE("HandleInputEvent does not toggle mode on plain Tab") {
  MockChatActions actions;
  ChatUI ui(actions);

  REQUIRE_FALSE(ui.HandleInputEvent(ftxui::Event::Tab));
  REQUIRE(actions.mode_toggles == 0);
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
  MockChatActions actions;
  ChatUI ui(actions);

  (void)ui.HandleInputEvent(ftxui::Event::Return);
  REQUIRE(actions.sent_messages.empty());

  auto component = ui.Build();
  TypeText(component, "hello");
  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.sent_messages == std::vector<std::string>{"hello"});
  REQUIRE(ui.CalculateInputHeight() == 1);
}

TEST_CASE("HandleInputEvent submits soft-wrapped content unchanged") {
  MockChatActions actions;
  ChatUI ui(actions);
  auto component = ui.Build();
  std::string typed;
  for (int i = 0; i < 30; ++i) {
    typed += "word ";
  }
  typed += "unchanged";

  TypeText(component, typed);
  auto output = RenderComponent(component, 32, 12);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("3/3"));

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.sent_messages == std::vector<std::string>{typed});
}

TEST_CASE("Whitespace-only input is not submitted") {
  MockChatActions actions;
  ChatUI ui(actions);
  auto component = ui.Build();

  TypeText(component, "   ");
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(actions.sent_messages.empty());
}

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
