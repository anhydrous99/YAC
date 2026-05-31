#include "provider/openai_auth_jwt.hpp"

#include <exception>

namespace yac::provider {
namespace {

[[nodiscard]] int DecodeBase64UrlChar(unsigned char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '-') {
    return 62;
  }
  if (value == '_') {
    return 63;
  }
  return -1;
}

}  // namespace

std::optional<std::string> Base64UrlDecode(std::string_view input) {
  std::string decoded;
  decoded.reserve((input.size() * 3) / 4);
  int buffer = 0;
  int bits = 0;
  for (const unsigned char value : input) {
    if (value == '=') {
      break;
    }
    const int index = DecodeBase64UrlChar(value);
    if (index < 0) {
      return std::nullopt;
    }
    buffer = (buffer << 6) | index;
    bits += 6;
    while (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return decoded;
}

std::optional<nlohmann::json> ParseJwtPayload(std::string_view token) {
  const std::size_t first_dot = token.find('.');
  if (first_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto payload =
      Base64UrlDecode(token.substr(first_dot + 1, second_dot - first_dot - 1));
  if (!payload.has_value()) {
    return std::nullopt;
  }
  try {
    return nlohmann::json::parse(*payload);
  } catch (const std::exception& error) {
    (void)error;
    return std::nullopt;
  }
}

std::optional<std::string> ExtractAccountIdFromPayload(
    const nlohmann::json& payload) {
  if (const auto it = payload.find("chatgpt_account_id");
      it != payload.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (const auto claims = payload.find("https://api.openai.com/auth");
      claims != payload.end() && claims->is_object()) {
    if (const auto account_id = claims->find("chatgpt_account_id");
        account_id != claims->end() && account_id->is_string()) {
      return account_id->get<std::string>();
    }
  }
  if (const auto it =
          payload.find("https://api.openai.com/auth.chatgpt_account_id");
      it != payload.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (const auto orgs = payload.find("organizations");
      orgs != payload.end() && orgs->is_array() && !orgs->empty() &&
      (*orgs)[0].is_object()) {
    if (const auto org_id = (*orgs)[0].find("id");
        org_id != (*orgs)[0].end() && org_id->is_string()) {
      return org_id->get<std::string>();
    }
  }
  return std::nullopt;
}

std::optional<std::string> ExtractAccountId(
    const std::optional<std::string>& id_token, std::string_view access_token) {
  if (id_token.has_value()) {
    if (const auto id_payload = ParseJwtPayload(*id_token);
        id_payload.has_value()) {
      if (const auto account_id = ExtractAccountIdFromPayload(*id_payload);
          account_id.has_value()) {
        return account_id;
      }
    }
  }
  if (const auto access_payload = ParseJwtPayload(access_token);
      access_payload.has_value()) {
    return ExtractAccountIdFromPayload(*access_payload);
  }
  return std::nullopt;
}

}  // namespace yac::provider
