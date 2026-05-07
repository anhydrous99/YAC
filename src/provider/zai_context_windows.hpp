#pragma once

#include <string_view>

namespace yac::provider {

// Built-in context-window table for the Z.ai Coding API preset (`zai`). Z.ai
// does not advertise a `/models` context-window field, so the provider keeps
// its own table for `glm-*` models. Returns 0 for unknown model ids; callers
// should fall through to the cross-provider table in
// `provider/model_context_windows.cpp`. Values mirror the glm-* entries there
// to keep the two sources consistent (asserted by tests).
[[nodiscard]] int KnownZaiContextWindow(std::string_view model_id);

}  // namespace yac::provider
