#pragma once

#include "core_types/tool_call_types.hpp"

#include <chrono>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

inline constexpr int kWebSearchDefaultTimeoutSeconds = 25;
inline constexpr int kWebSearchMaxTimeoutSeconds = 120;
inline constexpr int kWebSearchDefaultResultLimit = 5;
inline constexpr int kWebSearchMaxResultLimit = 10;
inline constexpr int kWebSearchDefaultContextLimit = 4096;
inline constexpr int kWebSearchMaxContextLimit = 12000;

struct WebSearchProviderConfig {
  std::string endpoint;
  std::string api_key;
  int timeout_seconds = kWebSearchDefaultTimeoutSeconds;
  int result_limit = kWebSearchDefaultResultLimit;
  int context_limit = kWebSearchDefaultContextLimit;
};

struct WebSearchRequest {
  std::string query;
  int num_results = kWebSearchDefaultResultLimit;
  int context_max_characters = kWebSearchDefaultContextLimit;
};

struct WebSearchTransportRequest {
  std::string endpoint;
  std::string api_key;
  std::string body;
  std::chrono::milliseconds timeout{
      std::chrono::seconds(kWebSearchDefaultTimeoutSeconds)};
};

struct WebSearchTransportResponse {
  long status_code = 0;
  std::string content_type;
  std::string body;
};

class WebSearchTransport {
 public:
  WebSearchTransport() = default;
  WebSearchTransport(const WebSearchTransport&) = delete;
  WebSearchTransport& operator=(const WebSearchTransport&) = delete;
  WebSearchTransport(WebSearchTransport&&) = delete;
  WebSearchTransport& operator=(WebSearchTransport&&) = delete;
  virtual ~WebSearchTransport() = default;

  [[nodiscard]] virtual WebSearchTransportResponse Search(
      const WebSearchTransportRequest& request, std::stop_token stop_token) = 0;
};

class CurlWebSearchTransport final : public WebSearchTransport {
 public:
  [[nodiscard]] WebSearchTransportResponse Search(
      const WebSearchTransportRequest& request,
      std::stop_token stop_token) override;
};

struct WebSearchResponse {
  std::vector<SearchResult> results;
};

[[nodiscard]] std::string BuildExaWebSearchRequestBody(
    const WebSearchRequest& request);

[[nodiscard]] WebSearchResponse ParseExaWebSearchResponse(
    std::string_view body, std::string_view content_type = {});

[[nodiscard]] WebSearchResponse SearchWeb(const WebSearchRequest& request,
                                          const WebSearchProviderConfig& config,
                                          WebSearchTransport& transport,
                                          std::stop_token stop_token = {});

[[nodiscard]] ToolExecutionResult ExecuteWebSearchTool(
    const WebSearchRequest& request, const WebSearchProviderConfig& config,
    WebSearchTransport& transport, std::stop_token stop_token = {});

}  // namespace yac::tool_call
