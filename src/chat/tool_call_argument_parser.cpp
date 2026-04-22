#include "chat/tool_call_argument_parser.hpp"

#include <cstddef>

namespace yac::chat {

namespace {

constexpr std::size_t kMinHexDigitsForSurrogatePair = 4;

bool SkipWhitespace(std::string_view json, std::size_t& cursor) {
  while (cursor < json.size()) {
    const auto character = json[cursor];
    if (character != ' ' && character != '\t' && character != '\n' &&
        character != '\r') {
      return true;
    }
    ++cursor;
  }
  return false;
}

bool MatchesKey(std::string_view json, std::size_t cursor,
                std::string_view key) {
  std::size_t offset = 0;
  while (cursor < json.size() && offset < key.size()) {
    const auto character = json[cursor];
    if (character == '\\') {
      return false;
    }
    if (character != key[offset]) {
      return false;
    }
    ++cursor;
    ++offset;
  }
  if (offset != key.size()) {
    return false;
  }
  return cursor < json.size() && json[cursor] == '"';
}

void AppendHexCodepoint(unsigned code_point, std::string& out) {
  if (code_point < 0x80) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

bool ParseHex4(std::string_view json, std::size_t cursor, unsigned& out) {
  if (cursor + kMinHexDigitsForSurrogatePair > json.size()) {
    return false;
  }
  unsigned value = 0;
  for (std::size_t offset = 0; offset < kMinHexDigitsForSurrogatePair;
       ++offset) {
    const auto character = json[cursor + offset];
    value <<= 4;
    if (character >= '0' && character <= '9') {
      value |= static_cast<unsigned>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      value |= static_cast<unsigned>(character - 'a' + 10);
    } else if (character >= 'A' && character <= 'F') {
      value |= static_cast<unsigned>(character - 'A' + 10);
    } else {
      return false;
    }
  }
  out = value;
  return true;
}

// Scan forward from `cursor` for a top-level `"<key>"` and return the index
// just past its closing quote. Skips nested strings/objects/arrays so a key
// embedded inside a nested value is not falsely matched.
std::optional<std::size_t> FindKey(std::string_view json, std::size_t cursor,
                                   std::string_view key) {
  int depth = 0;
  while (cursor < json.size()) {
    const auto character = json[cursor];
    if (character == '{' || character == '[') {
      ++depth;
      ++cursor;
      continue;
    }
    if (character == '}' || character == ']') {
      if (depth == 0) {
        return std::nullopt;
      }
      --depth;
      ++cursor;
      continue;
    }
    if (character != '"') {
      ++cursor;
      continue;
    }

    const auto string_start = cursor + 1;
    if (depth == 1 && MatchesKey(json, string_start, key)) {
      const auto after_key_close = string_start + key.size() + 1;
      return after_key_close;
    }

    // Not our key — skip this string and continue scanning.
    std::size_t scan = string_start;
    while (scan < json.size() && json[scan] != '"') {
      if (json[scan] == '\\' && scan + 1 < json.size()) {
        scan += 2;
      } else {
        ++scan;
      }
    }
    if (scan >= json.size()) {
      return std::nullopt;
    }
    cursor = scan + 1;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> ExtractStringFieldPartial(
    std::string_view partial_json, std::string_view key) {
  auto after_key = FindKey(partial_json, 0, key);
  if (!after_key.has_value()) {
    return std::nullopt;
  }

  std::size_t cursor = *after_key;
  if (!SkipWhitespace(partial_json, cursor) || partial_json[cursor] != ':') {
    return std::nullopt;
  }
  ++cursor;
  if (!SkipWhitespace(partial_json, cursor) || partial_json[cursor] != '"') {
    return std::nullopt;
  }
  ++cursor;

  std::string out;
  out.reserve(partial_json.size() - cursor);
  while (cursor < partial_json.size()) {
    const auto character = partial_json[cursor];
    if (character == '"') {
      return out;
    }
    if (character != '\\') {
      out.push_back(character);
      ++cursor;
      continue;
    }

    if (cursor + 1 >= partial_json.size()) {
      return out;
    }
    const auto escape = partial_json[cursor + 1];
    switch (escape) {
      case '"':
        out.push_back('"');
        cursor += 2;
        continue;
      case '\\':
        out.push_back('\\');
        cursor += 2;
        continue;
      case '/':
        out.push_back('/');
        cursor += 2;
        continue;
      case 'b':
        out.push_back('\b');
        cursor += 2;
        continue;
      case 'f':
        out.push_back('\f');
        cursor += 2;
        continue;
      case 'n':
        out.push_back('\n');
        cursor += 2;
        continue;
      case 'r':
        out.push_back('\r');
        cursor += 2;
        continue;
      case 't':
        out.push_back('\t');
        cursor += 2;
        continue;
      case 'u': {
        unsigned code_point = 0;
        if (!ParseHex4(partial_json, cursor + 2, code_point)) {
          return out;
        }
        cursor += 6;
        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
          unsigned low = 0;
          if (cursor + 6 > partial_json.size() ||
              partial_json[cursor] != '\\' || partial_json[cursor + 1] != 'u' ||
              !ParseHex4(partial_json, cursor + 2, low) || low < 0xDC00 ||
              low > 0xDFFF) {
            return out;
          }
          code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
          cursor += 6;
        }
        AppendHexCodepoint(code_point, out);
        continue;
      }
      default:
        return out;
    }
  }
  return out;
}

}  // namespace yac::chat
