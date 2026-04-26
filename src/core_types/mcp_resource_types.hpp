#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace yac::core_types {

struct McpResourceDescriptor {
  std::string uri;
  std::string name;
  std::string title;
  std::string description;
  std::string mime_type;
  uintmax_t size = 0;
};

struct McpResourceContent {
  std::string uri;
  std::string mime_type;
  std::string text;
  std::vector<std::byte> blob;
  bool is_truncated = false;
  uintmax_t total_bytes = 0;
};

struct McpServerStatus {
  std::string id;
  std::string state;
  std::string error;
  std::string transport;
  size_t tool_count = 0;
  size_t resource_count = 0;
};

}  // namespace yac::core_types
