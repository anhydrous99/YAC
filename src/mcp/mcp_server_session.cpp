#include "mcp/mcp_server_session.hpp"

#include "mcp/debug_log.hpp"
#include "mcp/protocol_constants.hpp"

#include <chrono>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace yac::mcp {
namespace {

namespace pc = protocol;

struct NonRetriableError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

constexpr auto kInitializeTimeout = std::chrono::seconds(10);
constexpr auto kListTimeout = std::chrono::seconds(10);
constexpr std::string_view kClientName = "YAC";
constexpr std::string_view kClientVersion = "0.1.0";
constexpr auto kCancelGraceMs = std::chrono::milliseconds{1000};
constexpr auto kCancelledIdTtl = std::chrono::seconds{5};
constexpr std::int64_t kNoInflightId = -1;

[[nodiscard]] std::string Serialize(const Json& value) {
  return value.dump();
}

// Extract result field from JSON-RPC envelope.
// SendRequest returns the full envelope:
// {"jsonrpc":"2.0","id":1,"result":{...}} But FromJson methods expect just the
// result object: {...} This helper handles both cases for backward
// compatibility with MockTransport.
[[nodiscard]] Json ExtractResultField(const Json& response) {
  if (response.contains("result")) {
    return response["result"];
  }
  // Fallback for MockTransport which returns just the result object
  return response;
}

}  // namespace

McpServerSession::McpServerSession(
    McpServerConfig config, IMcpTransport* transport, McpDebugLog* debug_log,
    std::chrono::milliseconds initial_reconnect_delay)
    : config_(std::move(config)),
      transport_(transport),
      debug_log_(debug_log),
      initial_reconnect_delay_(initial_reconnect_delay),
      rng_(std::random_device{}()) {}

McpServerSession::~McpServerSession() {
  Stop();
}

void McpServerSession::Start() {
  if (transport_ == nullptr) {
    SetFailure("McpServerSession requires a transport");
    return;
  }

  std::lock_guard lock(mutex_);
  if (worker_.joinable()) {
    return;
  }

  state_ = ServerState::Disconnected;
  last_error_.clear();
  capabilities_ = ServerCapabilities{};
  tools_ = std::make_shared<const std::vector<ToolDefinition>>();
  resources_ = std::make_shared<const std::vector<ResourceDescriptor>>();
  tools_dirty_ = false;
  resources_dirty_ = false;
  worker_ =
      std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
}

void McpServerSession::Stop() {
  const bool has_worker = worker_.joinable();
  if (!has_worker) {
    SetState(ServerState::Disconnected);
    return;
  }

  SetState(ServerState::ShuttingDown);

  const std::int64_t inflight_id = inflight_request_id_.load();
  worker_.request_stop();

  if (inflight_id != kNoInflightId) {
    Json cancel_params = Json::object();
    cancel_params[std::string(pc::kFieldRequestId)] = inflight_id;
    cancel_params[std::string(pc::kFieldReason)] = "user cancelled";
    transport_->SendNotification(pc::kMethodNotificationsCancelled,
                                 cancel_params);
    {
      std::lock_guard<std::mutex> lock(cancelled_mutex_);
      cancelled_ids_[inflight_id] =
          std::chrono::steady_clock::now() + kCancelledIdTtl;
    }
    std::this_thread::sleep_for(kCancelGraceMs);
  }

  transport_->Stop(worker_.get_stop_token());
  if (debug_log_ != nullptr) {
    debug_log_->LogShutdown();
  }
  worker_.join();
  SetState(ServerState::Disconnected);
}

ServerState McpServerSession::State() const {
  std::lock_guard lock(mutex_);
  return state_;
}

std::string McpServerSession::LastError() const {
  std::lock_guard lock(mutex_);
  return last_error_;
}

std::shared_ptr<const std::vector<ToolDefinition>> McpServerSession::Tools()
    const {
  std::lock_guard lock(mutex_);
  return tools_;
}

std::shared_ptr<const std::vector<ResourceDescriptor>>
McpServerSession::Resources() const {
  std::lock_guard lock(mutex_);
  return resources_;
}

const ServerCapabilities& McpServerSession::Capabilities() const {
  return capabilities_;
}

