#pragma once

#include "chat/types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace yac::chat {

struct TextLine {
  std::string text;
  std::string newline;
};

[[nodiscard]] std::string WithThemeName(std::string_view content,
                                        std::string_view theme_name);

[[nodiscard]] std::optional<size_t> MatchingModelSettingsIndex(
    toml::table& table, const ProviderId& provider_id, const ModelId& model);

[[nodiscard]] std::optional<std::pair<size_t, size_t>> FindModelSettingsBlock(
    const std::vector<TextLine>& lines, size_t target_index);

[[nodiscard]] std::string WithProviderModelEffort(
    std::string_view content, const ProviderId& provider_id,
    const ModelId& model, std::optional<ReasoningEffort> effort,
    std::optional<size_t> matching_index, std::vector<ConfigIssue>& issues);

}  // namespace yac::chat
