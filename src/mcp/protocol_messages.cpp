#include "mcp/protocol_messages.hpp"

#include "mcp/json_helpers.hpp"
#include "mcp/protocol_constants.hpp"

#include <string>
#include <type_traits>
#include <utility>

namespace yac::mcp {

namespace pc = protocol;

ImplementationInfo ImplementationInfo::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ImplementationInfo: expected object");
  }
  return ImplementationInfo{
      .name = GetString(j, pc::kFieldName),
      .version = GetString(j, pc::kFieldVersion),
  };
}

Json ImplementationInfo::ToJson() const {
  return Json{{std::string(pc::kFieldName), name},
              {std::string(pc::kFieldVersion), version}};
}

ClientCapabilities ClientCapabilities::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ClientCapabilities: expected object");
  }
  ClientCapabilities caps;
  const std::string roots_key{pc::kCapabilityRoots};
  if (j.contains(roots_key) && j[roots_key].is_object()) {
    caps.has_roots = true;
    caps.roots_list_changed = GetBool(j[roots_key], pc::kCapabilityListChanged);
  }
  const std::string samp_key{pc::kCapabilitySampling};
  caps.has_sampling = j.contains(samp_key) && !j[samp_key].is_null();
  return caps;
}

Json ClientCapabilities::ToJson() const {
  Json result = Json::object();
  if (has_roots) {
    Json roots = Json::object();
    if (roots_list_changed) {
      roots[std::string(pc::kCapabilityListChanged)] = true;
    }
    result[std::string(pc::kCapabilityRoots)] = std::move(roots);
  }
  if (has_sampling) {
    result[std::string(pc::kCapabilitySampling)] = Json::object();
  }
  return result;
}

ServerCapabilities ServerCapabilities::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ServerCapabilities: expected object");
  }
  ServerCapabilities caps;
  const std::string tools_key{pc::kCapabilityTools};
  if (j.contains(tools_key) && j[tools_key].is_object()) {
    caps.has_tools = true;
    caps.tools_list_changed = GetBool(j[tools_key], pc::kCapabilityListChanged);
  }
  const std::string res_key{pc::kCapabilityResources};
  if (j.contains(res_key) && j[res_key].is_object()) {
    caps.has_resources = true;
    caps.resources_list_changed =
        GetBool(j[res_key], pc::kCapabilityListChanged);
    caps.resources_subscribe = GetBool(j[res_key], pc::kCapabilitySubscribe);
  }
  const std::string log_key{pc::kCapabilityLogging};
  caps.has_logging = j.contains(log_key) && !j[log_key].is_null();
  return caps;
}

Json ServerCapabilities::ToJson() const {
  Json result = Json::object();
  if (has_tools) {
    Json tools = Json::object();
    if (tools_list_changed) {
      tools[std::string(pc::kCapabilityListChanged)] = true;
    }
    result[std::string(pc::kCapabilityTools)] = std::move(tools);
  }
  if (has_resources) {
    Json res = Json::object();
    if (resources_list_changed) {
      res[std::string(pc::kCapabilityListChanged)] = true;
    }
    if (resources_subscribe) {
      res[std::string(pc::kCapabilitySubscribe)] = true;
    }
    result[std::string(pc::kCapabilityResources)] = std::move(res);
  }
  if (has_logging) {
    result[std::string(pc::kCapabilityLogging)] = Json::object();
  }
  return result;
}

InitializeRequest InitializeRequest::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("InitializeRequest: expected object");
  }
  const std::string pv_key{pc::kFieldProtocolVersion};
  if (!j.contains(pv_key) || !j[pv_key].is_string()) {
    throw McpProtocolError("InitializeRequest: missing protocolVersion");
  }
  const std::string caps_key{pc::kFieldCapabilities};
  if (!j.contains(caps_key) || !j[caps_key].is_object()) {
    throw McpProtocolError("InitializeRequest: missing capabilities");
  }
  const std::string ci_key{pc::kFieldClientInfo};
  if (!j.contains(ci_key) || !j[ci_key].is_object()) {
    throw McpProtocolError("InitializeRequest: missing clientInfo");
  }
  return InitializeRequest{
      .protocol_version = j[pv_key].get<std::string>(),
      .capabilities = ClientCapabilities::FromJson(j[caps_key]),
      .client_info = ImplementationInfo::FromJson(j[ci_key]),
  };
}

Json InitializeRequest::ToJson() const {
  return Json{
      {std::string(pc::kFieldProtocolVersion), protocol_version},
      {std::string(pc::kFieldCapabilities), capabilities.ToJson()},
      {std::string(pc::kFieldClientInfo), client_info.ToJson()},
  };
}

