#include "mcp/oauth/pkce.hpp"

#include <algorithm>
#include <array>
#include <openssl/sha.h>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace yac::mcp::oauth::test {
namespace {

constexpr std::string_view kBase64UrlAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] std::string Base64UrlEncode(std::string_view bytes) {
  std::string encoded;
  encoded.reserve(((bytes.size() + 2) / 3) * 4);

  for (std::size_t index = 0; index < bytes.size(); index += 3) {
    const auto first = static_cast<unsigned char>(bytes[index]);
    const auto second = index + 1 < bytes.size()
                            ? static_cast<unsigned char>(bytes[index + 1])
                            : 0U;
    const auto third = index + 2 < bytes.size()
                           ? static_cast<unsigned char>(bytes[index + 2])
                           : 0U;
    const unsigned int block = (static_cast<unsigned int>(first) << 16U) |
                               (static_cast<unsigned int>(second) << 8U) |
                               static_cast<unsigned int>(third);

    encoded.push_back(kBase64UrlAlphabet.at((block >> 18U) & 0x3FU));
    encoded.push_back(kBase64UrlAlphabet.at((block >> 12U) & 0x3FU));
    if (index + 1 < bytes.size()) {
      encoded.push_back(kBase64UrlAlphabet.at((block >> 6U) & 0x3FU));
    }
    if (index + 2 < bytes.size()) {
      encoded.push_back(kBase64UrlAlphabet.at(block & 0x3FU));
    }
  }

  return encoded;
}

[[nodiscard]] std::string ExpectedChallenge(std::string_view verifier) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(verifier.data()),
         verifier.size(), digest.data());
  return Base64UrlEncode(std::string_view(
      reinterpret_cast<const char*>(digest.data()), digest.size()));
}

[[nodiscard]] bool IsUrlSafe(std::string_view value) {
  return std::ranges::all_of(value, [](const char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' ||
           ch == '~';
  });
}

}  // namespace

TEST_CASE("pair_property") {
  for (int index = 0; index < 100; ++index) {
    const std::string verifier = GenerateCodeVerifier();
    const std::string challenge = DeriveCodeChallenge(verifier);

    REQUIRE(verifier.size() == 43);
    REQUIRE(challenge == ExpectedChallenge(verifier));
    REQUIRE(IsUrlSafe(verifier));
    REQUIRE(IsUrlSafe(challenge));
  }
}

}  // namespace yac::mcp::oauth::test
