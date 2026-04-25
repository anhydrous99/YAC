#pragma once

#include "core_types/tool_call_types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

[[nodiscard]] std::optional<std::string> SimpleReplacer(
    std::string_view content, std::string_view old_string,
    std::string_view new_string);

[[nodiscard]] std::optional<std::string> LineTrimmedReplacer(
    std::string_view content, std::string_view old_string,
    std::string_view new_string);

[[nodiscard]] std::optional<std::string> WhitespaceNormalizedReplacer(
    std::string_view content, std::string_view old_string,
    std::string_view new_string);

[[nodiscard]] std::string ReplaceAll(std::string_view content,
                                     std::string_view old_string,
                                     std::string_view new_string);

[[nodiscard]] std::vector<DiffLine> ComputeDiff(std::string_view old_text,
                                                std::string_view new_text);

}  // namespace yac::tool_call
