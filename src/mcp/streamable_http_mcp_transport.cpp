#include "mcp/streamable_http_mcp_transport.hpp"

#include "mcp/protocol_constants.hpp"
#include "mcp/sse_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <curl/curl.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::mcp {
namespace {

namespace pc = protocol;

constexpr auto kDefaultNotificationTimeout = std::chrono::seconds(10);

[[nodiscard]] std::string Trim(std::string value) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

[[nodiscard]] std::string ToLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

[[nodiscard]] std::string NormalizeContentType(std::string value) {
  const std::size_t semicolon_pos = value.find(';');
  if (semicolon_pos != std::string::npos) {
    value.erase(semicolon_pos);
  }
  return ToLower(Trim(std::move(value)));
}

int ProgressCallback(void* clientp, curl_off_t download_total,
                     curl_off_t download_now, curl_off_t upload_total,
                     curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;

  const auto* stop_token = static_cast<std::stop_token*>(clientp);
  return stop_token->stop_requested() ? 1 : 0;
}

}  // namespace

struct StreamableHttpMcpTransport::RequestState {
  std::int64_t expected_id = -1;
  std::string response_body;
  std::string content_type;
  std::optional<std::string> response_session_id;
  std::optional<std::string> error_message;
  SseParser sse_parser;
  Json matched_response;
  bool has_matched_response = false;
  StreamableHttpMcpTransport* owner = nullptr;
};

struct StreamableHttpMcpTransport::HttpResponse {
  long status_code = 0;
  std::string content_type;
  std::optional<std::string> session_id;
  Json payload;
};

std::size_t StreamableHttpMcpTransport::HeaderCallback(char* buffer,
                                                       std::size_t size,
                                                       std::size_t nitems,
                                                       void* userdata) {
  const std::size_t bytes = size * nitems;
  auto* state = static_cast<RequestState*>(userdata);
  std::string_view line(buffer, bytes);

  if (line.starts_with("HTTP/")) {
    state->content_type.clear();
    return bytes;
  }
  if (line == "\r\n") {
    return bytes;
  }

  const std::size_t colon_pos = line.find(':');
  if (colon_pos == std::string_view::npos) {
    return bytes;
  }

  const std::string header_name =
      ToLower(std::string(line.substr(0, colon_pos)));
  std::string header_value = Trim(std::string(line.substr(colon_pos + 1)));
  if (!header_value.empty() && header_value.back() == '\r') {
    header_value.pop_back();
  }

  if (header_name == ToLower(std::string(pc::kHeaderContentType))) {
    state->content_type = NormalizeContentType(std::move(header_value));
  } else if (header_name == ToLower(std::string(pc::kHeaderMcpSessionId))) {
    state->response_session_id = std::move(header_value);
  }

  return bytes;
}

std::size_t StreamableHttpMcpTransport::WriteCallback(char* ptr,
                                                      std::size_t size,
                                                      std::size_t nmemb,
                                                      void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* state = static_cast<RequestState*>(userdata);

  if (state->content_type == pc::kContentTypeEventStream) {
    const auto events =
        state->sse_parser.FeedChunk(std::string_view(ptr, bytes));
    for (const auto& event : events) {
      if (event.data.empty()) {
        continue;
      }

      Json message;
      try {
        message = Json::parse(event.data);
      } catch (const std::exception& error) {
        state->error_message =
            std::string("Invalid SSE JSON payload: ") + error.what();
        return 0;
      }

      if (message.contains(std::string(pc::kFieldMethod)) &&
          message[std::string(pc::kFieldMethod)].is_string() &&
          !message.contains(std::string(pc::kFieldId))) {
        const Json params = message.contains(std::string(pc::kFieldParams))
                                ? message[std::string(pc::kFieldParams)]
                                : Json::object();
        state->owner->DispatchNotification(
            message[std::string(pc::kFieldMethod)].get<std::string>(), params);
        continue;
      }

      if (message.contains(std::string(pc::kFieldId)) &&
          message[std::string(pc::kFieldId)].is_number_integer() &&
          message[std::string(pc::kFieldId)].get<std::int64_t>() ==
              state->expected_id) {
        state->matched_response = std::move(message);
        state->has_matched_response = true;
      }
    }
    return bytes;
  }

  state->response_body.append(ptr, bytes);
  return bytes;
}

StreamableHttpMcpTransport::StreamableHttpMcpTransport(McpServerConfig config)
    : config_(std::move(config)) {}

StreamableHttpMcpTransport::~StreamableHttpMcpTransport() {
  Stop(std::stop_token{});
}

void StreamableHttpMcpTransport::Start() {
  std::lock_guard lock(mutex_);
  if (status_ == TransportStatus::Ready ||
      status_ == TransportStatus::Starting) {
    return;
  }
  if (config_.url.empty()) {
    status_ = TransportStatus::Failed;
    throw std::runtime_error("MCP HTTP transport requires a URL");
  }
  status_ = TransportStatus::Ready;
}

void StreamableHttpMcpTransport::Stop(std::stop_token stop) {
  (void)stop;
  std::lock_guard lock(mutex_);
  session_id_.reset();
  status_ = TransportStatus::Stopped;
}

Json StreamableHttpMcpTransport::SendRequest(std::string_view method,
                                             const Json& params,
                                             std::chrono::milliseconds timeout,
                                             std::stop_token stop) {
  Json message = {
      {std::string(pc::kFieldJsonRpc), std::string(pc::kJsonRpcVersion)},
      {std::string(pc::kFieldId), next_request_id_.fetch_add(1)},
      {std::string(pc::kFieldMethod), method},
      {std::string(pc::kFieldParams), params}};
  return PerformJsonRpcRequest(message, timeout, stop, true);
}

