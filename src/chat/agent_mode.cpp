#include "chat/agent_mode.hpp"

namespace yac::chat {

std::set<std::string> ExcludedToolsForMode(AgentMode mode) {
  switch (mode) {
    case AgentMode::Build:
      return {};
    case AgentMode::Plan:
      return {"bash", "file_write", "file_edit", "lsp_rename"};
  }
  return {};
}

}  // namespace yac::chat
