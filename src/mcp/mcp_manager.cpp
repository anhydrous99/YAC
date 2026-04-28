#include "mcp/mcp_manager.hpp"

#include "mcp/debug_log.hpp"
#include "mcp/file_token_store.hpp"
#include "mcp/keychain_token_store.hpp"
#include "mcp/mcp_server_session.hpp"
#include "mcp/oauth/loopback_callback.hpp"
#include "mcp/oauth/pkce.hpp"
#include "mcp/protocol_constants.hpp"
#include "mcp/protocol_messages.hpp"
#include "mcp/stdio_mcp_transport.hpp"
#include "mcp/streamable_http_mcp_transport.hpp"
#include "mcp/tool_naming.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::mcp {
namespace {

constexpr auto kRequestTimeout = std::chrono::seconds(15);

std::string ToString(ServerState state) {
  switch (state) {
    case ServerState::Disconnected:
      return "Disconnected";
    case ServerState::Connecting:
      return "Connecting";
    case ServerState::Initializing:
      return "Initializing";
    case ServerState::Ready:
      return "Ready";
    case ServerState::Reconnecting:
      return "Reconnecting";
    case ServerState::Failed:
      return "Failed";
    case ServerState::ShuttingDown:
      return "ShuttingDown";
  }
  return "Unknown";
}

std::string SerializeOAuthTokens(const oauth::OAuthTokens& tokens) {
  const auto expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                              tokens.expires_at.time_since_epoch())
                              .count();
  return Json{{"access_token", tokens.access_token},
              {"refresh_token", tokens.refresh_token},
              {"expires_at", expires_at},
              {"token_type", tokens.token_type},
              {"scope", tokens.scope}}
      .dump();
}

oauth::OAuthTokens RunOAuthAuthentication(
    const McpServerConfig& config, const oauth::OAuthInteractionMode& mode,
    std::stop_token stop) {
  if (!config.auth.has_value() ||
      !std::holds_alternative<McpAuthOAuth>(*config.auth)) {
    throw std::runtime_error("MCP server is not configured for OAuth");
  }

  const auto& auth = std::get<McpAuthOAuth>(*config.auth);
  const std::string verifier = oauth::GenerateCodeVerifier();
  const std::string challenge = oauth::DeriveCodeChallenge(verifier);
  const std::string state = oauth::GenerateCodeVerifier();
  oauth::LoopbackCallbackServer callback_server;
  oauth::OAuthFlow flow;

  const oauth::OAuthConfig oauth_config{
      .authorization_url = auth.authorization_url,
      .token_url = auth.token_url,
      .client_id = auth.client_id,
      .scopes = auth.scopes,
      .resource_url = config.url,
  };
  const std::string redirect_uri = callback_server.RedirectUri();
  const std::string auth_url = flow.BuildAuthorizationUrl(
      oauth_config, challenge, state, redirect_uri, config.url);
  const auto callback = oauth::RunOAuthInteraction(mode, auth_url, stop);
  if (!callback.has_value()) {
    throw std::runtime_error("OAuth authentication cancelled");
  }

  flow.ValidateState(callback->second);
  return flow.ExchangeCode(oauth_config, callback->first, verifier,
                           redirect_uri, config.url);
}

std::vector<core_types::McpResourceDescriptor> ConvertResourceDescriptors(
    const std::vector<ResourceDescriptor>& resources) {
  std::vector<core_types::McpResourceDescriptor> converted;
  converted.reserve(resources.size());
  for (const auto& resource : resources) {
    converted.push_back(core_types::McpResourceDescriptor{
        .uri = resource.uri,
        .name = resource.name.value_or(std::string{}),
        .title = resource.name.value_or(std::string{}),
        .description = resource.description.value_or(std::string{}),
        .mime_type = resource.mime_type.value_or(std::string{}),
    });
  }
  return converted;
}

std::vector<std::byte> BytesFromString(std::string_view data) {
  std::vector<std::byte> bytes;
  bytes.reserve(data.size());
  for (const char c : data) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  return bytes;
}

