#pragma once

#include <compare>
#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace yac {

// Strongly-typed string ID wrapper. Distinct `Tag` types make differently
// typed IDs (e.g. `ApprovalId` vs `ToolCallId`) incompatible at compile time
// without changing the on-the-wire representation: the underlying value is
// just a `std::string`. Construction is `explicit`, blocking accidental
// implicit conversions from raw strings or between unrelated tags.
template <typename Tag>
struct StringId {
  std::string value;

  explicit StringId() = default;
  explicit StringId(std::string v) : value(std::move(v)) {}
  explicit StringId(std::string_view v) : value(v) {}
  explicit StringId(const char* v) : value(v) {}

  bool operator==(const StringId&) const = default;
  auto operator<=>(const StringId&) const = default;
};

struct ApprovalIdTag {};
using ApprovalId = StringId<ApprovalIdTag>;

struct ToolCallIdTag {};
using ToolCallId = StringId<ToolCallIdTag>;

struct McpServerIdTag {};
using McpServerId = StringId<McpServerIdTag>;

struct SubAgentIdTag {};
using SubAgentId = StringId<SubAgentIdTag>;

struct ModelIdTag {};
using ModelId = StringId<ModelIdTag>;

struct ProviderIdTag {};
using ProviderId = StringId<ProviderIdTag>;

}  // namespace yac

template <typename Tag>
struct std::hash<yac::StringId<Tag>> {
  size_t operator()(const yac::StringId<Tag>& id) const noexcept {
    return std::hash<std::string>{}(id.value);
  }
};

template <typename Tag>
struct std::formatter<yac::StringId<Tag>> : std::formatter<std::string_view> {
  auto format(const yac::StringId<Tag>& id, auto& ctx) const {
    return std::formatter<std::string_view>::format(id.value, ctx);
  }
};
