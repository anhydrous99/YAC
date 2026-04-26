#include "tool_call/json_rpc_stdio_base.hpp"

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::JsonRpcStdioBase;
using Json = JsonRpcStdioBase::Json;

static constexpr std::size_t kMaxTestFrameBytes = 64UL * 1024UL * 1024UL;

class ContentLengthClient : public JsonRpcStdioBase {
 public:
  ContentLengthClient() : JsonRpcStdioBase("test") {}
  ~ContentLengthClient() override { Stop(); }
  ContentLengthClient(const ContentLengthClient&) = delete;
  ContentLengthClient& operator=(const ContentLengthClient&) = delete;
  ContentLengthClient(ContentLengthClient&&) = delete;
  ContentLengthClient& operator=(ContentLengthClient&&) = delete;

  std::vector<std::string> notification_methods;
  std::atomic<int> notification_count{0};

  void PublicStart(const std::string& command,
                   const std::vector<std::string>& args) {
    Start(command, args);
  }

  Json PublicSendRequest(std::string_view method, const Json& params,
                         std::chrono::milliseconds timeout) {
    return SendRequest(method, params, timeout);
  }

 protected:
  void WriteFrame(const std::string& body) override {
    const auto header =
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    WriteBytes(header);
    WriteBytes(body);
  }

  std::optional<std::string> ReadFrame() override {
    size_t content_length = 0;
    while (true) {
      auto line = ReadLine();
      if (!line) {
        return std::nullopt;
      }
      if (line->empty()) {
        break;
      }
      constexpr std::string_view kHeader = "Content-Length:";
      if (line->rfind(kHeader, 0) == 0) {
        try {
          const auto parsed =
              std::stoul(std::string(line->substr(kHeader.size())));
          if (parsed > kMaxTestFrameBytes) {
            return std::nullopt;
          }
          content_length = parsed;
        } catch (...) {
          return std::nullopt;
        }
      }
    }
    if (content_length == 0) {
      return std::string{};
    }
    std::string body(content_length, '\0');
    size_t read_count = 0;
    while (read_count < content_length) {
      const auto result =
          read(ReadFd(), body.data() + read_count, content_length - read_count);
      if (result <= 0) {
        return std::nullopt;
      }
      read_count += static_cast<size_t>(result);
    }
    return body;
  }

  void OnNotification(std::string_view method,
                      const Json& /*params*/) override {
    notification_methods.emplace_back(method);
    notification_count.fetch_add(1, std::memory_order_release);
  }

 private:
  [[nodiscard]] std::optional<std::string> ReadLine() const {
    std::string line;
    char ch = '\0';
    while (true) {
      const auto result = read(ReadFd(), &ch, 1);
      if (result <= 0) {
        return std::nullopt;
      }
      if (ch == '\n') {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        return line;
      }
      line.push_back(ch);
    }
  }
};

TEST_CASE("stop_faults_pending") {
  ContentLengthClient client;
  client.PublicStart("/bin/false", {});

  const auto start = std::chrono::steady_clock::now();
  bool threw = false;
  try {
    client.PublicSendRequest("test/method", Json::object(),
                             std::chrono::milliseconds(5000));
  } catch (const std::exception&) {
    threw = true;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(threw);
  REQUIRE(elapsed < std::chrono::milliseconds(2000));
}

TEST_CASE("request_response_correlation") {
  const std::string response_body =
      R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})";
  const std::string server_cmd =
      "printf 'Content-Length: " + std::to_string(response_body.size()) +
      R"(\r\n\r\n)" + response_body + "'";

  ContentLengthClient client;
  client.PublicStart("/bin/sh", {"-c", server_cmd});

  bool threw = false;
  Json result;
  try {
    result = client.PublicSendRequest("test/method", Json::object(),
                                      std::chrono::milliseconds(3000));
  } catch (const std::exception&) {
    threw = true;
  }

  REQUIRE_FALSE(threw);
  REQUIRE(result.contains("result"));
  REQUIRE(result["result"]["ok"] == true);
}

TEST_CASE("notification_dispatched") {
  const std::string notif_body =
      R"({"jsonrpc":"2.0","method":"test/event","params":{"x":1}})";
  const std::string server_cmd =
      "printf 'Content-Length: " + std::to_string(notif_body.size()) +
      R"(\r\n\r\n)" + notif_body + "'";

  ContentLengthClient client;
  client.PublicStart("/bin/sh", {"-c", server_cmd});

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (client.notification_count.load(std::memory_order_acquire) == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE(client.notification_count.load() >= 1);
  REQUIRE(client.notification_methods.front() == "test/event");
}