tool_call::McpResultBlock ConvertResultBlock(const McpContentBlock& block) {
  return std::visit(
      [](const auto& content) -> tool_call::McpResultBlock {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, TextContent>) {
          return tool_call::McpResultBlock{
              .kind = tool_call::McpResultBlockKind::Text,
              .text = content.text,
              .mime_type = {},
              .uri = {},
              .name = {},
              .bytes = content.text.size()};
        } else if constexpr (std::is_same_v<T, ImageContent>) {
          return tool_call::McpResultBlock{
              .kind = tool_call::McpResultBlockKind::Image,
              .text = content.data,
              .mime_type = content.mime_type,
              .uri = {},
              .name = {},
              .bytes = content.data.size()};
        } else if constexpr (std::is_same_v<T, AudioContent>) {
          return tool_call::McpResultBlock{
              .kind = tool_call::McpResultBlockKind::Audio,
              .text = content.data,
              .mime_type = content.mime_type,
              .uri = {},
              .name = {},
              .bytes = content.data.size()};
        } else if constexpr (std::is_same_v<T, EmbeddedResourceContent>) {
          const std::string text =
              content.text.value_or(content.blob.value_or(std::string{}));
          return tool_call::McpResultBlock{
              .kind = tool_call::McpResultBlockKind::EmbeddedResource,
              .text = text,
              .mime_type = content.mime_type.value_or(std::string{}),
              .uri = content.uri,
              .name = {},
              .bytes = text.size()};
        } else {
          const std::string description =
              content.description.value_or(std::string{});
          return tool_call::McpResultBlock{
              .kind = tool_call::McpResultBlockKind::ResourceLink,
              .text = description,
              .mime_type = content.mime_type.value_or(std::string{}),
              .uri = content.uri,
              .name = content.name.value_or(std::string{}),
              .bytes = description.size()};
        }
      },
      block);
}

void TruncateResult(tool_call::McpToolCall& call, std::uintmax_t max_bytes) {
  if (max_bytes == 0) {
    call.result_blocks.clear();
    call.is_truncated = call.result_bytes > 0;
    return;
  }

  std::uintmax_t remaining = max_bytes;
  std::vector<tool_call::McpResultBlock> truncated;
  truncated.reserve(call.result_blocks.size());
  bool hit_limit = false;
  for (auto block : call.result_blocks) {
    if (remaining == 0) {
      hit_limit = true;
      break;
    }

    const auto shrink_string = [&remaining, &hit_limit](std::string& value) {
      if (value.size() <= remaining) {
        remaining -= value.size();
        return;
      }
      value.resize(static_cast<std::size_t>(remaining));
      remaining = 0;
      hit_limit = true;
    };

    shrink_string(block.text);
    if (remaining > 0) {
      shrink_string(block.mime_type);
    }
    if (remaining > 0) {
      shrink_string(block.uri);
    }
    if (remaining > 0) {
      shrink_string(block.name);
    }
    block.bytes = block.text.size() + block.mime_type.size() +
                  block.uri.size() + block.name.size();
    truncated.push_back(std::move(block));
    if (hit_limit) {
      break;
    }
  }
  call.result_blocks = std::move(truncated);
  call.is_truncated = call.is_truncated || hit_limit;
}

}  // namespace

class McpManager::ObservedTransport : public IMcpTransport {
 public:
  using ObserverFn =
      std::function<void(std::string_view method, const Json& params)>;

  ObservedTransport(std::unique_ptr<IMcpTransport> inner, ObserverFn observer)
      : inner_(std::move(inner)), observer_(std::move(observer)) {}

  void Start() override { inner_->Start(); }

  void Stop(std::stop_token stop) override { inner_->Stop(stop); }

  [[nodiscard]] Json SendRequest(std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop) override {
    return inner_->SendRequest(method, params, timeout, stop);
  }

  void SendNotification(std::string_view method, const Json& params) override {
    inner_->SendNotification(method, params);
  }

  void SetNotificationCallback(NotificationCallback callback) override {
    callback_ = std::move(callback);
    inner_->SetNotificationCallback(
        [this](std::string_view method, const Json& params) {
          if (observer_) {
            observer_(method, params);
          }
          if (callback_) {
            callback_(method, params);
          }
        });
  }

  [[nodiscard]] TransportStatus Status() const override {
    return inner_->Status();
  }

 private:
  std::unique_ptr<IMcpTransport> inner_;
  ObserverFn observer_;
  NotificationCallback callback_;
};

