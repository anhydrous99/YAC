#include "presentation/mcp/mcp_resources_command.hpp"

#include <stop_token>
#include <string_view>

namespace yac::presentation {

McpResourcesCommandHandler::McpResourcesCommandHandler(
    core_types::IMcpManager* manager)
    : manager_(manager) {}

void McpResourcesCommandHandler::HandleCommand(std::string_view server_id,
                                               std::stop_token stop) {
  overlay_state_ = manager_->ListResources(server_id, stop);
}

void McpResourcesCommandHandler::ReadSelectedResource(
    std::string_view server_id, std::string_view uri, std::stop_token stop) {
  last_read_content_ = manager_->ReadResource(server_id, uri, stop);
}

const std::vector<core_types::McpResourceDescriptor>&
McpResourcesCommandHandler::GetOverlayState() const {
  return overlay_state_;
}

const std::optional<core_types::McpResourceContent>&
McpResourcesCommandHandler::GetLastReadContent() const {
  return last_read_content_;
}

}  // namespace yac::presentation
