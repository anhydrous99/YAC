#pragma once

#include "core_types/tool_call_types.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace yac::tool_call {

inline constexpr std::size_t kWebFetchMaxBodyBytes =
    std::size_t{5} * 1024 * 1024;
inline constexpr int kWebFetchDefaultTimeoutSeconds = 30;
inline constexpr int kWebFetchMaxTimeoutSeconds = 120;

using WebFetchHeaderCallback = std::function<void(std::string_view line)>;
using WebFetchBodyCallback = std::function<void(std::string_view chunk)>;

enum class WebFetchNetworkPolicy {
  RealNetwork,
  InjectedTransport,
};

struct WebFetchTransportRequest {
  std::string url;
  std::chrono::milliseconds timeout{
      std::chrono::seconds(kWebFetchDefaultTimeoutSeconds)};
  WebFetchHeaderCallback on_header_line;
  WebFetchBodyCallback on_body_chunk;
  // When RealNetwork, the curl transport installs a connect-time guard that
  // refuses to dial private/loopback/link-local peers (SSRF defense). Test and
  // injected transports ignore this field.
  WebFetchNetworkPolicy network_policy = WebFetchNetworkPolicy::RealNetwork;
};

struct WebFetchTransportResponse {
  long status_code = 0;
};

class WebFetchTransport {
 public:
  WebFetchTransport() = default;
  WebFetchTransport(const WebFetchTransport&) = delete;
  WebFetchTransport& operator=(const WebFetchTransport&) = delete;
  WebFetchTransport(WebFetchTransport&&) = delete;
  WebFetchTransport& operator=(WebFetchTransport&&) = delete;
  virtual ~WebFetchTransport() = default;

  [[nodiscard]] virtual WebFetchTransportResponse Fetch(
      const WebFetchTransportRequest& request, std::stop_token stop_token) = 0;
};

class CurlWebFetchTransport final : public WebFetchTransport {
 public:
  [[nodiscard]] WebFetchTransportResponse Fetch(
      const WebFetchTransportRequest& request,
      std::stop_token stop_token) override;
};

struct WebFetchRequest {
  std::string url;
  std::string format = "markdown";
  int timeout = kWebFetchDefaultTimeoutSeconds;
  WebFetchNetworkPolicy network_policy = WebFetchNetworkPolicy::RealNetwork;
};

struct WebFetchResponse {
  std::string url;
  long status_code = 0;
  std::string content_type;
  std::string body;
};

[[nodiscard]] WebFetchResponse FetchWebUrl(const WebFetchRequest& request,
                                           WebFetchTransport& transport,
                                           std::stop_token stop_token = {});

[[nodiscard]] ToolExecutionResult ExecuteWebFetchTool(
    const WebFetchRequest& request, WebFetchTransport& transport,
    std::stop_token stop_token = {});

}  // namespace yac::tool_call
