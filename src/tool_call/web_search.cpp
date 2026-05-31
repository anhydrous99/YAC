#include "tool_call/web_search.hpp"

#include "tool_call/web_secret_redaction.hpp"
#include "util/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yac::tool_call {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kInvalidResponseError =
    "web_search provider returned invalid response";
constexpr std::string_view kTimeoutError = "web_search request timed out";

struct CurlSearchState {
  std::string content_type;
  std::string body;
  std::stop_token stop_token;
};

struct SseEventDataParser {
  std::string buffer;
  std::string pending_data;
  bool has_pending_data = false;
  std::vector<std::string> events;

  void Feed(std::string_view chunk) {
    buffer.append(chunk.data(), chunk.size());
    std::size_t newline_pos = 0;
    while ((newline_pos = buffer.find('\n')) != std::string::npos) {
      std::string line = buffer.substr(0, newline_pos);
      buffer.erase(0, newline_pos + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      ConsumeLine(line);
    }
  }

  void Finish() {
    if (!buffer.empty()) {
      ConsumeLine(buffer);
      buffer.clear();
    }
    Flush();
  }

  void ConsumeLine(std::string_view line) {
    if (line.empty()) {
      Flush();
      return;
    }
    if (line.starts_with(':')) {
      return;
    }
    const std::size_t colon_pos = line.find(':');
    const std::string_view field = line.substr(0, colon_pos);
    std::string_view value;
    if (colon_pos != std::string_view::npos) {
      value = line.substr(colon_pos + 1);
      if (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }
    }
    if (field != "data") {
      return;
    }
    if (!pending_data.empty()) {
      pending_data.push_back('\n');
    }
    pending_data.append(value.data(), value.size());
    has_pending_data = true;
  }

  void Flush() {
    if (!has_pending_data) {
      return;
    }
    events.push_back(std::move(pending_data));
    pending_data.clear();
    has_pending_data = false;
  }
};

[[nodiscard]] std::string ToLowerAscii(std::string_view value) {
  std::string lowered(value);
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lowered;
}

[[nodiscard]] bool ContainsTimeout(std::string_view value) {
  const std::string lowered = ToLowerAscii(value);
  return lowered.find("timeout") != std::string::npos ||
         lowered.find("timedout") != std::string::npos ||
         lowered.find("timed out") != std::string::npos;
}

[[nodiscard]] std::string RedactProviderError(
    std::string_view message, const WebSearchProviderConfig& config) {
  return RedactWebSecrets(
      message, WebSecretRedactionConfig{
                   .api_key_like_substrings = {config.api_key,
                                               "Bearer " + config.api_key}});
}

[[nodiscard]] int ClampResultLimit(int num_results) {
  return std::clamp(num_results, 1, kWebSearchMaxResultLimit);
}

[[nodiscard]] int ClampContextLimit(int context_max_characters) {
  return std::clamp(context_max_characters, 1, kWebSearchMaxContextLimit);
}

[[nodiscard]] std::chrono::milliseconds ClampTimeout(int timeout_seconds) {
  return std::chrono::seconds(
      std::clamp(timeout_seconds, 1, kWebSearchMaxTimeoutSeconds));
}

[[nodiscard]] std::optional<std::string> OptionalStringField(
    const Json& object, std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end() || it->is_null()) {
    return std::nullopt;
  }
  if (!it->is_string()) {
    throw std::runtime_error(std::string(kInvalidResponseError));
  }
  return it->get<std::string>();
}

[[nodiscard]] std::string FirstStringField(
    const Json& object, std::initializer_list<std::string_view> keys) {
  for (const std::string_view key : keys) {
    if (auto value = OptionalStringField(object, key)) {
      return *value;
    }
  }
  return {};
}

void ThrowIfProviderError(const Json& payload) {
  const auto error_it = payload.find("error");
  if (error_it == payload.end() || error_it->is_null()) {
    return;
  }
  if (error_it->is_string()) {
    throw std::runtime_error(error_it->get<std::string>());
  }
  if (error_it->is_object()) {
    const std::string message =
        FirstStringField(*error_it, {"message", "error"});
    throw std::runtime_error(message.empty() ? "web_search provider error"
                                             : message);
  }
  throw std::runtime_error("web_search provider error");
}

