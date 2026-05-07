#include "core_types/typed_ids.hpp"

#include <format>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

using yac::ApprovalId;
using yac::McpServerId;
using yac::ModelId;
using yac::ProviderId;
using yac::StringId;
using yac::SubAgentId;
using yac::ToolCallId;

TEST_CASE("StringId construction stores the underlying value") {
  ApprovalId a{"approval-42"};
  REQUIRE(a.value == "approval-42");

  ToolCallId t{std::string_view{"tc_abc"}};
  REQUIRE(t.value == "tc_abc");
}

TEST_CASE("StringId equality compares value within the same tag") {
  ApprovalId a{"x"};
  ApprovalId b{"x"};
  REQUIRE(a == b);
  REQUIRE_FALSE(a != b);
}

TEST_CASE("StringId inequality detects differing values") {
  McpServerId a{"server-a"};
  McpServerId b{"server-b"};
  REQUIRE(a != b);
  REQUIRE_FALSE(a == b);
}

TEST_CASE("StringId ordering follows underlying string ordering") {
  SubAgentId a{"alpha"};
  SubAgentId b{"beta"};
  REQUIRE(a < b);
  REQUIRE(b > a);
  REQUIRE(a <= b);
  REQUIRE(b >= a);
  REQUIRE((a <=> b) == std::strong_ordering::less);

  SubAgentId c{"alpha"};
  REQUIRE((a <=> c) == std::strong_ordering::equal);
}

TEST_CASE("std::format renders the underlying string value") {
  ModelId m{"gpt-4o-mini"};
  REQUIRE(std::format("{}", m) == "gpt-4o-mini");

  ProviderId p{"openai-compatible"};
  REQUIRE(std::format("provider={}", p) == "provider=openai-compatible");
}

TEST_CASE("ApprovalId works as unordered_map key via std::hash") {
  std::unordered_map<ApprovalId, int> counts;
  counts[ApprovalId{"a"}] = 1;
  counts[ApprovalId{"b"}] = 2;
  counts[ApprovalId{"a"}] = 7;

  REQUIRE(counts.size() == 2);
  REQUIRE(counts[ApprovalId{"a"}] == 7);
  REQUIRE(counts[ApprovalId{"b"}] == 2);
  REQUIRE_FALSE(counts.contains(ApprovalId{"missing"}));
}

TEST_CASE("ToolCallId works as unordered_map key via std::hash") {
  std::unordered_map<ToolCallId, std::string> labels;
  labels[ToolCallId{"call_1"}] = "first";
  labels[ToolCallId{"call_2"}] = "second";

  REQUIRE(labels.at(ToolCallId{"call_1"}) == "first");
  REQUIRE(labels.at(ToolCallId{"call_2"}) == "second");
  REQUIRE(labels.size() == 2);
}

TEST_CASE("Default-constructed StringId has an empty value") {
  ApprovalId a;
  ToolCallId t;
  REQUIRE(a.value.empty());
  REQUIRE(t.value.empty());
  REQUIRE(a == ApprovalId{""});
}

TEST_CASE("Distinct tag aliases are not the same type") {
  STATIC_REQUIRE_FALSE(std::is_same_v<ApprovalId, ToolCallId>);
  STATIC_REQUIRE_FALSE(std::is_same_v<ModelId, ProviderId>);
  STATIC_REQUIRE_FALSE(std::is_same_v<McpServerId, SubAgentId>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<std::string, ApprovalId>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<ApprovalId, ToolCallId>);
}
