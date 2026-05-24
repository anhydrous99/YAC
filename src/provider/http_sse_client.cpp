#include "provider/http_sse_client.hpp"

#include <curl/curl.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yac::provider::internal {
namespace {

struct CurlStreamState {
  const HttpStreamPostRequest* request = nullptr;
  std::stop_token stop_token;
  std::optional<std::string> callback_error;
};

[[nodiscard]] size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                                    void* userdata) {
  const auto bytes = size * nitems;
  auto* state = static_cast<CurlStreamState*>(userdata);
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

[[nodiscard]] size_t BodyCallback(char* ptr, size_t size, size_t nmemb,
                                  void* userdata) {
  const auto bytes = size * nmemb;
  auto* state = static_cast<CurlStreamState*>(userdata);
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

int ProgressCallback(void* clientp, curl_off_t download_total,
                     curl_off_t download_now, curl_off_t upload_total,
                     curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;

  const auto* state = static_cast<CurlStreamState*>(clientp);
  return state->stop_token.stop_requested() ? 1 : 0;
}

}  // namespace

HttpStreamPostResponse PerformHttpStreamPost(
    const HttpStreamPostRequest& request, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return {.cancelled = true};
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  struct curl_slist* headers = nullptr;
  for (const auto& header : request.headers) {
    curl_slist* appended = curl_slist_append(headers, header.c_str());
    if (appended == nullptr) {
      curl_slist_free_all(headers);
      throw std::runtime_error("curl_slist_append failed.");
    }
    headers = appended;
  }
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  CurlStreamState state{.request = &request, .stop_token = stop_token};

  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(request.payload.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, BodyCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  if (request.timeout.has_value()) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(request.timeout->count()));
  }
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

  const CURLcode result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    return {.cancelled = true};
  }
  if (state.callback_error.has_value()) {
    throw std::runtime_error(*state.callback_error);
  }
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  return {.status_code = status_code};
}

}  // namespace yac::provider::internal