void AppendResultsFromPayload(const Json& payload,
                              std::vector<SearchResult>& results) {
  if (!payload.is_object()) {
    throw std::runtime_error(std::string(kInvalidResponseError));
  }
  ThrowIfProviderError(payload);

  const auto results_it = payload.find("results");
  if (results_it == payload.end()) {
    throw std::runtime_error(std::string(kInvalidResponseError));
  }
  if (!results_it->is_array()) {
    throw std::runtime_error(std::string(kInvalidResponseError));
  }

  for (const auto& item : *results_it) {
    if (!item.is_object()) {
      throw std::runtime_error(std::string(kInvalidResponseError));
    }
    const std::string url = FirstStringField(item, {"url"});
    if (url.empty()) {
      throw std::runtime_error(std::string(kInvalidResponseError));
    }
    results.push_back(SearchResult{
        .title = FirstStringField(item, {"title"}),
        .url = url,
        .snippet = FirstStringField(item, {"snippet", "text", "summary"})});
  }
}

[[nodiscard]] bool IsSseResponse(std::string_view body,
                                 std::string_view content_type) {
  const std::string lowered_content_type = ToLowerAscii(content_type);
  if (lowered_content_type.find("text/event-stream") != std::string::npos) {
    return true;
  }
  std::string_view trimmed = ::yac::util::TrimSv(body);
  return trimmed.starts_with("data:") || trimmed.starts_with(":");
}

[[nodiscard]] Json ParseJsonPayload(std::string_view value) {
  try {
    return Json::parse(value);
  } catch (...) {
    throw std::runtime_error(std::string(kInvalidResponseError));
  }
}

[[nodiscard]] std::size_t CurlHeaderCallback(char* buffer, std::size_t size,
                                             std::size_t nitems,
                                             void* userdata) {
  const std::size_t bytes = size * nitems;
  auto* state = static_cast<CurlSearchState*>(userdata);
  std::string_view line(buffer, bytes);
  if (line.starts_with("HTTP/")) {
    state->content_type.clear();
    return bytes;
  }
  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos) {
    return bytes;
  }
  const std::string name =
      ToLowerAscii(::yac::util::TrimSv(line.substr(0, colon)));
  if (name == "content-type") {
    state->content_type = ::yac::util::Trim(line.substr(colon + 1));
  }
  return bytes;
}

[[nodiscard]] std::size_t CurlBodyCallback(char* ptr, std::size_t size,
                                           std::size_t nmemb, void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* state = static_cast<CurlSearchState*>(userdata);
  state->body.append(ptr, bytes);
  return bytes;
}

int CurlProgressCallback(void* clientp, curl_off_t download_total,
                         curl_off_t download_now, curl_off_t upload_total,
                         curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;
  const auto* state = static_cast<CurlSearchState*>(clientp);
  return state->stop_token.stop_requested() ? 1 : 0;
}

void RequireHttpSuccess(long status_code) {
  if (status_code >= 200 && status_code < 300) {
    return;
  }
  throw std::runtime_error("web_search provider error");
}

[[nodiscard]] std::string FormatSearchOutput(
    const std::vector<SearchResult>& results) {
  if (results.empty()) {
    return "No search results found";
  }
  std::ostringstream output;
  output << "Search results from exa:\n";
  for (std::size_t index = 0; index < results.size(); ++index) {
    const auto& result = results[index];
    output << "\n" << (index + 1) << ". ";
    if (result.title.empty()) {
      output << result.url;
    } else {
      output << result.title;
    }
    output << "\n" << result.url;
    if (!result.snippet.empty()) {
      output << "\n" << result.snippet;
    }
    output << "\n";
  }
  return output.str();
}

}  // namespace

