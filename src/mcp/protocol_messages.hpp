#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace yac::mcp {

using Json = nlohmann::json;

class McpProtocolError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ImplementationInfo {
  std::string name;
  std::string version;

  [[nodiscard]] static ImplementationInfo FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ClientCapabilities {
  bool has_roots = false;
  bool roots_list_changed = false;
  bool has_sampling = false;

  [[nodiscard]] static ClientCapabilities FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ServerCapabilities {
  bool has_tools = false;
  bool tools_list_changed = false;
  bool has_resources = false;
  bool resources_list_changed = false;
  bool resources_subscribe = false;
  bool has_logging = false;

  [[nodiscard]] static ServerCapabilities FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct InitializeRequest {
  std::string protocol_version;
  ClientCapabilities capabilities;
  ImplementationInfo client_info;

  [[nodiscard]] static InitializeRequest FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct InitializeResponse {
  std::string protocol_version;
  ServerCapabilities capabilities;
  ImplementationInfo server_info;
  std::optional<std::string> instructions;

  [[nodiscard]] static InitializeResponse FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ToolDefinition {
  std::string name;
  std::optional<std::string> description;
  Json input_schema = Json::object();

  [[nodiscard]] static ToolDefinition FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ToolsListResponse {
  std::vector<ToolDefinition> tools;
  std::optional<std::string> next_cursor;

  [[nodiscard]] static ToolsListResponse FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ToolsCallRequest {
  std::string name;
  Json arguments = Json::object();

  [[nodiscard]] static ToolsCallRequest FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct TextContent {
  std::string text;
};

struct ImageContent {
  std::string data;
  std::string mime_type;
};

struct AudioContent {
  std::string data;
  std::string mime_type;
};

struct EmbeddedResourceContent {
  std::string uri;
  std::optional<std::string> mime_type;
  std::optional<std::string> text;
  std::optional<std::string> blob;
};

struct ResourceLinkContent {
  std::string uri;
  std::optional<std::string> name;
  std::optional<std::string> description;
  std::optional<std::string> mime_type;
};

using McpContentBlock =
    std::variant<TextContent, ImageContent, AudioContent,
                 EmbeddedResourceContent, ResourceLinkContent>;

[[nodiscard]] McpContentBlock McpContentBlockFromJson(const Json& j);
[[nodiscard]] Json McpContentBlockToJson(const McpContentBlock& block);

struct ToolsCallResponse {
  std::vector<McpContentBlock> result_blocks;
  bool is_error = false;

  [[nodiscard]] static ToolsCallResponse FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ResourceDescriptor {
  std::string uri;
  std::optional<std::string> name;
  std::optional<std::string> description;
  std::optional<std::string> mime_type;

  [[nodiscard]] static ResourceDescriptor FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ResourcesListResponse {
  std::vector<ResourceDescriptor> resources;
  std::optional<std::string> next_cursor;

  [[nodiscard]] static ResourcesListResponse FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ResourcesReadRequest {
  std::string uri;

  [[nodiscard]] static ResourcesReadRequest FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ResourceContent {
  std::string uri;
  std::optional<std::string> mime_type;
  std::optional<std::string> text;
  std::optional<std::string> blob;

  [[nodiscard]] static ResourceContent FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ResourcesReadResponse {
  std::vector<ResourceContent> contents;

  [[nodiscard]] static ResourcesReadResponse FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct CancelledNotification {
  Json request_id;
  std::optional<std::string> reason;

  [[nodiscard]] static CancelledNotification FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct ProgressNotification {
  Json progress_token;
  double progress = 0.0;
  std::optional<double> total;

  [[nodiscard]] static ProgressNotification FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

struct LogMessageNotification {
  std::string level;
  std::optional<std::string> logger;
  Json data;

  [[nodiscard]] static LogMessageNotification FromJson(const Json& j);
  [[nodiscard]] Json ToJson() const;
};

}  // namespace yac::mcp
