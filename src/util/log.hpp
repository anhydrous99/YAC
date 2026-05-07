#pragma once

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace yac::log {

enum class Level { Debug, Info, Warn, Error };

// Pluggable destination for log records. Process-wide; replaceable through
// SetSink. Implementations are responsible for their own thread safety: Write
// may be invoked from arbitrary threads.
struct Sink {
  Sink() = default;
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;
  Sink(Sink&&) = delete;
  Sink& operator=(Sink&&) = delete;
  virtual ~Sink() = default;

  virtual void Write(Level level, std::string_view module,
                     std::string_view message) = 0;
};

// Replace the process-wide sink. Passing nullptr restores the default
// stderr-bound sink. Thread-safe.
void SetSink(std::shared_ptr<Sink> sink);

// Snapshot of the current sink. Thread-safe; the returned shared_ptr keeps
// the sink alive for the duration of any Write call dispatched through it.
std::shared_ptr<Sink> GetSink();

// Mask sensitive substrings (api_key=..., Bearer <token>, code=<oauth-code>,
// token=...) by replacing the value with ***REDACTED*** while keeping the key
// visible. Plain text without any sensitive markers is returned unchanged.
std::string Redact(std::string_view input);

// Describe the currently in-flight exception. Intended to be called from
// inside a catch block. Returns "unknown exception" when the active
// exception does not derive from std::exception.
std::string DescribeCurrentException();

namespace detail {
void Log(Level level, std::string_view module, std::string message);
}  // namespace detail

template <typename... Args>
void Info(std::string_view module, std::format_string<Args...> fmt,
          Args&&... args) {
  detail::Log(Level::Info, module,
              std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Warn(std::string_view module, std::format_string<Args...> fmt,
          Args&&... args) {
  detail::Log(Level::Warn, module,
              std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::string_view module, std::format_string<Args...> fmt,
           Args&&... args) {
  detail::Log(Level::Error, module,
              std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void Debug(std::string_view module, std::format_string<Args...> fmt,
           Args&&... args) {
  detail::Log(Level::Debug, module,
              std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace yac::log