InitializeResponse InitializeResponse::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("InitializeResponse: expected object");
  }
  const std::string pv_key{pc::kFieldProtocolVersion};
  if (!j.contains(pv_key) || !j[pv_key].is_string()) {
    throw McpProtocolError("InitializeResponse: missing protocolVersion");
  }
  const std::string caps_key{pc::kFieldCapabilities};
  if (!j.contains(caps_key) || !j[caps_key].is_object()) {
    throw McpProtocolError("InitializeResponse: missing capabilities");
  }
  const std::string si_key{pc::kFieldServerInfo};
  if (!j.contains(si_key) || !j[si_key].is_object()) {
    throw McpProtocolError("InitializeResponse: missing serverInfo");
  }
  return InitializeResponse{
      .protocol_version = j[pv_key].get<std::string>(),
      .capabilities = ServerCapabilities::FromJson(j[caps_key]),
      .server_info = ImplementationInfo::FromJson(j[si_key]),
      .instructions = GetOptString(j, pc::kFieldInstructions),
  };
}

Json InitializeResponse::ToJson() const {
  Json result = Json{
      {std::string(pc::kFieldProtocolVersion), protocol_version},
      {std::string(pc::kFieldCapabilities), capabilities.ToJson()},
      {std::string(pc::kFieldServerInfo), server_info.ToJson()},
  };
  if (instructions) {
    result[std::string(pc::kFieldInstructions)] = *instructions;
  }
  return result;
}

ToolDefinition ToolDefinition::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ToolDefinition: expected object");
  }
  Json schema = Json::object();
  const std::string schema_key{pc::kFieldInputSchema};
  if (j.contains(schema_key) && j[schema_key].is_object()) {
    schema = j[schema_key];
  }
  return ToolDefinition{
      .name = GetString(j, pc::kFieldName),
      .description = GetOptString(j, pc::kFieldDescription),
      .input_schema = std::move(schema),
  };
}

Json ToolDefinition::ToJson() const {
  Json result = Json{
      {std::string(pc::kFieldName), name},
      {std::string(pc::kFieldInputSchema), input_schema},
  };
  if (description) {
    result[std::string(pc::kFieldDescription)] = *description;
  }
  return result;
}

ToolsListResponse ToolsListResponse::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ToolsListResponse: expected object");
  }
  ToolsListResponse resp;
  const std::string tools_key{pc::kFieldTools};
  if (j.contains(tools_key) && j[tools_key].is_array()) {
    for (const auto& item : j[tools_key]) {
      resp.tools.push_back(ToolDefinition::FromJson(item));
    }
  }
  resp.next_cursor = GetOptString(j, pc::kFieldNextCursor);
  return resp;
}

Json ToolsListResponse::ToJson() const {
  Json tools_arr = Json::array();
  for (const auto& tool : tools) {
    tools_arr.push_back(tool.ToJson());
  }
  Json result = Json{{std::string(pc::kFieldTools), std::move(tools_arr)}};
  if (next_cursor) {
    result[std::string(pc::kFieldNextCursor)] = *next_cursor;
  }
  return result;
}

ToolsCallRequest ToolsCallRequest::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ToolsCallRequest: expected object");
  }
  Json args = Json::object();
  const std::string args_key{pc::kFieldArguments};
  if (j.contains(args_key) && j[args_key].is_object()) {
    args = j[args_key];
  }
  return ToolsCallRequest{
      .name = GetString(j, pc::kFieldName),
      .arguments = std::move(args),
  };
}

Json ToolsCallRequest::ToJson() const {
  return Json{
      {std::string(pc::kFieldName), name},
      {std::string(pc::kFieldArguments), arguments},
  };
}

McpContentBlock McpContentBlockFromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("McpContentBlock: expected object");
  }
  const std::string type_key{pc::kFieldType};
  if (!j.contains(type_key) || !j[type_key].is_string()) {
    throw McpProtocolError("McpContentBlock: missing type");
  }
  const auto type_val = j[type_key].get<std::string>();
  if (type_val == std::string(pc::kContentTypeText)) {
    return TextContent{.text = GetString(j, pc::kFieldText)};
  }
  if (type_val == std::string(pc::kContentTypeImage)) {
    return ImageContent{
        .data = GetString(j, pc::kFieldData),
        .mime_type = GetString(j, pc::kFieldMimeType),
    };
  }
  if (type_val == std::string(pc::kContentTypeAudio)) {
    return AudioContent{
        .data = GetString(j, pc::kFieldData),
        .mime_type = GetString(j, pc::kFieldMimeType),
    };
  }
  if (type_val == std::string(pc::kContentTypeResource)) {
    const std::string res_key{pc::kContentTypeResource};
    if (!j.contains(res_key) || !j[res_key].is_object()) {
      throw McpProtocolError("resource content: missing resource field");
    }
    const auto& inner = j[res_key];
    return EmbeddedResourceContent{
        .uri = GetString(inner, pc::kFieldUri),
        .mime_type = GetOptString(inner, pc::kFieldMimeType),
        .text = GetOptString(inner, pc::kFieldText),
        .blob = GetOptString(inner, pc::kFieldBlob),
    };
  }
  if (type_val == std::string(pc::kContentTypeResourceLink)) {
    return ResourceLinkContent{
        .uri = GetString(j, pc::kFieldUri),
        .name = GetOptString(j, pc::kFieldName),
        .description = GetOptString(j, pc::kFieldDescription),
        .mime_type = GetOptString(j, pc::kFieldMimeType),
    };
  }
  throw McpProtocolError("McpContentBlock: unknown type: " + type_val);
}

