#pragma once

#include "core_types/mcp_manager_interface.hpp"
#include "core_types/mcp_resource_types.hpp"

#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace yac::presentation {

class McpResourcesCommandHandler {
 public:
  explicit McpResourcesCommandHandler(core_types::IMcpManager* manager);

  void HandleCommand(std::string_view server_id, std::stop_token stop);
  void ReadSelectedResource(std::string_view server_id, std::string_view uri,
                            std::stop_token stop);

  [[nodiscard]] const std::vector<core_types::McpResourceDescriptor>&
  GetOverlayState() const;

  [[nodiscard]] const std::optional<core_types::McpResourceContent>&
  GetLastReadContent() const;

 private:
  core_types::IMcpManager* manager_;
  std::vector<core_types::McpResourceDescriptor> overlay_state_;
  std::optional<core_types::McpResourceContent> last_read_content_;
};

}  // namespace yac::presentation
