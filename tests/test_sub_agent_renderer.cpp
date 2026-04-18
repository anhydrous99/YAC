#include "presentation/tool_call/renderer.hpp"
#include "tool_call/types.hpp"

#include <array>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;
using namespace yac::presentation::tool_call;
using namespace yac::tool_call;

namespace {

RenderContext MakeContext() {
  RenderContext context;
  context.terminal_width = 80;
  return context;
}

}  // namespace

TEST_CASE("ToolCallRenderer::BuildSummary for SubAgentCall includes task and status") {
  SubAgentCall call{.task = "analyze the codebase",
                    .status = SubAgentStatus::Running};

  const auto summary = ToolCallRenderer::BuildSummary(call);

  REQUIRE(summary.find("Sub-agent") != std::string::npos);
  REQUIRE(summary.find("analyze") != std::string::npos);
  REQUIRE(summary.find("running") != std::string::npos);
}

TEST_CASE("ToolCallRenderer::BuildLabel for SubAgentCall returns sub-agent label") {
  SubAgentCall call{.task = "test"};

  const auto label = ToolCallRenderer::BuildLabel(call);

  REQUIRE(label.find("Sub-agent") != std::string::npos);
}

TEST_CASE("ToolCallRenderer renders all SubAgentCall statuses without throwing") {
  const auto context = MakeContext();

  const std::array<SubAgentStatus, 6> statuses{
      SubAgentStatus::Pending,   SubAgentStatus::Running,
      SubAgentStatus::Complete,  SubAgentStatus::Error,
      SubAgentStatus::Cancelled, SubAgentStatus::Timeout};

  for (const auto status : statuses) {
    SubAgentCall call{.task = "test", .status = status};
    REQUIRE_NOTHROW(ToolCallRenderer::Render(call, context));
    auto element = ToolCallRenderer::Render(call, context);
    REQUIRE(element != nullptr);
  }
}
