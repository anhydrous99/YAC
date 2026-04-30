#include "mock_mcp_manager.hpp"
#include "presentation/mcp/mcp_resources_command.hpp"

#include <stop_token>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class ResourceMockMcpManager : public yac::test::MockMcpManager {
 public:
  void SetListResult(
      std::vector<yac::core_types::McpResourceDescriptor> result) {
    list_result_ = std::move(result);
  }

  void SetReadResult(yac::core_types::McpResourceContent result) {
    read_result_ = std::move(result);
  }

  std::vector<yac::core_types::McpResourceDescriptor> ListResources(
      std::string_view /*unused*/, std::stop_token /*unused*/) override {
    ++list_count;
    return list_result_;
  }

  yac::core_types::McpResourceContent ReadResource(
      std::string_view /*unused*/, std::string_view /*unused*/,
      std::stop_token /*unused*/) override {
    ++read_count;
    return read_result_;
  }

  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  // Test mock: counters intentionally exposed for direct assertion.
  int list_count = 0;
  int read_count = 0;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

 private:
  std::vector<yac::core_types::McpResourceDescriptor> list_result_;
  yac::core_types::McpResourceContent read_result_;
};

}  // namespace

TEST_CASE("list_renders_descriptors", "[mcp_resources]") {
  ResourceMockMcpManager mock;
  mock.SetListResult({
      {.uri = "res://a", .name = "alpha"},
      {.uri = "res://b", .name = "beta"},
      {.uri = "res://c", .name = "gamma"},
  });

  yac::presentation::McpResourcesCommandHandler handler{&mock};
  std::stop_source stop_source;
  handler.HandleCommand("myserver", stop_source.get_token());

  const auto& state = handler.GetOverlayState();
  REQUIRE(state.size() == 3);
  REQUIRE(state[0].uri == "res://a");
  REQUIRE(state[0].name == "alpha");
  REQUIRE(state[1].uri == "res://b");
  REQUIRE(state[1].name == "beta");
  REQUIRE(state[2].uri == "res://c");
  REQUIRE(state[2].name == "gamma");
  REQUIRE(mock.list_count == 1);
}

TEST_CASE("read_inserts_message", "[mcp_resources]") {
  ResourceMockMcpManager mock;
  mock.SetReadResult(
      yac::core_types::McpResourceContent{.uri = "res://a", .text = "hello"});

  yac::presentation::McpResourcesCommandHandler handler{&mock};
  std::stop_source stop_source;
  handler.ReadSelectedResource("myserver", "res://a", stop_source.get_token());

  const auto& content = handler.GetLastReadContent();
  REQUIRE(content.has_value());
  REQUIRE(content->text == "hello");
  REQUIRE(content->uri == "res://a");

  REQUIRE(handler.GetOverlayState().empty());
  REQUIRE(mock.list_count == 0);
}
