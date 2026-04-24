#include "chat/settings_toml.hpp"

#include "chat/settings_toml_template.hpp"

#include <exception>
#include <fstream>
#include <string>
#include <system_error>
#include <toml++/toml.hpp>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yac::chat {

namespace {

constexpr double kMinTemperature = 0.0;
constexpr double kMaxTemperature = 2.0;

void AddError(std::vector<ConfigIssue>& issues, std::string message,
              std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Error,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

void AddWarning(std::vector<ConfigIssue>& issues, std::string message,
                std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Warning,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

// Reads a string key from a TOML node; emits an error if the node is the
// wrong type. Returns true when a value was applied.
bool ApplyStringField(const toml::node_view<toml::node>& node,
                      const std::string& key, std::string& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_string()) {
    target = value->get();
    return true;
  }
  AddError(issues, "Invalid type for " + key + " in settings.toml",
           "Expected a string.");
  return false;
}

bool ApplyTemperature(const toml::node_view<toml::node>& node, double& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  double parsed = 0.0;
  if (auto* value = node.as_floating_point()) {
    parsed = value->get();
  } else if (auto* value = node.as_integer()) {
    parsed = static_cast<double>(value->get());
  } else {
    AddError(issues, "Invalid type for temperature in settings.toml",
             "Expected a number between 0.0 and 2.0.");
    return false;
  }
  if (parsed < kMinTemperature || parsed > kMaxTemperature) {
    AddError(issues, "Invalid temperature in settings.toml",
             "Value must be between 0.0 and 2.0.");
    return false;
  }
  target = parsed;
  return true;
}

bool ApplyBoolField(const toml::node_view<toml::node>& node,
                    const std::string& key, bool& target,
                    std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_boolean()) {
    target = value->get();
    return true;
  }
  AddError(issues, "Invalid type for " + key + " in settings.toml",
           "Expected a boolean (true or false).");
  return false;
}

bool ApplyStringArray(const toml::node_view<toml::node>& node,
                      const std::string& key, std::vector<std::string>& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* array = node.as_array();
  if (array == nullptr) {
    AddError(issues, "Invalid type for " + key + " in settings.toml",
             "Expected an array of strings.");
    return false;
  }
  std::vector<std::string> values;
  values.reserve(array->size());
  for (const auto& element : *array) {
    if (const auto* string_value = element.as_string()) {
      values.push_back(string_value->get());
    } else {
      AddError(issues, "Invalid element in " + key,
               "Every entry must be a string.");
      return false;
    }
  }
  target = std::move(values);
  return true;
}

}  // namespace

