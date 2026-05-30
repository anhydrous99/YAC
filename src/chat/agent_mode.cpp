#include "chat/agent_mode.hpp"

#include "core_types/tool_call_types.hpp"

namespace yac::chat {

namespace {

bool IsPlanModeAllowedTool(std::string_view tool_name) {
  namespace tc = ::yac::tool_call;
  if (tool_name.starts_with(tc::kMcpToolNamePrefix)) {
    return false;
  }
  return tool_name == tc::kFileReadToolName ||
         tool_name == tc::kListDirToolName || tool_name == tc::kGlobToolName ||
         tool_name == tc::kGrepToolName ||
         tool_name == tc::kLspDiagnosticsToolName ||
         tool_name == tc::kLspReferencesToolName ||
         tool_name == tc::kLspGotoDefinitionToolName ||
         tool_name == tc::kLspSymbolsToolName ||
         tool_name == tc::kSubAgentToolName ||
         tool_name == tc::kAskUserToolName ||
         tool_name == tc::kTodoWriteToolName ||
         tool_name == tc::kPlanExitToolName;
}

}  // namespace

std::set<std::string> ExcludedToolsForMode(AgentMode mode) {
  switch (mode) {
    case AgentMode::Build:
      return {};
    case AgentMode::Plan:
      return {std::string(::yac::tool_call::kBashToolName),
              std::string(::yac::tool_call::kLspRenameToolName),
              std::string(::yac::tool_call::kFileWriteToolName),
              std::string(::yac::tool_call::kFileEditToolName)};
  }
  return {};
}

bool IsToolAllowedForMode(AgentMode mode, std::string_view tool_name) {
  switch (mode) {
    case AgentMode::Build:
      return tool_name != ::yac::tool_call::kPlanExitToolName;
    case AgentMode::Plan:
      return IsPlanModeAllowedTool(tool_name);
  }
  return true;
}

}  // namespace yac::chat
