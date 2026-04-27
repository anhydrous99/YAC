#pragma once

#include "core_types/chat_ids.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::core_types {

struct McpToolApprovalPolicy {
  bool requires_approval = false;
  bool server_requires_approval = false;
  std::vector<std::string> approval_required_tools;
};

struct McpToolCatalogSnapshot {
  uint64_t revision_id = 0;
  std::vector<::yac::chat::ToolDefinition> tools;
  std::unordered_map<std::string, std::pair<std::string, std::string>>
      name_to_server_tool;
  std::unordered_map<std::string, McpToolApprovalPolicy> approval_policy;
};

}  // namespace yac::core_types
