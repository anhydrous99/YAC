#pragma once

#include "mcp/mcp_server_session.hpp"

#include <string>

namespace yac::mcp {

[[nodiscard]] inline std::string ToString(ServerState state) {
  switch (state) {
    case ServerState::Disconnected:
      return "Disconnected";
    case ServerState::Connecting:
      return "Connecting";
    case ServerState::Initializing:
      return "Initializing";
    case ServerState::Ready:
      return "Ready";
    case ServerState::Reconnecting:
      return "Reconnecting";
    case ServerState::Failed:
      return "Failed";
    case ServerState::ShuttingDown:
      return "ShuttingDown";
  }
  return "Unknown";
}

}  // namespace yac::mcp
