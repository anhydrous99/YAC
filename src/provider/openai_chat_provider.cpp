#include "provider/openai_chat_provider.hpp"

#include <cstdlib>
#include <curl/curl.h>
#include <openai.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace yac::provider {
namespace {

using Json = openai::_detail::Json;

Json BuildChatPayload(const chat::ChatRequest& request, bool stream) {
  Json messages = Json::array();
  for (const auto& message : request.messages) {
    messages.push_back(
        {{"role", OpenAiChatProvider::RoleToOpenAi(message.role)},
         {"content", message.content}});
  }

  return {{"model", request.model},
          {"messages", std::move(messages)},
          {"temperature", request.temperature},
          {"stream", stream}};
}

size_t WriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* buffer = static_cast<std::string*>(userdata);
  buffer->append(ptr, bytes);
  return bytes;
}

struct StreamState {
  std::string buffer;
  ChatEventSink* sink = nullptr;
};

void DispatchSseLine(const std::string& line, ChatEventSink& sink) {
  constexpr std::string_view kPrefix = "data: ";
  if (!line.starts_with(kPrefix)) {
    return;
  }

  auto data = line.substr(kPrefix.size());
  if (data == "[DONE]") {
    return;
  }

  auto event = OpenAiChatProvider::ParseStreamData(data);
  if (event.type == chat::ChatEventType::TextDelta ||
      event.type == chat::ChatEventType::Error) {
    sink(std::move(event));
  }
}

size_t WriteStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* state = static_cast<StreamState*>(userdata);
  state->buffer.append(ptr, bytes);

  size_t pos = 0;
  while ((pos = state->buffer.find('\n')) != std::string::npos) {
    auto line = state->buffer.substr(0, pos);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    state->buffer.erase(0, pos + 1);
    DispatchSseLine(line, *state->sink);
  }

  return bytes;
}

struct ProgressState {
  std::stop_token* stop_token = nullptr;
};

int ProgressCallback(void* clientp, curl_off_t download_total,
                     curl_off_t download_now, curl_off_t upload_total,
                     curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;

  const auto* state = static_cast<ProgressState*>(clientp);
  return state->stop_token->stop_requested() ? 1 : 0;
}

std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string ExtractBufferedText(const Json& response) {
  if (!response.contains("choices") || response["choices"].empty()) {
    return {};
  }
  const auto& choice = response["choices"][0];
  if (!choice.contains("message") || !choice["message"].contains("content")) {
    return {};
  }
  return choice["message"]["content"].get<std::string>();
}

}  // namespace

OpenAiChatProvider::OpenAiChatProvider(chat::ProviderConfig config)
    : config_(std::move(config)) {}

std::string OpenAiChatProvider::Id() const {
  return config_.id;
}

void OpenAiChatProvider::CompleteStream(const chat::ChatRequest& request,
                                        ChatEventSink sink,
                                        std::stop_token stop_token) {
  try {
    if (request.stream) {
      CompleteStreaming(request, std::move(sink), stop_token);
      return;
    }
    CompleteBuffered(request, std::move(sink));
  } catch (const std::exception& error) {
    sink(chat::ChatEvent{.type = chat::ChatEventType::Error,
                         .text = error.what(),
                         .provider_id = config_.id,
                         .model = request.model});
  }
}

std::string OpenAiChatProvider::RoleToOpenAi(chat::ChatRole role) {
  switch (role) {
    case chat::ChatRole::System:
      return "system";
    case chat::ChatRole::User:
      return "user";
    case chat::ChatRole::Assistant:
      return "assistant";
    case chat::ChatRole::Tool:
      return "tool";
  }
  return "user";
}

chat::ChatEvent OpenAiChatProvider::ParseStreamData(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    if (json.contains("error")) {
      return chat::ChatEvent{.type = chat::ChatEventType::Error,
                             .text = json["error"].dump()};
    }
    if (!json.contains("choices") || json["choices"].empty()) {
      return chat::ChatEvent{.type = chat::ChatEventType::TextDelta};
    }
    const auto& choice = json["choices"][0];
    if (!choice.contains("delta") || !choice["delta"].contains("content")) {
      return chat::ChatEvent{.type = chat::ChatEventType::TextDelta};
    }
    return chat::ChatEvent{
        .type = chat::ChatEventType::TextDelta,
        .text = choice["delta"]["content"].get<std::string>()};
  } catch (const std::exception& error) {
    return chat::ChatEvent{.type = chat::ChatEventType::Error,
                           .text = error.what()};
  }
}

void OpenAiChatProvider::CompleteBuffered(const chat::ChatRequest& request,
                                          ChatEventSink sink) {
  openai::OpenAI client(ResolveApiKey(), "", true, config_.base_url);
  const auto response = client.chat.create(BuildChatPayload(request, false));
  const auto text = ExtractBufferedText(response);
  if (!text.empty()) {
    sink(chat::ChatEvent{.type = chat::ChatEventType::TextDelta,
                         .text = text,
                         .provider_id = config_.id,
                         .model = request.model});
  }
}

void OpenAiChatProvider::CompleteStreaming(const chat::ChatRequest& request,
                                           ChatEventSink sink,
                                           std::stop_token stop_token) {
  const auto api_key = ResolveApiKey();
  if (api_key.empty()) {
    throw std::runtime_error(config_.api_key_env + " is not set.");
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  auto payload = BuildChatPayload(request, true).dump();
  StreamState stream_state{.sink = &sink};
  ProgressState progress_state{.stop_token = &stop_token};

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: text/event-stream");
  const auto auth_header = "Authorization: Bearer " + api_key;
  headers = curl_slist_append(headers, auth_header.c_str());

  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  const auto url = CompletionUrl();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStream);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_state);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_state);

  const auto result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    return;
  }
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400) {
    std::ostringstream message;
    message << "OpenAI request failed with HTTP " << status << ".";
    throw std::runtime_error(message.str());
  }
}

std::string OpenAiChatProvider::ResolveApiKey() const {
  if (!config_.api_key.empty()) {
    return config_.api_key;
  }
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    return env;
  }
  return {};
}

std::string OpenAiChatProvider::CompletionUrl() const {
  return TrimTrailingSlash(config_.base_url) + "/chat/completions";
}

}  // namespace yac::provider
