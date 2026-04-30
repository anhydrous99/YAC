#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

struct Options {
  bool slow_mode = false;
  std::optional<std::string> log_frames_to;
};

[[nodiscard]] Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--slow-mode") {
      options.slow_mode = true;
      continue;
    }

    constexpr std::string_view kLogPrefix = "--log-frames-to=";
    if (arg.starts_with(kLogPrefix)) {
      options.log_frames_to = std::string(arg.substr(kLogPrefix.size()));
    }
  }
  return options;
}

void LogFrame(std::ofstream* log_stream, std::mutex* log_mutex,
              std::string_view frame) {
  if (log_stream == nullptr) {
    return;
  }

  std::scoped_lock lock(*log_mutex);
  *log_stream << frame << '\n';
  log_stream->flush();
}

void WriteResponse(std::mutex* write_mutex, const Json& response) {
  std::scoped_lock lock(*write_mutex);
  std::cout << response.dump() << '\n' << std::flush;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = ParseOptions(argc, argv);

  std::ofstream log_stream;
  std::ofstream* log_stream_ptr = nullptr;
  if (options.log_frames_to.has_value()) {
    log_stream.open(*options.log_frames_to, std::ios::app);
    log_stream_ptr = &log_stream;
  }

  std::mutex log_mutex;
  std::mutex write_mutex;
  std::vector<std::jthread> delayed_writers;
  std::string line;
  while (std::getline(std::cin, line)) {
    LogFrame(log_stream_ptr, &log_mutex, line);

    Json request;
    try {
      request = Json::parse(line);
    } catch (const std::exception&) {
      continue;
    }

    if (!request.contains("id")) {
      continue;
    }

    Json response = {
        {"jsonrpc", "2.0"},
        {"id", request["id"]},
        {"result",
         {{"ok", true},
          {"echoMethod", request.value("method", "")},
          {"echoParams", request.value("params", Json::object())}}},
    };

    if (!options.slow_mode) {
      WriteResponse(&write_mutex, response);
      continue;
    }

    delayed_writers.emplace_back([response = std::move(response),
                                  &write_mutex](std::stop_token stop_token) {
      std::this_thread::sleep_for(2s);
      if (stop_token.stop_requested()) {
        return;
      }
      WriteResponse(&write_mutex, response);
    });
  }

  return 0;
}