ChatConfigFieldSet LoadSettingsFromToml(const std::filesystem::path& path,
                                        ChatConfig& config,
                                        std::vector<ConfigIssue>& issues) {
  ChatConfigFieldSet fields;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return fields;
  }

  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    AddError(issues, "Failed to parse settings.toml",
             std::string(error.description()));
    return fields;
  } catch (const std::exception& error) {
    AddError(issues, "Failed to read settings.toml", error.what());
    return fields;
  }

  fields.temperature =
      ApplyTemperature(table["temperature"], config.temperature, issues);

  if (auto* system_prompt = table["system_prompt"].as_string()) {
    config.system_prompt = system_prompt->get();
    fields.system_prompt = true;
  } else if (table.contains("system_prompt")) {
    AddError(issues, "Invalid type for system_prompt in settings.toml",
             "Expected a string.");
  }

  fields.workspace_root = ApplyStringField(
      table["workspace_root"], "workspace_root", config.workspace_root, issues);

  const auto provider = table["provider"];
  if (provider.is_table()) {
    fields.provider_id = ApplyStringField(provider["id"], "provider.id",
                                          config.provider_id, issues);
    fields.model = ApplyStringField(provider["model"], "provider.model",
                                    config.model, issues);
    fields.base_url = ApplyStringField(
        provider["base_url"], "provider.base_url", config.base_url, issues);
    fields.api_key_env =
        ApplyStringField(provider["api_key_env"], "provider.api_key_env",
                         config.api_key_env, issues);
    fields.api_key = ApplyStringField(provider["api_key"], "provider.api_key",
                                      config.api_key, issues);
  } else if (table.contains("provider")) {
    AddError(issues, "Invalid type for [provider] in settings.toml",
             "Expected a table.");
  }

  const auto clangd = table["lsp"]["clangd"];
  if (clangd.is_table()) {
    fields.lsp_clangd_command =
        ApplyStringField(clangd["command"], "lsp.clangd.command",
                         config.lsp_clangd_command, issues);
    fields.lsp_clangd_args = ApplyStringArray(clangd["args"], "lsp.clangd.args",
                                              config.lsp_clangd_args, issues);
  } else if (table.contains("lsp")) {
    if (const auto lsp = table["lsp"]; !lsp.is_table()) {
      AddError(issues, "Invalid type for [lsp] in settings.toml",
               "Expected a table.");
    }
  }

  const auto theme_section = table["theme"];
  if (theme_section.is_table()) {
    ApplyBoolField(theme_section["sync_terminal_background"],
                   "theme.sync_terminal_background",
                   config.sync_terminal_background, issues);
    if (ApplyStringField(theme_section["name"], "theme.name", config.theme_name,
                         issues)) {
      fields.theme_name = true;
      if (config.theme_name.empty()) {
        AddWarning(issues, "theme.name is empty in settings.toml",
                   "Falling back to default theme 'opencode'.");
        config.theme_name = "opencode";
        fields.theme_name = false;
      }
    }
    if (ApplyStringField(theme_section["density"], "theme.density",
                         config.theme_density, issues)) {
      fields.theme_density = true;
      if (config.theme_density != "compact" &&
          config.theme_density != "comfortable") {
        AddWarning(issues, "Unknown theme.density in settings.toml",
                   "Falling back to default theme density 'comfortable'.");
        config.theme_density = "comfortable";
      }
    }
  } else if (table.contains("theme")) {
    AddError(issues, "Invalid type for [theme] in settings.toml",
             "Expected a table.");
  }

  return fields;
}

void WriteDefaultSettingsToml(const std::filesystem::path& path,
                              std::vector<ConfigIssue>& issues) {
  std::error_code ec;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      AddWarning(issues, "Failed to create " + parent.string(), ec.message());
      return;
    }
#ifndef _WIN32
    std::filesystem::permissions(parent, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) {
      AddWarning(issues, "Failed to set permissions on " + parent.string(),
                 ec.message());
      ec.clear();
    }
#endif
  }

#ifndef _WIN32
  // Create atomically with 0600 so the file never exists with umask-default
  // permissions. O_EXCL also means we will not clobber a file created by a
  // racing process between the caller's existence check and this call.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0) {
    if (errno == EEXIST) {
      // Another writer beat us to it; the caller's "create if missing"
      // semantic is satisfied, so treat as success.
      return;
    }
    AddWarning(issues, "Failed to create " + path.string(),
               std::strerror(errno));
    return;
  }
  const std::string_view body = kDefaultSettingsToml;
  size_t written = 0;
  while (written < body.size()) {
    const auto bytes =
        ::write(fd, body.data() + written, body.size() - written);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      const auto err = std::string(std::strerror(errno));
      ::close(fd);
      AddWarning(issues, "Failed to write " + path.string(), err);
      return;
    }
    written += static_cast<size_t>(bytes);
  }
  if (::close(fd) != 0) {
    AddWarning(issues, "Failed to close " + path.string(),
               std::strerror(errno));
  }
#else
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    AddWarning(issues, "Failed to create " + path.string(),
               "YAC will continue with built-in defaults.");
    return;
  }
  output << kDefaultSettingsToml;
  output.close();
  if (!output) {
    AddWarning(issues, "Failed to write " + path.string(),
               "YAC will continue with built-in defaults.");
    return;
  }
#endif
}

}  // namespace yac::chat
