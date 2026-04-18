#pragma once

#include <string>
#include <vector>

namespace yac::presentation {

enum class UiSeverity { Info, Warning, Error };

struct UiNotice {
  UiSeverity severity = UiSeverity::Info;
  std::string title;
  std::string detail;
};

struct StartupStatus {
  std::string provider_id;
  std::string model;
  std::string workspace_root;
  std::string api_key_env;
  bool api_key_configured = false;
  std::string lsp_command;
  bool lsp_available = false;
  std::vector<UiNotice> notices;
};

}  // namespace yac::presentation
