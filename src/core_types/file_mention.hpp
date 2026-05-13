#pragma once

#include <cstdint>
#include <string>

namespace yac::tool_call {

// Workspace-relative file entry shared between FileIndex (producer in
// yac_service) and the file-mention menu render (consumer in
// yac_presentation). Kept in core_types so neither layer has to depend on
// the other.
struct FileMentionRow {
  std::string relative_path;
  std::uintmax_t size_bytes = 0;
};

}  // namespace yac::tool_call