struct McpManager::SessionRecord {
  McpServerConfig config;
  std::unique_ptr<ObservedTransport> transport;
  std::unique_ptr<McpDebugLog> debug_log;
  std::unique_ptr<McpServerSession> session;
  std::string last_emitted_state = "Disconnected";
};

McpManager::Dependencies McpManager::BuildDefaultDependencies() {
  return Dependencies{
      .transport_factory =
          [](const McpServerConfig& config) -> std::unique_ptr<IMcpTransport> {
        if (config.transport == "stdio") {
          return std::make_unique<StdioMcpTransport>(config);
        }
        if (config.transport == "http") {
          return std::make_unique<StreamableHttpMcpTransport>(config);
        }
        throw std::runtime_error("Unsupported MCP transport: " +
                                 config.transport);
      },
      .authenticate_fn = RunOAuthAuthentication,
      .keychain_token_store = std::make_shared<KeychainTokenStore>(),
      .file_token_store = std::make_shared<FileTokenStore>(),
      .emit_issue =
          [](chat::ConfigIssue issue) {
            std::clog << "[mcp] " << static_cast<int>(issue.severity) << ": "
                      << issue.message;
            if (!issue.detail.empty()) {
              std::clog << " (" << issue.detail << ")";
            }
            std::clog << '\n';
          },
  };
}

McpManager::McpManager(McpConfig config, EmitEventFn emit_event)
    : McpManager(std::move(config), std::move(emit_event),
                 BuildDefaultDependencies()) {}

McpManager::McpManager(McpConfig config, EmitEventFn emit_event,
                       Dependencies deps)
    : config_(std::move(config)),
      emit_event_(std::move(emit_event)),
      deps_(std::move(deps)) {}

McpManager::~McpManager() {
  Stop();
}

void McpManager::EnsureSessionsCreated() const {
  if (!sessions_.empty()) {
    return;
  }

  sessions_.reserve(config_.servers.size());
  for (const auto& server : config_.servers) {
    auto transport = deps_.transport_factory(server);
    auto observed_transport = std::make_unique<ObservedTransport>(
        std::move(transport), [this, server_id = server.id](
                                  std::string_view method, const Json& params) {
          HandleNotification(server_id, method, params);
        });
    auto debug_log = std::make_unique<McpDebugLog>(server.id);
    auto session = std::make_unique<McpServerSession>(server,
                                                      observed_transport.get(),
                                                      debug_log.get());
    sessions_.push_back(
        SessionRecord{.config = server,
                      .transport = std::move(observed_transport),
                      .debug_log = std::move(debug_log),
                      .session = std::move(session)});
  }
}

void McpManager::Start() {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  if (started_) {
    EmitStateChanges();
    return;
  }

  for (auto& record : sessions_) {
    if (record.config.auto_start && record.config.enabled) {
      record.session->Start();
    }
  }
  started_ = true;
  EmitStateChanges();
}

void McpManager::Stop() {
  std::lock_guard lock(mutex_);
  if (sessions_.empty()) {
    started_ = false;
    return;
  }
  for (auto& record : sessions_) {
    record.session->Stop();
  }
  started_ = false;
  EmitStateChanges();
}

McpManager::SessionRecord& McpManager::RequireRecord(
    std::string_view server_id) {
  auto it = std::find_if(sessions_.begin(), sessions_.end(),
                         [server_id](const SessionRecord& record) {
                           return record.config.id == server_id;
                         });
  if (it == sessions_.end()) {
    throw std::runtime_error("Unknown MCP server: " + std::string(server_id));
  }
  return *it;
}

const McpManager::SessionRecord& McpManager::RequireRecord(
    std::string_view server_id) const {
  auto it = std::find_if(sessions_.begin(), sessions_.end(),
                         [server_id](const SessionRecord& record) {
                           return record.config.id == server_id;
                         });
  if (it == sessions_.end()) {
    throw std::runtime_error("Unknown MCP server: " + std::string(server_id));
  }
  return *it;
}

void McpManager::EmitStateChanges() const {
  for (auto& record : sessions_) {
    const std::string state = ToString(record.session->State());
    if (state == record.last_emitted_state) {
      continue;
    }
    record.last_emitted_state = state;
    emit_event_(chat::MakeMcpServerStateChangedEvent(
        record.config.id, state, record.session->LastError()));
  }
}

