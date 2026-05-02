#pragma once

#include <string>
#include <string_view>

namespace yac::provider {
class LanguageModelProvider;
}

namespace yac::app {

// Best-effort lookup of a model's context-window size in tokens. Returns 0 for
// unknown model identifiers; callers should render an "unknown" state in that
// case rather than assuming a default. Matches by literal id first, then by
// common prefix families.
[[nodiscard]] int LookupContextWindow(std::string_view model_id);

// Resolves a context-window size by chaining: provider-advertised value
// (`provider->GetContextWindow(model_id)` — which itself consults any
// discovered cache before any provider built-in table) → cross-provider
// `LookupContextWindow` table → 0. `provider` may be null, in which case
// only the cross-provider table is consulted.
[[nodiscard]] int ResolveContextWindow(
    const provider::LanguageModelProvider* provider,
    const std::string& model_id);

}  // namespace yac::app