void McpServerSession::RefreshIfDirty() {
  if (State() != ServerState::Ready) {
    return;
  }

  if (tools_dirty_.exchange(false)) {
    try {
      auto refreshed_tools = FetchTools(std::stop_token{});
      std::lock_guard lock(mutex_);
      tools_ = std::make_shared<const std::vector<ToolDefinition>>(
          std::move(refreshed_tools));
    } catch (...) {
      tools_dirty_ = true;
      throw;
    }
  }
  if (resources_dirty_.exchange(false)) {
    try {
      auto refreshed_resources = FetchResources(std::stop_token{});
      std::lock_guard lock(mutex_);
      resources_ = std::make_shared<const std::vector<ResourceDescriptor>>(
          std::move(refreshed_resources));
    } catch (...) {
      resources_dirty_ = true;
      throw;
    }
  }
}

void McpServerSession::MarkToolsDirty() {
  tools_dirty_ = true;
}

void McpServerSession::MarkResourcesDirty() {
  resources_dirty_ = true;
}

void McpServerSession::BackoffSleep(std::stop_token stop_token,
                                    std::chrono::milliseconds delay) {
  const long long jitter_max = delay.count() / 4;
  const auto jitter = std::chrono::milliseconds(
      jitter_max > 0
          ? std::uniform_int_distribution<long long>{0LL, jitter_max}(rng_)
          : 0LL);
  // libc++#76807 workaround: see chat_service.cpp WorkerLoop.
  std::stop_callback wake_on_stop(
      stop_token, [this] { reconnect_cv_.notify_all(); });
  std::unique_lock<std::mutex> lock(reconnect_mutex_);
  (void)reconnect_cv_.wait_for(lock, delay + jitter, [&] {
    return stop_token.stop_requested();
  });
}

void McpServerSession::Run(std::stop_token stop_token) {
  transport_->SetNotificationCallback(
      [this](std::string_view method, const Json& params) {
        HandleNotification(method, params);
      });

  auto delay = initial_reconnect_delay_;
  int attempt = 0;

  while (!stop_token.stop_requested()) {
    try {
      SetState(ServerState::Connecting);
      transport_->Start();
      PerformHandshake(stop_token);
      SetState(ServerState::Ready);
      break;
    } catch (const NonRetriableError& error) {
      SetFailure(error.what());
      return;
    } catch (const std::exception& error) {
      if (stop_token.stop_requested()) {
        break;
      }
      ++attempt;
      if (attempt >= pc::kReconnectMaxAttempts) {
        SetFailure(error.what());
        return;
      }
      SetState(ServerState::Reconnecting);
      BackoffSleep(stop_token, delay);
      if (stop_token.stop_requested()) {
        break;
      }
      delay = std::min(std::chrono::duration_cast<std::chrono::milliseconds>(
                           delay * pc::kReconnectBackoffMultiplier),
                       pc::kReconnectMaxDelayMs);
    }
  }
}

void McpServerSession::HandleNotification(std::string_view method,
                                          const Json& params) {
  if (method == pc::kMethodNotificationsToolsListChanged) {
    MarkToolsDirty();
    return;
  }
  if (method == pc::kMethodNotificationsResourcesListChanged) {
    MarkResourcesDirty();
    return;
  }
  if (method == pc::kMethodNotificationsProgress) {
    (void)ProgressNotification::FromJson(params);
    return;
  }
  if (method == pc::kMethodNotificationsMessage && debug_log_ != nullptr) {
    debug_log_->LogFrame("notify", Serialize(params));
  }
}

void McpServerSession::SetState(ServerState state) {
  std::lock_guard lock(mutex_);
  state_ = state;
}

void McpServerSession::SetFailure(std::string message) {
  std::lock_guard lock(mutex_);
  last_error_ = std::move(message);
  state_ = ServerState::Failed;
}

Json McpServerSession::SendTracked(std::string_view method, const Json& params,
                                   std::chrono::milliseconds timeout,
                                   std::stop_token stop_token) {
  const std::int64_t id = next_request_id_.fetch_add(1) + 1;
  inflight_request_id_.store(id);
  struct InflightGuard {
    explicit InflightGuard(std::atomic<std::int64_t>* ptr) noexcept
        : inflight(ptr) {}
    InflightGuard(const InflightGuard&) = delete;
    InflightGuard& operator=(const InflightGuard&) = delete;
    InflightGuard(InflightGuard&&) = delete;
    InflightGuard& operator=(InflightGuard&&) = delete;
    ~InflightGuard() noexcept { inflight->store(kNoInflightId); }
    std::atomic<std::int64_t>* inflight;
  } guard{&inflight_request_id_};

  Json result = transport_->SendRequest(method, params, timeout, stop_token);

  if (stop_token.stop_requested()) {
    throw std::runtime_error("request " + std::to_string(id) + " cancelled");
  }
  {
    std::lock_guard<std::mutex> lock(cancelled_mutex_);
    PurgeStaleCancelledIds();
    if (cancelled_ids_.contains(id)) {
      if (debug_log_ != nullptr) {
        debug_log_->LogFrame("cancelled_response_discarded",
                             std::to_string(id));
      }
      throw std::runtime_error("response discarded for cancelled request " +
                               std::to_string(id));
    }
  }
  return result;
}