void McpManager::HandleNotification(std::string_view server_id,
                                    std::string_view method,
                                    const Json& params) const {
  if (method == protocol::kMethodNotificationsProgress) {
    const auto progress = ProgressNotification::FromJson(params);
    std::ostringstream text;
    text << std::string(server_id) << " progress";
    emit_event_(chat::MakeMcpProgressUpdateEvent(
        0, text.str(), progress.progress, progress.total.value_or(0.0)));
  }
}

core_types::McpToolCatalogSnapshot McpManager::GetToolCatalogSnapshot() const {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  EmitStateChanges();

  latest_snapshot_ = BuildSnapshotLocked();
  return latest_snapshot_;
}

core_types::McpToolCatalogSnapshot McpManager::BuildSnapshotLocked() const {
  core_types::McpToolCatalogSnapshot snapshot;
  snapshot.revision_id = next_revision_id_.fetch_add(1);
  for (auto& record : sessions_) {
    record.session->RefreshIfDirty();
    const auto tools = record.session->Tools();
    if (!tools) {
      continue;
    }

    for (const auto& tool : *tools) {
      const std::string qualified_name =
          SanitizeMcpToolName(record.config.id, tool.name);
      snapshot.tools.push_back(chat::ToolDefinition{
          .name = qualified_name,
          .description = tool.description.value_or(std::string{}),
          .parameters_schema_json = tool.input_schema.dump(),
      });
      snapshot.name_to_server_tool.emplace(
          qualified_name, std::make_pair(record.config.id, tool.name));
      const bool requires_approval =
          record.config.requires_approval ||
          std::find(record.config.approval_required_tools.begin(),
                    record.config.approval_required_tools.end(),
                    tool.name) != record.config.approval_required_tools.end();
      snapshot.approval_policy.emplace(
          qualified_name,
          core_types::McpToolApprovalPolicy{
              .requires_approval = requires_approval,
              .server_requires_approval = record.config.requires_approval,
              .approval_required_tools = record.config.approval_required_tools,
          });
    }
  }
  return snapshot;
}

core_types::ToolExecutionResult McpManager::InvokeTool(
    std::string_view qualified_name, std::string_view arguments_json,
    std::stop_token stop) {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  if (!latest_snapshot_.name_to_server_tool.contains(
          std::string(qualified_name))) {
    latest_snapshot_ = BuildSnapshotLocked();
  }

  const auto it =
      latest_snapshot_.name_to_server_tool.find(std::string(qualified_name));
  if (it == latest_snapshot_.name_to_server_tool.end()) {
    throw std::runtime_error("Unknown MCP tool: " +
                             std::string(qualified_name));
  }

  auto& record = RequireRecord(it->second.first);
  Json params = Json::object();
  params[std::string(protocol::kFieldName)] = it->second.second;
  params[std::string(protocol::kFieldArguments)] =
      arguments_json.empty() ? Json::object() : Json::parse(arguments_json);
  const Json response_json = record.transport->SendRequest(
      protocol::kMethodToolsCall, params,
      std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout),
      stop);
  const auto response = ToolsCallResponse::FromJson(response_json);

  tool_call::McpToolCall block{.server_id = record.config.id,
                               .tool_name = std::string(qualified_name),
                               .original_tool_name = it->second.second,
                               .arguments_json = std::string(arguments_json),
                               .is_error = response.is_error,
                               .result_bytes = response_json.dump().size()};
  block.result_blocks.reserve(response.result_blocks.size());
  for (const auto& result_block : response.result_blocks) {
    block.result_blocks.push_back(ConvertResultBlock(result_block));
  }
  if (block.result_bytes > config_.result_max_bytes) {
    block.is_truncated = true;
    TruncateResult(block, config_.result_max_bytes);
  }

  Json result_json = Json::object();
  result_json[std::string(protocol::kFieldIsError)] = response.is_error;
  result_json[std::string(protocol::kFieldContent)] = Json::array();
  for (const auto& result_block : block.result_blocks) {
    result_json[std::string(protocol::kFieldContent)].push_back(
        {{"kind", static_cast<int>(result_block.kind)},
         {"text", result_block.text},
         {"mime_type", result_block.mime_type},
         {"uri", result_block.uri},
         {"name", result_block.name},
         {"bytes", result_block.bytes}});
  }
  if (block.is_truncated) {
    result_json["is_truncated"] = true;
  }

  return core_types::ToolExecutionResult{.block = std::move(block),
                                         .result_json = result_json.dump(),
                                         .is_error = response.is_error};
}