Json McpContentBlockToJson(const McpContentBlock& block) {
  return std::visit(
      [](const auto& content) -> Json {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, TextContent>) {
          return Json{
              {std::string(pc::kFieldType), std::string(pc::kContentTypeText)},
              {std::string(pc::kFieldText), content.text},
          };
        } else if constexpr (std::is_same_v<T, ImageContent>) {
          return Json{
              {std::string(pc::kFieldType), std::string(pc::kContentTypeImage)},
              {std::string(pc::kFieldData), content.data},
              {std::string(pc::kFieldMimeType), content.mime_type},
          };
        } else if constexpr (std::is_same_v<T, AudioContent>) {
          return Json{
              {std::string(pc::kFieldType), std::string(pc::kContentTypeAudio)},
              {std::string(pc::kFieldData), content.data},
              {std::string(pc::kFieldMimeType), content.mime_type},
          };
        } else if constexpr (std::is_same_v<T, EmbeddedResourceContent>) {
          Json inner = Json{{std::string(pc::kFieldUri), content.uri}};
          if (content.mime_type) {
            inner[std::string(pc::kFieldMimeType)] = *content.mime_type;
          }
          if (content.text) {
            inner[std::string(pc::kFieldText)] = *content.text;
          }
          if (content.blob) {
            inner[std::string(pc::kFieldBlob)] = *content.blob;
          }
          return Json{
              {std::string(pc::kFieldType),
               std::string(pc::kContentTypeResource)},
              {std::string(pc::kContentTypeResource), std::move(inner)},
          };
        } else {
          static_assert(std::is_same_v<T, ResourceLinkContent>);
          Json result = Json{
              {std::string(pc::kFieldType),
               std::string(pc::kContentTypeResourceLink)},
              {std::string(pc::kFieldUri), content.uri},
          };
          if (content.name) {
            result[std::string(pc::kFieldName)] = *content.name;
          }
          if (content.description) {
            result[std::string(pc::kFieldDescription)] = *content.description;
          }
          if (content.mime_type) {
            result[std::string(pc::kFieldMimeType)] = *content.mime_type;
          }
          return result;
        }
      },
      block);
}

ToolsCallResponse ToolsCallResponse::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ToolsCallResponse: expected object");
  }
  ToolsCallResponse resp;
  resp.is_error = GetBool(j, pc::kFieldIsError);
  const std::string content_key{pc::kFieldContent};
  if (j.contains(content_key) && j[content_key].is_array()) {
    for (const auto& item : j[content_key]) {
      resp.result_blocks.push_back(McpContentBlockFromJson(item));
    }
  }
  return resp;
}

Json ToolsCallResponse::ToJson() const {
  Json content_arr = Json::array();
  for (const auto& block : result_blocks) {
    content_arr.push_back(McpContentBlockToJson(block));
  }
  return Json{
      {std::string(pc::kFieldContent), std::move(content_arr)},
      {std::string(pc::kFieldIsError), is_error},
  };
}

ResourceDescriptor ResourceDescriptor::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ResourceDescriptor: expected object");
  }
  return ResourceDescriptor{
      .uri = GetString(j, pc::kFieldUri),
      .name = GetOptString(j, pc::kFieldName),
      .description = GetOptString(j, pc::kFieldDescription),
      .mime_type = GetOptString(j, pc::kFieldMimeType),
  };
}

Json ResourceDescriptor::ToJson() const {
  Json result = Json{{std::string(pc::kFieldUri), uri}};
  if (name) {
    result[std::string(pc::kFieldName)] = *name;
  }
  if (description) {
    result[std::string(pc::kFieldDescription)] = *description;
  }
  if (mime_type) {
    result[std::string(pc::kFieldMimeType)] = *mime_type;
  }
  return result;
}

ResourcesListResponse ResourcesListResponse::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ResourcesListResponse: expected object");
  }
  ResourcesListResponse resp;
  const std::string res_key{pc::kFieldResources};
  if (j.contains(res_key) && j[res_key].is_array()) {
    for (const auto& item : j[res_key]) {
      resp.resources.push_back(ResourceDescriptor::FromJson(item));
    }
  }
  resp.next_cursor = GetOptString(j, pc::kFieldNextCursor);
  return resp;
}