void McpServerSession::PurgeStaleCancelledIds() {
  const auto now = std::chrono::steady_clock::now();
  for (auto it = cancelled_ids_.begin(); it != cancelled_ids_.end();) {
    if (it->second <= now) {
      it = cancelled_ids_.erase(it);
    } else {
      ++it;
    }
  }
}

InitializeRequest McpServerSession::BuildInitializeRequest() const {
  return InitializeRequest{
      .protocol_version = std::string(pc::kMcpProtocolVersion),
      .capabilities = ClientCapabilities{},
      .client_info = ImplementationInfo{.name = std::string(kClientName),
                                        .version = std::string(kClientVersion)},
  };
}

void McpServerSession::PerformHandshake(std::stop_token stop_token) {
  SetState(ServerState::Initializing);
  const auto response_json = SendTracked(
      pc::kMethodInitialize, BuildInitializeRequest().ToJson(),
      std::chrono::duration_cast<std::chrono::milliseconds>(kInitializeTimeout),
      stop_token);
  const auto response =
      InitializeResponse::FromJson(ExtractResultField(response_json));
  ValidateProtocolVersion(response.protocol_version);

  {
    std::lock_guard lock(mutex_);
    capabilities_ = response.capabilities;
  }

  transport_->SendNotification(pc::kMethodInitialized, Json::object());

  if (response.capabilities.has_tools) {
    auto fetched_tools = FetchTools(stop_token);
    std::lock_guard lock(mutex_);
    tools_ = std::make_shared<const std::vector<ToolDefinition>>(
        std::move(fetched_tools));
  }
  if (response.capabilities.has_resources) {
    auto fetched_resources = FetchResources(stop_token);
    std::lock_guard lock(mutex_);
    resources_ = std::make_shared<const std::vector<ResourceDescriptor>>(
        std::move(fetched_resources));
  }
}

void McpServerSession::ValidateProtocolVersion(
    const std::string& server_protocol_version) {
  const auto client_protocol_version = std::string(pc::kMcpProtocolVersion);
  if (server_protocol_version > client_protocol_version) {
    throw NonRetriableError(
        "Unsupported protocol version from server: " + server_protocol_version +
        " (client supports " + client_protocol_version + ")");
  }
}

std::vector<ToolDefinition> McpServerSession::FetchTools(
    std::stop_token stop_token) {
  std::vector<ToolDefinition> tools;
  std::optional<std::string> cursor;
  do {
    Json params = Json::object();
    if (cursor) {
      params[std::string(pc::kFieldCursor)] = *cursor;
    }
    const auto response_json = SendTracked(
        pc::kMethodToolsList, params,
        std::chrono::duration_cast<std::chrono::milliseconds>(kListTimeout),
        stop_token);
    auto page = ToolsListResponse::FromJson(ExtractResultField(response_json));
    tools.insert(tools.end(), page.tools.begin(), page.tools.end());
    cursor = std::move(page.next_cursor);
  } while (cursor.has_value());
  return tools;
}

std::vector<ResourceDescriptor> McpServerSession::FetchResources(
    std::stop_token stop_token) {
  std::vector<ResourceDescriptor> resources;
  std::optional<std::string> cursor;
  do {
    Json params = Json::object();
    if (cursor) {
      params[std::string(pc::kFieldCursor)] = *cursor;
    }
    const auto response_json = SendTracked(
        pc::kMethodResourcesList, params,
        std::chrono::duration_cast<std::chrono::milliseconds>(kListTimeout),
        stop_token);
    auto page =
        ResourcesListResponse::FromJson(ExtractResultField(response_json));
    resources.insert(resources.end(), page.resources.begin(),
                     page.resources.end());
    cursor = std::move(page.next_cursor);
  } while (cursor.has_value());
  return resources;
}

}  // namespace yac::mcp
