#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr size_t kMaxContentPreviewBytes = 4000;

std::optional<size_t> ReadableFileSize(const std::filesystem::path& path) {
  std::error_code ec;
  const auto sym_status = std::filesystem::symlink_status(path, ec);
  if (!ec && std::filesystem::is_symlink(sym_status)) {
    throw std::runtime_error("Refusing to read through symlink: " +
                             path.string());
  }
  if (!std::filesystem::exists(path, ec)) {
    return std::nullopt;
  }
  if (ec) {
    throw std::runtime_error("Unable to stat file: " + path.string());
  }
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    throw std::runtime_error("Unable to size file: " + path.string());
  }
  if (size > kMaxFileBytes) {
    throw std::runtime_error("File exceeds " + std::to_string(kMaxFileBytes) +
                             " byte read limit: " + path.string());
  }
  return static_cast<size_t>(size);
}

std::ifstream OpenFileForRead(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to open file for reading: " +
                             path.string());
  }
  return file;
}

}  // namespace

WorkspaceFilesystem::WorkspaceFilesystem(std::filesystem::path workspace_root)
    : workspace_root_(std::filesystem::absolute(std::move(workspace_root))
                          .lexically_normal()) {}

const std::filesystem::path& WorkspaceFilesystem::Root() const {
  return workspace_root_;
}

std::filesystem::path WorkspaceFilesystem::ResolvePath(
    const std::string& path) const {
  std::filesystem::path candidate(path);
  if (candidate.is_relative()) {
    candidate = workspace_root_ / candidate;
  }
  candidate = std::filesystem::absolute(candidate).lexically_normal();

  // Cheap string-prefix gate: rejects ../ escape attempts without touching
  // the filesystem.
  const auto root = workspace_root_.string();
  const auto value = candidate.string();
  const auto with_separator =
      root.back() == std::filesystem::path::preferred_separator
          ? root
          : root + std::filesystem::path::preferred_separator;
  if (value != root && !value.starts_with(with_separator)) {
    throw std::runtime_error("Path is outside the workspace: " + path);
  }

  // Symlink-aware gate: resolves any symlinks that already exist on the
  // candidate path and rejects the request if the resolved path leaves
  // workspace_root_. lexically_normal alone does not catch this — a symlink
  // planted inside the workspace that points at /etc/passwd would pass the
  // string check above but escape at open-time.
  std::error_code ec;
  const auto real_candidate = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    throw std::runtime_error("Unable to resolve path: " + path);
  }
  const auto real_root = std::filesystem::weakly_canonical(workspace_root_, ec);
  if (ec) {
    throw std::runtime_error("Unable to resolve workspace root: " +
                             workspace_root_.string());
  }
  const auto real_root_str = real_root.string();
  const auto real_value_str = real_candidate.string();
  const auto real_with_separator =
      !real_root_str.empty() &&
              real_root_str.back() == std::filesystem::path::preferred_separator
          ? real_root_str
          : real_root_str + std::filesystem::path::preferred_separator;
  if (real_value_str != real_root_str &&
      !real_value_str.starts_with(real_with_separator)) {
    throw std::runtime_error(
        "Path resolves outside the workspace via symlink: " + path);
  }

  return candidate;
}

std::string WorkspaceFilesystem::DisplayPath(
    const std::filesystem::path& path) const {
  auto normalized = std::filesystem::absolute(path).lexically_normal();
  std::error_code error;
  auto relative = std::filesystem::relative(normalized, workspace_root_, error);
  if (!error && !relative.empty() && !relative.native().starts_with("..")) {
    return relative.string();
  }
  return normalized.string();
}

