#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <openai.hpp>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace yac::tool_call {

class JsonRpcStdioBase {
 public:
  using Json = openai::_detail::Json;

  explicit JsonRpcStdioBase(std::string error_label = "JSON-RPC");
  virtual ~JsonRpcStdioBase();

  JsonRpcStdioBase(const JsonRpcStdioBase&) = delete;
  JsonRpcStdioBase& operator=(const JsonRpcStdioBase&) = delete;
  JsonRpcStdioBase(JsonRpcStdioBase&&) = delete;
  JsonRpcStdioBase& operator=(JsonRpcStdioBase&&) = delete;

 protected:
  void Start(const std::string& command, const std::vector<std::string>& args);
  void Stop();

  [[nodiscard]] Json SendRequest(std::string_view method, Json params,
                                 std::chrono::milliseconds timeout);
  void SendNotification(std::string_view method, Json params);
  void FaultAllPending(std::string_view reason);

  void WriteBytes(std::string_view bytes) const;
  [[nodiscard]] int ReadFd() const { return read_fd_; }

  virtual void WriteFrame(const std::string& body) = 0;
  virtual std::optional<std::string> ReadFrame() = 0;
  virtual void OnNotification(std::string_view method, const Json& params) = 0;

 private:
  void SendMessage(const Json& message);
  void ReaderLoop(std::stop_token stop_token);
  void ProcessMessage(const std::string& body);

  std::string error_label_;
  std::atomic<int> next_id_{1};
  std::mutex mutex_;
  std::condition_variable_any response_wake_;
  std::map<int, Json> responses_;
  std::jthread reader_;
  int read_fd_ = -1;
  int write_fd_ = -1;
  pid_t child_pid_ = -1;
  bool reader_alive_ = true;
  std::string reader_death_reason_;
};

}  // namespace yac::tool_call
