#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;

enum class CrashAfter { Never, Initialize, ToolCall };

struct Options {
  int advertise_tools = 2;
  int advertise_resources = 0;
  int init_delay_ms = 0;
  int tool_call_blocks_for_ms = 0;
  CrashAfter crash_after = CrashAfter::Never;
  std::string protocol_version = "2025-11-25";
  std::optional<std::string> log_frames_to;
};

[[nodiscard]] int ParseInt(std::string_view value) {
  return std::atoi(std::string(value).c_str());
}

[[nodiscard]] int ParseDurationMs(std::string_view value) {
  if (value.size() >= 2 && value.substr(value.size() - 2) == "ms") {
    return ParseInt(value.substr(0, value.size() - 2));
  }
  return ParseInt(value);
}

[[nodiscard]] CrashAfter ParseCrashAfter(std::string_view value) {
  if (value == "initialize") {
    return CrashAfter::Initialize;
  }
  if (value == "tool_call") {
    return CrashAfter::ToolCall;
  }
  return CrashAfter::Never;
}
[[nodiscard]] bool TryConsumeFlag(std::string_view arg, std::string_view prefix,
                                  std::string_view& out_value) {
  if (!arg.starts_with(prefix)) {
    return false;
  }
  out_value = arg.substr(prefix.size());
  return true;
}

[[nodiscard]] Options ParseOptions(int argc, char** argv) {
  Options options;
  std::string_view value;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (TryConsumeFlag(arg, "--advertise-tools=", value)) {
      options.advertise_tools = ParseInt(value);
    } else if (TryConsumeFlag(arg, "--advertise-resources=", value)) {
      options.advertise_resources = ParseInt(value);
    } else if (TryConsumeFlag(arg, "--init-delay=", value)) {
      options.init_delay_ms = ParseDurationMs(value);
    } else if (TryConsumeFlag(arg, "--tool-call-blocks-for=", value)) {
      options.tool_call_blocks_for_ms = ParseDurationMs(value);
    } else if (TryConsumeFlag(arg, "--crash-after=", value)) {
      options.crash_after = ParseCrashAfter(value);
    } else if (TryConsumeFlag(arg, "--protocol-version=", value)) {
      options.protocol_version = std::string(value);
    } else if (TryConsumeFlag(arg, "--log-frames-to=", value)) {
      options.log_frames_to = std::string(value);
    }
  }
  return options;
}

void LogFrame(std::ofstream* log_stream, std::string_view frame) {
  if (log_stream == nullptr) {
    return;
  }
  *log_stream << frame << '\n';
  log_stream->flush();
}

void WriteResponse(const Json& response) {
  std::cout << response.dump() << '\n' << std::flush;
}

[[nodiscard]] Json MakeServerInfo() {
  return Json{{"name", "fake_mcp_server"}, {"version", "0.1.0"}};
}

[[nodiscard]] Json MakeServerCapabilities(int tool_count, int resource_count) {
  Json caps = Json::object();
  if (tool_count > 0) {
    caps["tools"] = Json::object();
  }
  if (resource_count > 0) {
    caps["resources"] = Json::object();
  }
  return caps;
}

[[nodiscard]] Json MakeToolsListResult(int count) {
  Json tools = Json::array();
  for (int i = 0; i < count; ++i) {
    const std::string suffix(1, static_cast<char>('a' + i));
    tools.push_back(Json{
        {"name", "tool_" + suffix},
        {"description", "Fake tool " + suffix},
        {"inputSchema",
         Json{{"type", "object"}, {"properties", Json::object()}}},
    });
  }
  return Json{{"tools", std::move(tools)}};
}

[[nodiscard]] Json MakeResourcesListResult(int count) {
  Json resources = Json::array();
  for (int i = 0; i < count; ++i) {
    const std::string idx = std::to_string(i);
    resources.push_back(Json{
        {"uri", "fake://resource_" + idx},
        {"name", "resource_" + idx},
        {"mimeType", "text/plain"},
    });
  }
  return Json{{"resources", std::move(resources)}};
}

[[nodiscard]] Json MakeToolsCallResult(const std::string& tool_name) {
  return Json{
      {"content", Json::array({Json{
                      {"type", "text"},
                      {"text", "Fake result from " + tool_name},
                  }})},
      {"isError", false},
  };
}

[[nodiscard]] Json MakeErrorResponse(const Json& id, int code,
                                     std::string_view message) {
  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error", Json{{"code", code}, {"message", std::string(message)}}},
  };
}

[[nodiscard]] Json MakeResultResponse(const Json& id, Json result) {
  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"result", std::move(result)},
  };
}

void HandleInitialize(const Options& options, const Json& id) {
  if (options.init_delay_ms > 0) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(options.init_delay_ms));
  }
  Json result{
      {"protocolVersion", options.protocol_version},
      {"capabilities", MakeServerCapabilities(options.advertise_tools,
                                              options.advertise_resources)},
      {"serverInfo", MakeServerInfo()},
  };
  WriteResponse(MakeResultResponse(id, std::move(result)));
}

void HandleToolsCall(const Options& options, const Json& id,
                     const Json& params) {
  const std::string tool_name = params.value("name", std::string("tool_a"));
  if (options.tool_call_blocks_for_ms > 0) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(options.tool_call_blocks_for_ms));
  }
  WriteResponse(MakeResultResponse(id, MakeToolsCallResult(tool_name)));
}

[[nodiscard]] int RunServer(const Options& options,
                            std::ofstream* log_stream_ptr) {
  std::string line;
  while (std::getline(std::cin, line)) {
    LogFrame(log_stream_ptr, line);

    Json request;
    try {
      request = Json::parse(line);
    } catch (const std::exception&) {
      continue;
    }

    if (!request.is_object() || !request.contains("method") ||
        !request["method"].is_string()) {
      continue;
    }

    const std::string method = request["method"].get<std::string>();
    const Json params =
        request.contains("params") ? request["params"] : Json::object();

    if (!request.contains("id")) {
      continue;
    }

    const Json& id = request["id"];

    if (method == "initialize") {
      HandleInitialize(options, id);
      if (options.crash_after == CrashAfter::Initialize) {
        return 1;
      }
    } else if (method == "tools/list") {
      WriteResponse(
          MakeResultResponse(id, MakeToolsListResult(options.advertise_tools)));
    } else if (method == "resources/list") {
      WriteResponse(MakeResultResponse(
          id, MakeResourcesListResult(options.advertise_resources)));
    } else if (method == "tools/call") {
      HandleToolsCall(options, id, params);
      if (options.crash_after == CrashAfter::ToolCall) {
        return 1;
      }
    } else if (method == "ping") {
      WriteResponse(MakeResultResponse(id, Json::object()));
    } else {
      WriteResponse(
          MakeErrorResponse(id, -32601, "Method not found: " + method));
    }
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);

    std::ofstream log_stream;
    std::ofstream* log_stream_ptr = nullptr;
    if (options.log_frames_to.has_value()) {
      log_stream.open(*options.log_frames_to, std::ios::app);
      log_stream_ptr = &log_stream;
    }

    return RunServer(options, log_stream_ptr);
  } catch (const std::exception& e) {
    std::cerr << "fake_mcp_server: fatal: " << e.what() << '\n';
    return 2;
  } catch (...) {
    std::cerr << "fake_mcp_server: fatal: unknown exception\n";
    return 2;
  }
}