void StreamableHttpMcpTransport::SendNotification(std::string_view method,
                                                  const Json& params) {
  Json message = {
      {std::string(pc::kFieldJsonRpc), std::string(pc::kJsonRpcVersion)},
      {std::string(pc::kFieldMethod), method},
      {std::string(pc::kFieldParams), params}};
  (void)PerformJsonRpcRequest(message, kDefaultNotificationTimeout,
                              std::stop_token{}, true);
}

void StreamableHttpMcpTransport::SetNotificationCallback(
    NotificationCallback callback) {
  std::lock_guard lock(mutex_);
  notification_callback_ = std::move(callback);
}

TransportStatus StreamableHttpMcpTransport::Status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

Json StreamableHttpMcpTransport::PerformJsonRpcRequest(
    const Json& message, std::chrono::milliseconds timeout,
    std::stop_token stop, bool allow_session_retry) {
  std::lock_guard request_lock(request_mutex_);
  HttpResponse response = PerformHttpRequest(message, timeout, stop);
  if (response.status_code == 404 && allow_session_retry && SessionId()) {
    {
      std::lock_guard lock(mutex_);
      status_ = TransportStatus::Reconnecting;
    }
    SetSessionId(std::nullopt);
    response = PerformHttpRequest(message, timeout, stop);
  }

  if (response.session_id.has_value()) {
    SetSessionId(response.session_id);
  }

  if (response.status_code >= 400) {
    std::ostringstream message_builder;
    message_builder << "MCP HTTP request failed with HTTP "
                    << response.status_code;
    throw std::runtime_error(message_builder.str());
  }

  {
    std::lock_guard lock(mutex_);
    status_ = TransportStatus::Ready;
  }
  return response.payload;
}

StreamableHttpMcpTransport::HttpResponse
StreamableHttpMcpTransport::PerformHttpRequest(
    const Json& message, std::chrono::milliseconds timeout,
    std::stop_token stop) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  RequestState state{.owner = this};
  if (message.contains(std::string(pc::kFieldId)) &&
      message[std::string(pc::kFieldId)].is_number_integer()) {
    state.expected_id = message[std::string(pc::kFieldId)].get<std::int64_t>();
  }

  const std::string payload = message.dump();

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers =
      curl_slist_append(headers, "Accept: application/json, text/event-stream");
  headers =
      curl_slist_append(headers, (std::string(pc::kHeaderMcpProtocolVersion) +
                                  ": " + std::string(pc::kMcpProtocolVersion))
                                     .c_str());
  if (const auto session_id = SessionId(); session_id.has_value()) {
    headers = curl_slist_append(
        headers,
        (std::string(pc::kHeaderMcpSessionId) + ": " + *session_id).c_str());
  }
  if (const auto auth_header = ResolveAuthorizationHeader();
      !auth_header.empty()) {
    headers = curl_slist_append(headers, auth_header.c_str());
  }
  for (const auto& [key, value] : config_.headers) {
    headers = curl_slist_append(headers, (key + ": " + value).c_str());
  }

  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  curl_easy_setopt(curl, CURLOPT_URL, config_.url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
                   &StreamableHttpMcpTransport::HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                   &StreamableHttpMcpTransport::WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(timeout.count()));
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop);

  const CURLcode result = curl_easy_perform(curl);
  if (stop.stop_requested()) {
    throw std::runtime_error("MCP HTTP request cancelled");
  }
  if (result != CURLE_OK) {
    if (state.error_message.has_value()) {
      throw std::runtime_error(*state.error_message);
    }
    throw std::runtime_error(curl_easy_strerror(result));
  }

  HttpResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  response.content_type = std::move(state.content_type);
  response.session_id = std::move(state.response_session_id);

  if (response.status_code == 404) {
    response.payload = Json::object();
    return response;
  }

  if (response.content_type == pc::kContentTypeEventStream) {
    if (state.expected_id >= 0 && !state.has_matched_response) {
      throw std::runtime_error("MCP SSE response missing matching JSON-RPC id");
    }
    response.payload = std::move(state.matched_response);
    return response;
  }

  if (state.response_body.empty()) {
    response.payload = Json::object();
    return response;
  }

  try {
    response.payload = Json::parse(state.response_body);
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string("Invalid JSON response: ") +
                             error.what());
  }
  return response;
}

std::string StreamableHttpMcpTransport::ResolveAuthorizationHeader() const {
  if (!config_.auth.has_value()) {
    return {};
  }
  if (const auto* bearer = std::get_if<McpAuthBearer>(&*config_.auth)) {
    if (const char* env = std::getenv(bearer->api_key_env.c_str())) {
      return std::string(pc::kHeaderAuthorization) + ": Bearer " + env;
    }
    throw std::runtime_error("Missing MCP bearer token env: " +
                             bearer->api_key_env);
  }
  return {};
}

std::optional<std::string> StreamableHttpMcpTransport::SessionId() const {
  std::lock_guard lock(mutex_);
  return session_id_;
}

void StreamableHttpMcpTransport::SetSessionId(
    std::optional<std::string> session_id) {
  std::lock_guard lock(mutex_);
  session_id_ = std::move(session_id);
}

void StreamableHttpMcpTransport::DispatchNotification(
    std::string_view method, const Json& params) const {
  NotificationCallback callback;
  {
    std::lock_guard lock(mutex_);
    callback = notification_callback_;
  }
  if (callback) {
    callback(method, params);
  }
}

}  // namespace yac::mcp
