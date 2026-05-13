#pragma once

#include "core_types/file_mention.hpp"
#include "ftxui/dom/elements.hpp"

#include <vector>

namespace yac::presentation {

// `indexing=true` swaps the empty-rows placeholder from "No matching files"
// to "Indexing workspace…", letting the caller distinguish a cold/warming
// FileIndex from a query that genuinely matched nothing.
ftxui::Element RenderFileMentionMenu(
    const std::vector<tool_call::FileMentionRow>& rows, int selected_index,
    int max_width, bool indexing = false);

}  // namespace yac::presentation
