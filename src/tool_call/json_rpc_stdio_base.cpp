#include "tool_call/json_rpc_stdio_base.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace yac::tool_call {

namespace {

void CloseFd(int* fd) {
  if (*fd < 0) {
    return;
  }
  close(*fd);
  *fd = -1;
}

// Ignore SIGPIPE process-wide so that writing to a pipe whose reader has
// closed (e.g. a stdio child that exited before consuming the request)
// returns EPIPE from write() rather than terminating the process. The
// throwing branch in WriteBytes then surfaces it as a runtime_error.
[[maybe_unused]] const auto kIgnoreSigpipeOnce = [] {
  std::signal(SIGPIPE, SIG_IGN);
  return 0;
}();

}  // namespace

JsonRpcStdioBase::JsonRpcStdioBase(std::string error_label)
    : error_label_(std::move(error_label)) {}

JsonRpcStdioBase::~JsonRpcStdioBase() {
  Stop();
}

void JsonRpcStdioBase::Start(const std::string& command,
                             const std::vector<std::string>& args) {
  if (child_pid_ > 0) {
    return;
  }

  std::array<int, 2> stdin_pipe{};
  std::array<int, 2> stdout_pipe{};
  if (pipe(stdin_pipe.data()) != 0) {
    throw std::runtime_error("Unable to create " + error_label_ + " pipes.");
  }
  if (pipe(stdout_pipe.data()) != 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    throw std::runtime_error("Unable to create " + error_label_ + " pipes.");
  }

  child_pid_ = fork();
  if (child_pid_ < 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    throw std::runtime_error("Unable to fork " + error_label_ + " server.");
  }

  if (child_pid_ == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    std::vector<std::string> argv_storage;
    argv_storage.push_back(command);
    argv_storage.insert(argv_storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& item : argv_storage) {
      argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  write_fd_ = stdin_pipe[1];
  read_fd_ = stdout_pipe[0];
  {
    std::scoped_lock lock(mutex_);
    reader_alive_ = true;
    reader_death_reason_.clear();
    responses_.clear();
  }
  reader_ = std::jthread(
      [this](std::stop_token stop_token) { ReaderLoop(stop_token); });
}

void JsonRpcStdioBase::Stop() {
  if (reader_.joinable()) {
    reader_.request_stop();
  }

  CloseFd(&read_fd_);
  CloseFd(&write_fd_);

  if (child_pid_ > 0) {
    kill(child_pid_, SIGTERM);
    waitpid(child_pid_, nullptr, 0);
    child_pid_ = -1;
  }

  if (reader_.joinable()) {
    reader_.join();
  }
}

JsonRpcStdioBase::Json JsonRpcStdioBase::SendRequest(
    std::string_view method, Json params, std::chrono::milliseconds timeout) {
  const auto id = next_id_.fetch_add(1);
  SendMessage({{"jsonrpc", "2.0"},
               {"id", id},
               {"method", method},
               {"params", std::move(params)}});

  std::unique_lock lock(mutex_);
  const bool ready = response_wake_.wait_for(
      lock, timeout, [&] { return responses_.contains(id) || !reader_alive_; });
  if (!ready) {
    throw std::runtime_error(std::string(error_label_) +
                             " request timed out: " + std::string(method));
  }

  auto it = responses_.find(id);
  if (it == responses_.end()) {
    throw std::runtime_error(std::string(error_label_) + " request aborted: " +
                             std::string(method) + ": " + reader_death_reason_);
  }

  auto response = std::move(it->second);
  responses_.erase(it);
  if (response.contains("error")) {
    throw std::runtime_error(response["error"].dump());
  }
  return response;
}

void JsonRpcStdioBase::SendNotification(std::string_view method, Json params) {
  SendMessage(
      {{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

void JsonRpcStdioBase::FaultAllPending(std::string_view reason) {
  {
    std::scoped_lock lock(mutex_);
    if (!reader_alive_) {
      return;
    }
    reader_alive_ = false;
    reader_death_reason_ = std::string(reason);
  }
  response_wake_.notify_all();
}

void JsonRpcStdioBase::WriteBytes(std::string_view bytes) const {
  size_t written = 0;
  while (written < bytes.size()) {
    const auto result =
        write(write_fd_, bytes.data() + written, bytes.size() - written);
    if (result < 0) {
      throw std::runtime_error("Failed to write to " + error_label_ +
                               " server: " + std::string(std::strerror(errno)));
    }
    written += static_cast<size_t>(result);
  }
}

void JsonRpcStdioBase::SendMessage(const Json& message) {
  WriteFrame(message.dump());
}

void JsonRpcStdioBase::ReaderLoop(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    auto message = ReadFrame();
    if (!message.has_value()) {
      FaultAllPending(stop_token.stop_requested()
                          ? error_label_ + " client shut down"
                          : error_label_ + " server disconnected");
      return;
    }
    ProcessMessage(*message);
  }

  FaultAllPending(error_label_ + " client shut down");
}

void JsonRpcStdioBase::ProcessMessage(const std::string& body) {
  Json message;
  try {
    message = Json::parse(body);
  } catch (const std::exception&) {
    return;
  }

  if (message.contains("id")) {
    const auto id = message["id"].get<int>();
    {
      std::scoped_lock lock(mutex_);
      responses_[id] = std::move(message);
    }
    response_wake_.notify_all();
    return;
  }

  if (!message.contains("method") || !message["method"].is_string()) {
    return;
  }

  const Json params =
      message.contains("params") ? message["params"] : Json::object();
  OnNotification(message["method"].get<std::string>(), params);
}

}  // namespace yac::tool_call