std::string BuildExaWebSearchRequestBody(const WebSearchRequest& request) {
  return Json{{"query", request.query},
              {"num_results", ClampResultLimit(request.num_results)},
              {"context_max_characters",
               ClampContextLimit(request.context_max_characters)}}
      .dump();
}

WebSearchResponse ParseExaWebSearchResponse(std::string_view body,
                                            std::string_view content_type) {
  std::vector<SearchResult> results;
  if (IsSseResponse(body, content_type)) {
    SseEventDataParser parser;
    parser.Feed(body);
    parser.Finish();
    for (const auto& event_data : parser.events) {
      if (::yac::util::TrimSv(event_data) == "[DONE]") {
        continue;
      }
      AppendResultsFromPayload(ParseJsonPayload(event_data), results);
    }
    return {.results = std::move(results)};
  }

  AppendResultsFromPayload(ParseJsonPayload(body), results);
  return {.results = std::move(results)};
}

WebSearchTransportResponse CurlWebSearchTransport::Search(
    const WebSearchTransportRequest& request, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_search cancelled");
  }
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }
  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers =
      curl_slist_append(headers, "Accept: application/json, text/event-stream");
  const std::string authorization = "Authorization: Bearer " + request.api_key;
  headers = curl_slist_append(headers, authorization.c_str());
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  CurlSearchState state{.stop_token = stop_token};
  curl_easy_setopt(curl, CURLOPT_URL, request.endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
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

  const CURLcode result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    throw std::runtime_error("web_search cancelled");
  }
  if (result != CURLE_OK) {
    if (result == CURLE_OPERATION_TIMEDOUT) {
      throw std::runtime_error(std::string(kTimeoutError));
    }
    throw std::runtime_error(curl_easy_strerror(result));
  }

  WebSearchTransportResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  response.content_type = std::move(state.content_type);
  response.body = std::move(state.body);
  return response;
}

WebSearchResponse SearchWeb(const WebSearchRequest& request,
                            const WebSearchProviderConfig& config,
                            WebSearchTransport& transport,
                            std::stop_token stop_token) {
  WebSearchTransportResponse response;
  try {
    response = transport.Search(
        WebSearchTransportRequest{
            .endpoint = config.endpoint,
            .api_key = config.api_key,
            .body = BuildExaWebSearchRequestBody(request),
            .timeout = ClampTimeout(config.timeout_seconds)},
        stop_token);
    RequireHttpSuccess(response.status_code);
    return ParseExaWebSearchResponse(response.body, response.content_type);
  } catch (const std::exception& error) {
    const std::string message = error.what();
    if (ContainsTimeout(message)) {
      throw std::runtime_error(std::string(kTimeoutError));
    }
    if (message == kInvalidResponseError || message == kTimeoutError) {
      throw;
    }
    throw std::runtime_error(RedactProviderError(message, config));
  }
}

ToolExecutionResult ExecuteWebSearchTool(const WebSearchRequest& request,
                                         const WebSearchProviderConfig& config,
                                         WebSearchTransport& transport,
                                         std::stop_token stop_token) {
  WebSearchCall call{.query = request.query,
                     .num_results = ClampResultLimit(request.num_results),
                     .context_max_characters =
                         ClampContextLimit(request.context_max_characters)};
  try {
    WebSearchResponse response =
        SearchWeb(request, config, transport, stop_token);
    call.results = response.results;
    Json results_json = Json::array();
    for (const auto& result : response.results) {
      results_json.push_back(Json{{"title", result.title},
                                  {"url", result.url},
                                  {"snippet", result.snippet}});
    }
    const std::string output = FormatSearchOutput(response.results);
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"query", request.query},
                            {"provider", "exa"},
                            {"metadata", Json{{"provider", "exa"}}},
                            {"output", output},
                            {"results", std::move(results_json)}}
                           .dump()};
  } catch (const std::exception& error) {
    return ToolExecutionResult{
        .block = std::move(call),
        .result_json = Json{{"error", error.what()}}.dump(),
        .is_error = true};
  }
}

}  // namespace yac::tool_call
