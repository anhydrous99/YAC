#include "tool_call/web_fetch.hpp"

#include "tool_call/web_fetch_policy.hpp"
#include "tool_call/web_fetch_transform.hpp"
#include "util/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <utility>

namespace yac::tool_call {
namespace {

using Json = nlohmann::json;

struct CurlFetchState {
  const WebFetchTransportRequest* request = nullptr;
  std::stop_token stop_token;
  std::optional<std::string> callback_error;
};

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

[[nodiscard]] std::chrono::milliseconds ClampTimeout(int timeout_seconds) {
  const int clamped =
      std::clamp(timeout_seconds, 1, kWebFetchMaxTimeoutSeconds);
  return std::chrono::seconds(clamped);
}

[[nodiscard]] std::optional<std::size_t> ParseContentLength(
    std::string_view value) {
  value = ::yac::util::TrimSv(value);
  std::size_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(begin, end, parsed);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

[[nodiscard]] std::string NormalizeHeaderName(std::string_view name) {
  return ToLowerAscii(::yac::util::TrimSv(name));
}

[[nodiscard]] std::string SizeCapError() {
  return "response exceeded " + std::to_string(kWebFetchMaxBodyBytes) +
         " bytes";
}

[[nodiscard]] bool ContainsTimeout(std::string_view value) {
  const std::string lowered = ToLowerAscii(value);
  return lowered.find("timeout") != std::string::npos ||
         lowered.find("timedout") != std::string::npos ||
         lowered.find("timed out") != std::string::npos;
}

[[nodiscard]] std::runtime_error NormalizeTransportError(
    const std::exception& error) {
  const std::string message = error.what();
  if (ContainsTimeout(message)) {
    return std::runtime_error("web_fetch timeout");
  }
  return std::runtime_error(message);
}

void RequireHttpSuccess(long status_code) {
  if (status_code >= 200 && status_code < 300) {
    return;
  }
  std::ostringstream message;
  message << "web_fetch HTTP " << status_code;
  throw std::runtime_error(message.str());
}

struct ResponseCollector {
  std::string content_type;
  std::string body;

  void OnHeader(std::string_view line) {
    if (line.starts_with("HTTP/")) {
      content_type.clear();
      return;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      return;
    }

    const std::string name = NormalizeHeaderName(line.substr(0, colon));
    std::string value = ::yac::util::Trim(line.substr(colon + 1));
    if (name == "content-length") {
      const auto declared = ParseContentLength(value);
      if (declared.has_value() && *declared > kWebFetchMaxBodyBytes) {
        throw std::runtime_error(SizeCapError());
      }
    } else if (name == "content-type") {
      content_type = std::move(value);
    }
  }

  void OnBody(std::string_view chunk) {
    if (chunk.size() > kWebFetchMaxBodyBytes - body.size()) {
      throw std::runtime_error(SizeCapError());
    }
    body.append(chunk);
  }
};

[[nodiscard]] std::size_t CurlHeaderCallback(char* buffer, std::size_t size,
                                             std::size_t nitems,
                                             void* userdata) {
  const std::size_t bytes = size * nitems;
  auto* state = static_cast<CurlFetchState*>(userdata);
  if (state->request->on_header_line) {
    try {
      state->request->on_header_line(std::string_view(buffer, bytes));
    } catch (const std::exception& error) {
      state->callback_error = error.what();
      return 0;
    } catch (...) {
      state->callback_error = "HTTP header callback failed.";
      return 0;
    }
  }
  return bytes;
}

[[nodiscard]] std::size_t CurlBodyCallback(char* ptr, std::size_t size,
                                           std::size_t nmemb, void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* state = static_cast<CurlFetchState*>(userdata);
  if (state->request->on_body_chunk) {
    try {
      state->request->on_body_chunk(std::string_view(ptr, bytes));
    } catch (const std::exception& error) {
      state->callback_error = error.what();
      return 0;
    } catch (...) {
      state->callback_error = "HTTP body callback failed.";
      return 0;
    }
  }
  return bytes;
}

int CurlProgressCallback(void* clientp, curl_off_t download_total,
                         curl_off_t download_now, curl_off_t upload_total,
                         curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  const auto* state = static_cast<CurlFetchState*>(clientp);
  return state->stop_token.stop_requested() ? 1 : 0;
}

// Connect-time SSRF guard for CURLOPT_OPENSOCKETFUNCTION. curl invokes this
// with the exact peer address it is about to connect to, so validating here
// closes the DNS-rebinding window between the policy pre-check and the real
// connection. Returning CURL_SOCKET_BAD aborts the connect. Must stay a plain
// (non-capturing) free function to match curl's callback ABI.
curl_socket_t CurlOpenSocketGuard(void* clientp, curlsocktype purpose,
                                  struct curl_sockaddr* address) {
  (void)clientp;
  if (purpose != CURLSOCKTYPE_IPCXN ||
      IsPrivateSocketAddress(
          reinterpret_cast<const sockaddr*>(&address->addr))) {
    return CURL_SOCKET_BAD;
  }
  return ::socket(address->family, address->socktype, address->protocol);
}

}  // namespace

WebFetchTransportResponse CurlWebFetchTransport::Fetch(
    const WebFetchTransportRequest& request, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_fetch cancelled");
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }
  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: text/html, text/plain, */*");
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  CurlFetchState state{.request = &request, .stop_token = stop_token};
  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CurlHeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlBodyCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(request.timeout.count()));
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
  if (request.network_policy == WebFetchNetworkPolicy::RealNetwork) {
    // Validate the peer curl actually dials, eliminating the second-resolution
    // (DNS-rebinding) window the URL pre-check cannot cover. Redirects remain
    // disabled (no CURLOPT_FOLLOWLOCATION) so every connection is guarded.
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, CurlOpenSocketGuard);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, &state);
  }
  const CURLcode result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_fetch cancelled");
  }
  if (state.callback_error.has_value()) {
    throw std::runtime_error(*state.callback_error);
  }
  if (result != CURLE_OK) {
    if (result == CURLE_OPERATION_TIMEDOUT) {
      throw std::runtime_error("web_fetch timeout");
    }
    throw std::runtime_error(curl_easy_strerror(result));
  }

  WebFetchTransportResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  return response;
}

WebFetchResponse FetchWebUrl(const WebFetchRequest& request,
                             WebFetchTransport& transport,
                             std::stop_token stop_token) {
  const ParsedUrl parsed = ParseHttpUrl(request.url);
  EnforceUrlPolicy(parsed, request.network_policy);

  ResponseCollector collector;
  WebFetchTransportRequest transport_request{
      .url = request.url,
      .timeout = ClampTimeout(request.timeout),
      .on_header_line =
          [&collector](std::string_view line) { collector.OnHeader(line); },
      .on_body_chunk =
          [&collector](std::string_view chunk) { collector.OnBody(chunk); },
      .network_policy = request.network_policy};
  WebFetchTransportResponse transport_response;
  try {
    transport_response = transport.Fetch(transport_request, stop_token);
  } catch (const std::exception& error) {
    throw NormalizeTransportError(error);
  }
  RequireHttpSuccess(transport_response.status_code);
  collector.body = TransformWebFetchBody(collector.body, collector.content_type,
                                         request.format);
  return WebFetchResponse{.url = request.url,
                          .status_code = transport_response.status_code,
                          .content_type = std::move(collector.content_type),
                          .body = std::move(collector.body)};
}

ToolExecutionResult ExecuteWebFetchTool(const WebFetchRequest& request,
                                        WebFetchTransport& transport,
                                        std::stop_token stop_token) {
  try {
    WebFetchResponse response = FetchWebUrl(request, transport, stop_token);
    WebFetchCall call{
        .url = response.url,
        .title = "",
        .excerpt = response.body,
        .format = request.format,
        .timeout = std::clamp(request.timeout, 1, kWebFetchMaxTimeoutSeconds)};
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"url", response.url},
                            {"status_code", response.status_code},
                            {"content_type", response.content_type},
                            {"body", response.body}}
                           .dump()};
  } catch (const std::exception& error) {
    WebFetchCall call{
        .url = request.url,
        .format = request.format,
        .timeout = std::clamp(request.timeout, 1, kWebFetchMaxTimeoutSeconds)};
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"error", error.what()}}.dump(),
        .is_error = true};
  }
}

}  // namespace yac::tool_call
