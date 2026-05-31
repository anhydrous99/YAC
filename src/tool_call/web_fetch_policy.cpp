#include "tool_call/web_fetch_policy.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yac::tool_call {
namespace {

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

[[nodiscard]] bool IsValidHostCharacter(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
         c == '.' || c == '_' || c == ':';
}

[[nodiscard]] bool IsPrivateIpv4HostOrder(std::uint32_t ip) {
  return (ip >> 24) == 10 || (ip >> 24) == 127 ||
         (ip >= 0xAC100000 && ip <= 0xAC1FFFFF) ||
         (ip >= 0xC0A80000 && ip <= 0xC0A8FFFF) ||
         (ip >= 0xA9FE0000 && ip <= 0xA9FEFFFF) ||
         (ip >= 0x64400000 && ip <= 0x647FFFFF) || ip <= 0x00FFFFFF;
}

[[nodiscard]] bool IsPrivateIpv4(const in_addr& address) {
  return IsPrivateIpv4HostOrder(ntohl(address.s_addr));
}

[[nodiscard]] bool IsPrivateIpv6(const in6_addr& address) {
  const auto& bytes = address.s6_addr;
  bool loopback_prefix = true;
  bool ipv4_mapped_prefix = true;
  for (std::size_t index = 0; index < 10; ++index) {
    loopback_prefix = loopback_prefix && bytes[index] == 0;
    ipv4_mapped_prefix = ipv4_mapped_prefix && bytes[index] == 0;
  }
  for (std::size_t index = 10; index < 15; ++index) {
    loopback_prefix = loopback_prefix && bytes[index] == 0;
  }
  const bool loopback = loopback_prefix && bytes[15] == 1;
  const bool ipv4_mapped =
      ipv4_mapped_prefix && bytes[10] == 0xff && bytes[11] == 0xff;
  const std::uint32_t mapped_ipv4 =
      (static_cast<std::uint32_t>(bytes[12]) << 24U) |
      (static_cast<std::uint32_t>(bytes[13]) << 16U) |
      (static_cast<std::uint32_t>(bytes[14]) << 8U) |
      static_cast<std::uint32_t>(bytes[15]);
  return loopback || (ipv4_mapped && IsPrivateIpv4HostOrder(mapped_ipv4)) ||
         (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
         (bytes[0] & 0xfe) == 0xfc;
}

[[nodiscard]] bool HostResolvesToPrivateAddress(const std::string& host) {
  in_addr ipv4{};
  if (inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
    return IsPrivateIpv4(ipv4);
  }
  in6_addr ipv6{};
  if (inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
    return IsPrivateIpv6(ipv6);
  }

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* raw_results = nullptr;
  const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &raw_results);
  if (rc != 0) {
    return false;
  }
  const auto cleanup = [](addrinfo* info) { freeaddrinfo(info); };
  std::unique_ptr<addrinfo, decltype(cleanup)> results(raw_results, cleanup);
  for (const addrinfo* current = results.get(); current != nullptr;
       current = current->ai_next) {
    if (current->ai_family == AF_INET) {
      const auto* addr = reinterpret_cast<const sockaddr_in*>(current->ai_addr);
      if (IsPrivateIpv4(addr->sin_addr)) {
        return true;
      }
    } else if (current->ai_family == AF_INET6) {
      const auto* addr =
          reinterpret_cast<const sockaddr_in6*>(current->ai_addr);
      if (IsPrivateIpv6(addr->sin6_addr)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

ParsedUrl ParseHttpUrl(std::string_view url) {
  constexpr std::string_view kSchemeDelimiter = "://";
  const std::size_t scheme_end = url.find(kSchemeDelimiter);
  if (scheme_end == std::string_view::npos || scheme_end == 0) {
    throw std::runtime_error("Malformed URL");
  }

  ParsedUrl parsed;
  parsed.scheme = ToLowerAscii(url.substr(0, scheme_end));
  if (parsed.scheme != "http" && parsed.scheme != "https") {
    throw std::runtime_error("only http and https URLs are supported");
  }

  const std::size_t authority_start = scheme_end + kSchemeDelimiter.size();
  const std::size_t authority_end = url.find_first_of("/?#", authority_start);
  std::string_view authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    throw std::runtime_error("Malformed URL");
  }

  if (authority.front() == '[') {
    const std::size_t closing = authority.find(']');
    if (closing == std::string_view::npos) {
      throw std::runtime_error("Malformed URL");
    }
    parsed.host = std::string(authority.substr(1, closing - 1));
    if (closing + 1 < authority.size() && authority[closing + 1] != ':') {
      throw std::runtime_error("Malformed URL");
    }
  } else {
    const std::size_t colon = authority.rfind(':');
    const bool has_single_colon =
        colon != std::string_view::npos && authority.find(':') == colon;
    parsed.host =
        std::string(has_single_colon ? authority.substr(0, colon) : authority);
  }

  if (parsed.host.empty() ||
      !std::ranges::all_of(parsed.host, IsValidHostCharacter)) {
    throw std::runtime_error("Malformed URL");
  }
  return parsed;
}

void EnforceUrlPolicy(const ParsedUrl& parsed, WebFetchNetworkPolicy policy) {
  if (policy != WebFetchNetworkPolicy::RealNetwork) {
    return;
  }
  if (HostResolvesToPrivateAddress(parsed.host)) {
    throw std::runtime_error("private network addresses are blocked");
  }
}

}  // namespace yac::tool_call