std::vector<core_types::McpServerStatus> McpManager::GetServerStatusSnapshot()
    const {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  EmitStateChanges();

  std::vector<core_types::McpServerStatus> status;
  status.reserve(sessions_.size());
  for (const auto& record : sessions_) {
    const auto tools = record.session->Tools();
    const auto resources = record.session->Resources();
    status.push_back(core_types::McpServerStatus{
        .id = record.config.id,
        .state = ToString(record.session->State()),
        .error = record.session->LastError(),
        .transport = record.config.transport,
        .tool_count = tools ? tools->size() : 0,
        .resource_count = resources ? resources->size() : 0,
    });
  }
  return status;
}

std::vector<core_types::McpResourceDescriptor> McpManager::ListResources(
    std::string_view server_id, std::stop_token stop) {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  auto& record = RequireRecord(server_id);

  std::vector<ResourceDescriptor> all_resources;
  std::optional<std::string> cursor;
  do {
    Json params = Json::object();
    if (cursor.has_value()) {
      params[std::string(protocol::kFieldCursor)] = *cursor;
    }
    const auto response_json = record.transport->SendRequest(
        protocol::kMethodResourcesList, params,
        std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout),
        stop);
    auto page = ResourcesListResponse::FromJson(response_json);
    all_resources.insert(all_resources.end(), page.resources.begin(),
                         page.resources.end());
    cursor = std::move(page.next_cursor);
  } while (cursor.has_value());

  return ConvertResourceDescriptors(all_resources);
}

core_types::McpResourceContent McpManager::ReadResource(
    std::string_view server_id, std::string_view uri, std::stop_token stop) {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  auto& record = RequireRecord(server_id);

  const auto response_json = record.transport->SendRequest(
      protocol::kMethodResourcesRead,
      ResourcesReadRequest{.uri = std::string(uri)}.ToJson(),
      std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout),
      stop);
  const auto response = ResourcesReadResponse::FromJson(response_json);
  if (response.contents.empty()) {
    throw std::runtime_error("MCP resource read returned no contents");
  }
  const auto& content = response.contents.front();
  const std::string text = content.text.value_or(std::string{});
  const std::string blob_text = content.blob.value_or(std::string{});
  const std::uintmax_t total_bytes = text.size() + blob_text.size();
  const bool truncated = total_bytes > config_.result_max_bytes;
  const std::size_t text_limit = static_cast<std::size_t>(
      std::min<std::uintmax_t>(config_.result_max_bytes, text.size()));
  const std::size_t blob_limit = static_cast<std::size_t>(
      std::min<std::uintmax_t>(config_.result_max_bytes, blob_text.size()));

  return core_types::McpResourceContent{
      .uri = content.uri,
      .mime_type = content.mime_type.value_or(std::string{}),
      .text = text.substr(0, text_limit),
      .blob = BytesFromString(blob_text.substr(0, blob_limit)),
      .is_truncated = truncated,
      .total_bytes = total_bytes,
  };
}

void McpManager::Authenticate(std::string_view server_id,
                              const oauth::OAuthInteractionMode& mode,
                              std::stop_token stop) {
  std::lock_guard lock(mutex_);
  EnsureSessionsCreated();
  auto& record = RequireRecord(server_id);
  emit_event_(chat::MakeMcpAuthRequiredEvent(
      record.config.id, "OAuth authentication required for MCP server."));

  const oauth::OAuthTokens tokens =
      deps_.authenticate_fn(record.config, mode, stop);
  const std::string token_json = SerializeOAuthTokens(tokens);
  try {
    deps_.keychain_token_store->Set(record.config.id, token_json);
  } catch (const KeychainUnavailableError& error) {
    if (deps_.emit_issue) {
      deps_.emit_issue(chat::ConfigIssue{
          .severity = chat::ConfigIssueSeverity::Warning,
          .message = "MCP auth token storage fell back to file",
          .detail = error.what(),
      });
    }
    deps_.file_token_store->Set(record.config.id, token_json);
  }
}

}  // namespace yac::mcp