Json ResourcesListResponse::ToJson() const {
  Json res_arr = Json::array();
  for (const auto& res : resources) {
    res_arr.push_back(res.ToJson());
  }
  Json result = Json{
      {std::string(pc::kFieldResources), std::move(res_arr)},
  };
  if (next_cursor) {
    result[std::string(pc::kFieldNextCursor)] = *next_cursor;
  }
  return result;
}

ResourcesReadRequest ResourcesReadRequest::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ResourcesReadRequest: expected object");
  }
  return ResourcesReadRequest{.uri = GetString(j, pc::kFieldUri)};
}

Json ResourcesReadRequest::ToJson() const {
  return Json{{std::string(pc::kFieldUri), uri}};
}

ResourceContent ResourceContent::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ResourceContent: expected object");
  }
  return ResourceContent{
      .uri = GetString(j, pc::kFieldUri),
      .mime_type = GetOptString(j, pc::kFieldMimeType),
      .text = GetOptString(j, pc::kFieldText),
      .blob = GetOptString(j, pc::kFieldBlob),
  };
}

Json ResourceContent::ToJson() const {
  Json result = Json{{std::string(pc::kFieldUri), uri}};
  if (mime_type) {
    result[std::string(pc::kFieldMimeType)] = *mime_type;
  }
  if (text) {
    result[std::string(pc::kFieldText)] = *text;
  }
  if (blob) {
    result[std::string(pc::kFieldBlob)] = *blob;
  }
  return result;
}

ResourcesReadResponse ResourcesReadResponse::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ResourcesReadResponse: expected object");
  }
  ResourcesReadResponse resp;
  const std::string contents_key{pc::kFieldContents};
  if (j.contains(contents_key) && j[contents_key].is_array()) {
    for (const auto& item : j[contents_key]) {
      resp.contents.push_back(ResourceContent::FromJson(item));
    }
  }
  return resp;
}

Json ResourcesReadResponse::ToJson() const {
  Json contents_arr = Json::array();
  for (const auto& content : contents) {
    contents_arr.push_back(content.ToJson());
  }
  return Json{
      {std::string(pc::kFieldContents), std::move(contents_arr)},
  };
}

CancelledNotification CancelledNotification::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("CancelledNotification: expected object");
  }
  const std::string req_key{pc::kFieldRequestId};
  if (!j.contains(req_key)) {
    throw McpProtocolError("CancelledNotification: missing requestId");
  }
  return CancelledNotification{
      .request_id = j[req_key],
      .reason = GetOptString(j, pc::kFieldReason),
  };
}

Json CancelledNotification::ToJson() const {
  Json result = Json{{std::string(pc::kFieldRequestId), request_id}};
  if (reason) {
    result[std::string(pc::kFieldReason)] = *reason;
  }
  return result;
}

ProgressNotification ProgressNotification::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("ProgressNotification: expected object");
  }
  const std::string pt_key{pc::kFieldProgressToken};
  if (!j.contains(pt_key)) {
    throw McpProtocolError("ProgressNotification: missing progressToken");
  }
  double progress_val = 0.0;
  const std::string prog_key{pc::kFieldProgress};
  if (j.contains(prog_key) && j[prog_key].is_number()) {
    progress_val = j[prog_key].get<double>();
  }
  return ProgressNotification{
      .progress_token = j[pt_key],
      .progress = progress_val,
      .total = GetOptDouble(j, pc::kFieldTotal),
  };
}

Json ProgressNotification::ToJson() const {
  Json result = Json{
      {std::string(pc::kFieldProgressToken), progress_token},
      {std::string(pc::kFieldProgress), progress},
  };
  if (total) {
    result[std::string(pc::kFieldTotal)] = *total;
  }
  return result;
}

LogMessageNotification LogMessageNotification::FromJson(const Json& j) {
  if (!j.is_object()) {
    throw McpProtocolError("LogMessageNotification: expected object");
  }
  const std::string data_key{pc::kFieldData};
  Json data_val = j.contains(data_key) ? j[data_key] : Json{};
  return LogMessageNotification{
      .level = GetString(j, pc::kFieldLevel),
      .logger = GetOptString(j, pc::kFieldLogger),
      .data = std::move(data_val),
  };
}

Json LogMessageNotification::ToJson() const {
  Json result = Json{
      {std::string(pc::kFieldLevel), level},
      {std::string(pc::kFieldData), data},
  };
  if (logger) {
    result[std::string(pc::kFieldLogger)] = *logger;
  }
  return result;
}

}  // namespace yac::mcp
