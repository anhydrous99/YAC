#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace yac::provider::internal {

using HttpHeaderLineCallback = std::function<void(std::string_view line)>;
using HttpBodyChunkCallback = std::function<void(std::string_view chunk)>;

struct HttpStreamPostRequest {
  std::string url;
  std::string payload;
  std::vector<std::string> headers;
  std::optional<std::chrono::milliseconds> timeout;
  HttpHeaderLineCallback on_header_line;
  HttpBodyChunkCallback on_body_chunk;
};

struct HttpStreamPostResponse {
  long status_code = 0;
  bool cancelled = false;
};

[[nodiscard]] HttpStreamPostResponse PerformHttpStreamPost(
    const HttpStreamPostRequest& request, std::stop_token stop_token);

}  // namespace yac::provider::internal
