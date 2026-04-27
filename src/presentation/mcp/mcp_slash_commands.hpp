#pragma once

#include "presentation/command_palette.hpp"
#include "presentation/slash_command_registry.hpp"

#include <vector>

namespace yac::presentation {

void RegisterMcpSlashCommands(SlashCommandRegistry& registry);

[[nodiscard]] std::vector<Command> BuildMcpPaletteCommands();

}  // namespace yac::presentation
