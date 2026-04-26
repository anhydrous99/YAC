#include "mcp/tool_naming.hpp"

#include "core_types/tool_call_types.hpp"

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace yac::mcp {
namespace {

using yac::tool_call::kMcpToolNamePrefix;
using yac::tool_call::kMcpToolNameSeparator;

constexpr std::size_t kServerIdMax = 16;
constexpr std::size_t kToolNameMax = 40;
constexpr std::size_t kTruncatedToolPrefixLength = 35;

std::string SanitizeComponent(std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch) != 0 || ch == '-' || ch == '_') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }
  return sanitized;
}

std::uint64_t Fnv1a64(std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : value) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string Hex4(std::uint64_t value) {
  constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out(4, '0');
  for (std::size_t i = 0; i < 4; ++i) {
    out[3 - i] = kDigits[value & 0xF];
    value >>= 4;
  }
  return out;
}

std::string TruncateToolName(std::string_view sanitized_tool,
                             std::string_view raw_tool_name) {
  std::string out;
  out.reserve(kTruncatedToolPrefixLength + 6);
  out.append(sanitized_tool.substr(0, kTruncatedToolPrefixLength));
  out.append("_h");
  out.append(Hex4(Fnv1a64(raw_tool_name)));
  return out;
}

}  // namespace

std::string SanitizeMcpToolName(std::string_view server_id,
                                std::string_view raw_tool_name) {
  std::string sanitized_server = SanitizeComponent(server_id);
  if (sanitized_server.empty()) {
    throw std::invalid_argument("MCP server id cannot be empty");
  }
  if (sanitized_server.size() > kServerIdMax) {
    sanitized_server.resize(kServerIdMax);
  }

  std::string sanitized_tool = SanitizeComponent(raw_tool_name);
  if (sanitized_tool.size() > kToolNameMax) {
    sanitized_tool = TruncateToolName(sanitized_tool, raw_tool_name);
  }

  std::string qualified;
  qualified.reserve(kMcpToolNamePrefix.size() + kServerIdMax +
                    kMcpToolNameSeparator.size() + kToolNameMax + 6);
  qualified.append(kMcpToolNamePrefix);
  qualified.append(sanitized_server);
  qualified.append(kMcpToolNameSeparator);
  qualified.append(sanitized_tool);
  return qualified;
}

bool IsMcpToolName(std::string_view name) {
  return name.starts_with(kMcpToolNamePrefix) &&
         name.find(kMcpToolNameSeparator) != std::string_view::npos;
}

std::optional<std::pair<std::string, std::string>> SplitMcpToolName(
    std::string_view qualified) {
  if (!IsMcpToolName(qualified)) {
    return std::nullopt;
  }

  const std::size_t server_begin = kMcpToolNamePrefix.size();
  const std::size_t separator_pos =
      qualified.find(kMcpToolNameSeparator, server_begin);
  if (separator_pos == std::string_view::npos ||
      separator_pos == server_begin) {
    return std::nullopt;
  }

  const std::size_t tool_begin = separator_pos + kMcpToolNameSeparator.size();
  if (tool_begin >= qualified.size()) {
    return std::nullopt;
  }

  return std::make_pair(
      std::string{qualified.substr(server_begin, separator_pos - server_begin)},
      std::string{qualified.substr(tool_begin)});
}

}  // namespace yac::mcp
