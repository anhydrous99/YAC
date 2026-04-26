#pragma once

#include <chrono>
#include <string_view>

namespace yac::mcp::protocol {

inline constexpr std::string_view kMcpProtocolVersion = "2025-11-25";

inline constexpr std::string_view kMethodInitialize = "initialize";
inline constexpr std::string_view kMethodInitialized =
    "notifications/initialized";
inline constexpr std::string_view kMethodToolsList = "tools/list";
inline constexpr std::string_view kMethodToolsCall = "tools/call";
inline constexpr std::string_view kMethodResourcesList = "resources/list";
inline constexpr std::string_view kMethodResourcesRead = "resources/read";
inline constexpr std::string_view kMethodNotificationsCancelled =
    "notifications/cancelled";
inline constexpr std::string_view kMethodNotificationsToolsListChanged =
    "notifications/tools/list_changed";
inline constexpr std::string_view kMethodNotificationsResourcesListChanged =
    "notifications/resources/list_changed";
inline constexpr std::string_view kMethodNotificationsProgress =
    "notifications/progress";
inline constexpr std::string_view kMethodNotificationsMessage =
    "notifications/message";
inline constexpr std::string_view kMethodPing = "ping";

inline constexpr std::string_view kJsonRpcVersion = "2.0";
inline constexpr std::string_view kFieldJsonRpc = "jsonrpc";
inline constexpr std::string_view kFieldId = "id";
inline constexpr std::string_view kFieldMethod = "method";
inline constexpr std::string_view kFieldParams = "params";
inline constexpr std::string_view kFieldResult = "result";
inline constexpr std::string_view kFieldError = "error";
inline constexpr std::string_view kFieldCode = "code";
inline constexpr std::string_view kFieldMessage = "message";
inline constexpr std::string_view kFieldData = "data";

inline constexpr std::string_view kFieldProtocolVersion = "protocolVersion";
inline constexpr std::string_view kFieldCapabilities = "capabilities";
inline constexpr std::string_view kFieldClientInfo = "clientInfo";
inline constexpr std::string_view kFieldServerInfo = "serverInfo";
inline constexpr std::string_view kFieldInstructions = "instructions";
inline constexpr std::string_view kFieldName = "name";
inline constexpr std::string_view kFieldVersion = "version";

inline constexpr std::string_view kFieldTools = "tools";
inline constexpr std::string_view kFieldDescription = "description";
inline constexpr std::string_view kFieldInputSchema = "inputSchema";
inline constexpr std::string_view kFieldOutputSchema = "outputSchema";
inline constexpr std::string_view kFieldArguments = "arguments";
inline constexpr std::string_view kFieldContent = "content";
inline constexpr std::string_view kFieldIsError = "isError";
inline constexpr std::string_view kFieldStructuredContent = "structuredContent";
inline constexpr std::string_view kFieldType = "type";

inline constexpr std::string_view kContentTypeText = "text";
inline constexpr std::string_view kContentTypeImage = "image";
inline constexpr std::string_view kContentTypeAudio = "audio";
inline constexpr std::string_view kContentTypeResource = "resource";
inline constexpr std::string_view kContentTypeResourceLink = "resource_link";

inline constexpr std::string_view kFieldResources = "resources";
inline constexpr std::string_view kFieldUri = "uri";
inline constexpr std::string_view kFieldMimeType = "mimeType";
inline constexpr std::string_view kFieldBlob = "blob";
inline constexpr std::string_view kFieldText = "text";

inline constexpr std::string_view kFieldRequestId = "requestId";
inline constexpr std::string_view kFieldReason = "reason";

inline constexpr std::string_view kHeaderMcpSessionId = "Mcp-Session-Id";
inline constexpr std::string_view kHeaderMcpProtocolVersion =
    "MCP-Protocol-Version";
inline constexpr std::string_view kHeaderAuthorization = "Authorization";
inline constexpr std::string_view kHeaderWwwAuthenticate = "WWW-Authenticate";
inline constexpr std::string_view kHeaderLastEventId = "Last-Event-ID";
inline constexpr std::string_view kHeaderContentType = "Content-Type";
inline constexpr std::string_view kContentTypeJson = "application/json";
inline constexpr std::string_view kContentTypeEventStream = "text/event-stream";

inline constexpr int kErrorParse = -32700;
inline constexpr int kErrorInvalidRequest = -32600;
inline constexpr int kErrorMethodNotFound = -32601;
inline constexpr int kErrorInvalidParams = -32602;
inline constexpr int kErrorInternal = -32603;

inline constexpr std::string_view kCapabilityTools = "tools";
inline constexpr std::string_view kCapabilityResources = "resources";
inline constexpr std::string_view kCapabilityLogging = "logging";
inline constexpr std::string_view kCapabilityListChanged = "listChanged";
inline constexpr std::string_view kCapabilitySubscribe = "subscribe";

inline constexpr std::string_view kFieldCursor = "cursor";
inline constexpr std::string_view kFieldNextCursor = "nextCursor";

inline constexpr std::string_view kFieldProgressToken = "progressToken";
inline constexpr std::string_view kFieldProgress = "progress";
inline constexpr std::string_view kFieldTotal = "total";
inline constexpr std::string_view kFieldLevel = "level";
inline constexpr std::string_view kFieldLogger = "logger";
inline constexpr std::string_view kFieldContents = "contents";

inline constexpr std::string_view kCapabilityRoots = "roots";
inline constexpr std::string_view kCapabilitySampling = "sampling";

inline constexpr auto kReconnectInitialDelayMs =
    std::chrono::milliseconds{1000};
inline constexpr auto kReconnectMaxDelayMs = std::chrono::milliseconds{60000};
inline constexpr int kReconnectMaxAttempts = 5;
inline constexpr double kReconnectBackoffMultiplier = 2.0;

static_assert(kMcpProtocolVersion.size() > 0);
static_assert(kMethodInitialize.size() > 0);
static_assert(kMethodInitialized.size() > 0);
static_assert(kMethodToolsList.size() > 0);
static_assert(kMethodToolsCall.size() > 0);
static_assert(kMethodResourcesList.size() > 0);
static_assert(kMethodResourcesRead.size() > 0);
static_assert(kMethodNotificationsCancelled.size() > 0);
static_assert(kMethodNotificationsToolsListChanged.size() > 0);
static_assert(kMethodNotificationsResourcesListChanged.size() > 0);
static_assert(kMethodNotificationsProgress.size() > 0);
static_assert(kMethodNotificationsMessage.size() > 0);
static_assert(kMethodPing.size() > 0);
static_assert(kJsonRpcVersion.size() > 0);
static_assert(kFieldJsonRpc.size() > 0);
static_assert(kFieldId.size() > 0);
static_assert(kFieldMethod.size() > 0);
static_assert(kFieldParams.size() > 0);
static_assert(kFieldResult.size() > 0);
static_assert(kFieldError.size() > 0);
static_assert(kFieldCode.size() > 0);
static_assert(kFieldMessage.size() > 0);
static_assert(kFieldData.size() > 0);
static_assert(kFieldProtocolVersion.size() > 0);
static_assert(kFieldCapabilities.size() > 0);
static_assert(kFieldClientInfo.size() > 0);
static_assert(kFieldServerInfo.size() > 0);
static_assert(kFieldInstructions.size() > 0);
static_assert(kFieldName.size() > 0);
static_assert(kFieldVersion.size() > 0);
static_assert(kFieldTools.size() > 0);
static_assert(kFieldDescription.size() > 0);
static_assert(kFieldInputSchema.size() > 0);
static_assert(kFieldOutputSchema.size() > 0);
static_assert(kFieldArguments.size() > 0);
static_assert(kFieldContent.size() > 0);
static_assert(kFieldIsError.size() > 0);
static_assert(kFieldStructuredContent.size() > 0);
static_assert(kFieldType.size() > 0);
static_assert(kContentTypeText.size() > 0);
static_assert(kContentTypeImage.size() > 0);
static_assert(kContentTypeAudio.size() > 0);
static_assert(kContentTypeResource.size() > 0);
static_assert(kContentTypeResourceLink.size() > 0);
static_assert(kFieldResources.size() > 0);
static_assert(kFieldUri.size() > 0);
static_assert(kFieldMimeType.size() > 0);
static_assert(kFieldBlob.size() > 0);
static_assert(kFieldText.size() > 0);
static_assert(kFieldRequestId.size() > 0);
static_assert(kFieldReason.size() > 0);
static_assert(kHeaderMcpSessionId.size() > 0);
static_assert(kHeaderMcpProtocolVersion.size() > 0);
static_assert(kHeaderAuthorization.size() > 0);
static_assert(kHeaderWwwAuthenticate.size() > 0);
static_assert(kHeaderLastEventId.size() > 0);
static_assert(kHeaderContentType.size() > 0);
static_assert(kContentTypeJson.size() > 0);
static_assert(kContentTypeEventStream.size() > 0);
static_assert(kErrorParse < 0);
static_assert(kErrorInvalidRequest < 0);
static_assert(kErrorMethodNotFound < 0);
static_assert(kErrorInvalidParams < 0);
static_assert(kErrorInternal < 0);
static_assert(kCapabilityTools.size() > 0);
static_assert(kCapabilityResources.size() > 0);
static_assert(kCapabilityLogging.size() > 0);
static_assert(kCapabilityListChanged.size() > 0);
static_assert(kCapabilitySubscribe.size() > 0);
static_assert(kFieldCursor.size() > 0);
static_assert(kFieldNextCursor.size() > 0);
static_assert(kFieldProgressToken.size() > 0);
static_assert(kFieldProgress.size() > 0);
static_assert(kFieldTotal.size() > 0);
static_assert(kFieldLevel.size() > 0);
static_assert(kFieldLogger.size() > 0);
static_assert(kFieldContents.size() > 0);
static_assert(kCapabilityRoots.size() > 0);
static_assert(kCapabilitySampling.size() > 0);
static_assert(kReconnectInitialDelayMs.count() > 0);
static_assert(kReconnectMaxDelayMs > kReconnectInitialDelayMs);
static_assert(kReconnectMaxAttempts > 0);
static_assert(kReconnectBackoffMultiplier > 1.0);

}  // namespace yac::mcp::protocol