std::string WorkspaceFilesystem::ReadFile(const std::filesystem::path& path) {
  if (!ReadableFileSize(path).has_value()) {
    return {};
  }
  auto file = OpenFileForRead(path);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string WorkspaceFilesystem::ReadFilePrefix(
    const std::filesystem::path& path, std::size_t max_bytes) {
  const auto size = ReadableFileSize(path);
  if (!size.has_value()) {
    return {};
  }
  if (max_bytes == 0) {
    return {};
  }
  auto file = OpenFileForRead(path);
  const auto to_read = std::min(max_bytes, *size);
  std::string buffer;
  buffer.resize(to_read);
  if (to_read > 0) {
    file.read(buffer.data(), static_cast<std::streamsize>(to_read));
    buffer.resize(static_cast<std::size_t>(file.gcount()));
  }
  return buffer;
}

void WorkspaceFilesystem::WriteFile(const std::filesystem::path& path,
                                    const std::string& content) {
  if (content.size() > kMaxFileBytes) {
    throw std::runtime_error("Content exceeds " +
                             std::to_string(kMaxFileBytes) +
                             " byte write limit: " + path.string());
  }
  std::error_code sym_ec;
  const auto sym_status = std::filesystem::symlink_status(path, sym_ec);
  if (!sym_ec && std::filesystem::is_symlink(sym_status)) {
    throw std::runtime_error("Refusing to write through symlink: " +
                             path.string());
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Unable to open file for writing: " +
                             path.string());
  }
  file << content;
  file.flush();
  if (!file) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
}

int CountLines(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
         (text.back() == '\n' ? 0 : 1);
}

std::string PreviewText(const std::string& text) {
  if (text.size() <= kMaxContentPreviewBytes) {
    return text;
  }
  return text.substr(0, kMaxContentPreviewBytes) + "\n...";
}

std::string TailLines(const std::string& text, size_t max_lines) {
  if (max_lines == 0 || text.empty()) {
    return "";
  }
  std::vector<std::string> collected;
  collected.reserve(max_lines);
  size_t end = text.size();
  while (end > 0 && collected.size() < max_lines) {
    const size_t newline = text.rfind('\n', end - 1);
    const size_t start = (newline == std::string::npos) ? 0 : newline + 1;
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      collected.push_back(std::move(line));
    }
    if (newline == std::string::npos) {
      break;
    }
    end = newline;
  }
  std::ranges::reverse(collected);
  std::string result;
  for (const auto& line : collected) {
    if (!result.empty()) {
      result.push_back('\n');
    }
    result.append(line);
  }
  return result;
}

size_t OffsetForLineCharacter(const std::string& text, int line,
                              int character) {
  const int target_line = std::max(1, line);
  const int target_character = std::max(1, character);
  size_t offset = 0;
  for (int current_line = 1; current_line < target_line && offset < text.size();
       ++current_line) {
    auto newline = text.find('\n', offset);
    if (newline == std::string::npos) {
      return text.size();
    }
    offset = newline + 1;
  }
  // LSP `character` counts UTF-16 code units (the protocol default, and the
  // encoding the LSP client negotiates). Walk the line's UTF-8 bytes, advancing
  // one code unit per 1-3 byte sequence and two per 4-byte (astral) sequence,
  // so the byte offset stays correct when earlier columns hold multibyte
  // characters. ASCII advances one byte per code unit, matching the previous
  // raw-byte behavior. The walk stops at the line's newline or EOF.
  auto remaining_units = static_cast<size_t>(target_character - 1);
  size_t cursor = offset;
  while (remaining_units > 0 && cursor < text.size()) {
    const auto lead = static_cast<unsigned char>(text[cursor]);
    if (lead == '\n') {
      break;
    }
    size_t byte_len = 1;
    size_t code_units = 1;
    if (lead >= 0xF0) {
      byte_len = 4;
      code_units = 2;  // encoded as a surrogate pair in UTF-16
    } else if (lead >= 0xE0) {
      byte_len = 3;
    } else if (lead >= 0xC0) {
      byte_len = 2;
    }
    byte_len = std::min(byte_len, text.size() - cursor);
    cursor += byte_len;
    remaining_units -= std::min(code_units, remaining_units);
  }
  return std::min(text.size(), cursor);
}

}  // namespace yac::tool_call
