#pragma once

namespace yac::presentation::theme::testing {

// Clears the active theme state. Only for use in unit tests.
// Production code must NOT include this header.
void ResetThemeForTesting();

}  // namespace yac::presentation::theme::testing
