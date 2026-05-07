#include "chat/config_loader.hpp"

#include "chat/config.hpp"
#include "chat/config_paths.hpp"
#include "chat/prompt_library.hpp"

#include <exception>
#include <filesystem>

namespace yac::chat {

LoadConfigResult LoadConfig(const std::filesystem::path& settings_path,
                            const std::filesystem::path& prompts_dir) {
  LoadConfigResult result;
  result.chat = LoadChatConfigResultFrom(settings_path,
                                         /*create_if_missing=*/true);
  result.prompt_library = LoadPromptLibrary(prompts_dir,
                                            /*seed_defaults=*/true);
  return result;
}

LoadConfigResult LoadConfig() {
  LoadConfigResult result;
  result.chat = LoadChatConfigResult();
  try {
    result.prompt_library =
        LoadPromptLibrary(GetPromptsDir(), /*seed_defaults=*/true);
  } catch (const std::exception& error) {
    result.prompt_library.issues.push_back(
        {.severity = ConfigIssueSeverity::Warning,
         .message = "Could not locate ~/.yac/prompts",
         .detail = error.what()});
  }
  return result;
}

}  // namespace yac::chat
