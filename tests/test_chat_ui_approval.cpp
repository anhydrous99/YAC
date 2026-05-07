#include "core_types/typed_ids.hpp"
#include "presentation/chat_ui.hpp"
#include "tool_call/types.hpp"
#include "util/mock_chat_actions.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using yac::ApprovalId;
using yac::test::MockChatActions;

namespace {

std::string RenderComponent(const ftxui::Component& component, int width = 80,
                            int height = 24) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

}  // namespace

TEST_CASE("Tool approval modal swallows unrelated keys and rejects once") {
  MockChatActions actions;
  ChatUI ui(actions);
  auto component = ui.Build();

  ui.ShowToolApproval(ApprovalId{"approval-1"}, "file_write",
                      "Write notes.txt");

  REQUIRE(component->OnEvent(ftxui::Event::Character('x')));
  REQUIRE(actions.tool_approvals.empty());
  REQUIRE(actions.sent_messages.empty());

  REQUIRE(component->OnEvent(ftxui::Event::Escape));
  REQUIRE(actions.tool_approvals.size() == 1);
  REQUIRE(actions.tool_approvals[0].first == ApprovalId{"approval-1"});
  REQUIRE_FALSE(actions.tool_approvals[0].second);

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.tool_approvals.size() == 1);
  REQUIRE(actions.sent_messages.empty());
}

TEST_CASE("Tool approval modal renders prominent permission prompt") {
  ChatUI ui;
  auto component = ui.Build();

  ui.ShowToolApproval(ApprovalId{"approval-1"}, "file_write",
                      "Write notes.txt");

  auto output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Permission Required"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("file_write"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Write notes.txt"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Enter/Y Approve"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("N/Esc Reject"));
}

TEST_CASE("Tool approval modal renders tool preview") {
  ChatUI ui;
  auto component = ui.Build();

  ::yac::tool_call::ToolCallBlock preview =
      ::yac::tool_call::FileWriteCall{.filepath = "notes.txt",
                                      .content_preview = "hello\n",
                                      .content_tail = "hello\n",
                                      .lines_added = 1};
  ui.ShowToolApproval(ApprovalId{"approval-1"}, "file_write", "Write notes.txt",
                      std::move(preview));

  auto output = RenderComponent(component, 100, 30);
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Permission Required"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("write"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("notes.txt"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("hello"));
}

TEST_CASE("Tool approval modal approves on uppercase Y") {
  MockChatActions actions;
  ChatUI ui(actions);
  auto component = ui.Build();

  ui.ShowToolApproval(ApprovalId{"approval-2"}, "lsp_rename", "Rename symbol");

  REQUIRE(component->OnEvent(ftxui::Event::Character('Y')));
  REQUIRE(actions.tool_approvals.size() == 1);
  REQUIRE(actions.tool_approvals[0].first == ApprovalId{"approval-2"});
  REQUIRE(actions.tool_approvals[0].second);
}
