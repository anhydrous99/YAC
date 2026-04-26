#include "mcp/oauth/pkce.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <openssl/sha.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp::oauth {
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

[[nodiscard]] std::string ReadRandomBytes(std::size_t byte_count) {
  std::ifstream random_stream("/dev/urandom", std::ios::binary);
  if (!random_stream) {
    throw std::runtime_error("Failed to open /dev/urandom");
  }

  std::vector<char> bytes(byte_count);
  random_stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (random_stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    throw std::runtime_error("Failed to read /dev/urandom");
  }

  return {bytes.begin(), bytes.end()};
}

}  // namespace

std::string GenerateCodeVerifier() {
  return Base64UrlEncode(ReadRandomBytes(32));
}

std::string DeriveCodeChallenge(std::string_view verifier) {
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(reinterpret_cast<const unsigned char*>(verifier.data()),
         verifier.size(), digest.data());
  return Base64UrlEncode(std::string_view(
      reinterpret_cast<const char*>(digest.data()), digest.size()));
}

}  // namespace yac::mcp::oauth
