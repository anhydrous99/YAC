# This file is include()-ed by the root CMakeLists.txt and adds 4 extra sources to yac_service.
# The reason: chat module sources are split between root CMakeLists.txt and this file historically;
# new chat sources can go in either location, but check both before assuming a source is wired up.
target_sources(yac_service PRIVATE
  src/chat/agent_mode.cpp
  src/chat/chat_service_mcp.cpp
  src/chat/sub_agent_manager.cpp
  src/chat/tool_call_argument_parser.cpp
)
