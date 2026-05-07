#include "util/log.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace yac::log {

namespace {

constexpr std::string_view kRedactedPlaceholder = "***REDACTED***";

std::string_view LevelLabel(Level level) {
  switch (level) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO";
    case Level::Warn:
      return "WARN";
    case Level::Error:
      return "ERROR";
  }
  return "INFO";
}

class CerrSink final : public Sink {
 public:
  void Write(Level level, std::string_view module,
             std::string_view message) override {
    std::scoped_lock lock(mutex_);
    std::cerr << '[' << LevelLabel(level) << "] [" << module << "] " << message
              << '\n';
  }

 private:
  std::mutex mutex_;
};

std::mutex& SinkMutex() {
  static std::mutex m;
  return m;
}

std::shared_ptr<Sink>& SinkSlot() {
  static std::shared_ptr<Sink> slot = std::make_shared<CerrSink>();
  return slot;
}

}  // namespace

void SetSink(std::shared_ptr<Sink> sink) {
  std::scoped_lock lock(SinkMutex());
  if (sink) {
    SinkSlot() = std::move(sink);
  } else {
    SinkSlot() = std::make_shared<CerrSink>();
  }
}

std::shared_ptr<Sink> GetSink() {
  std::scoped_lock lock(SinkMutex());
  return SinkSlot();
}

std::string Redact(std::string_view input) {
  // Patterns target the four shapes that leak secrets in YAC's logs:
  // OpenAI-style "api_key=..." query strings, "Bearer <jwt>" auth headers,
  // OAuth "code=<authcode>" callback URLs, and generic "token=..." form
  // parameters. The character class stops at terminators that bound the
  // value in URLs (whitespace, `&`, `,`, `;`, `"`) so following structure
  // is preserved.
  static const std::regex api_key_regex(R"(api_key=[^\s&,;"]+)",
                                        std::regex::ECMAScript);
  static const std::regex bearer_regex(R"(Bearer[ \t]+[^\s,"]+)",
                                       std::regex::ECMAScript);
  static const std::regex code_regex(R"(\bcode=[^\s&,;"]+)",
                                     std::regex::ECMAScript);
  static const std::regex token_regex(R"(token=[^\s&,;"]+)",
                                      std::regex::ECMAScript);

  const std::string redacted(kRedactedPlaceholder);
  std::string text(input);
  text = std::regex_replace(text, api_key_regex, "api_key=" + redacted);
  text = std::regex_replace(text, bearer_regex, "Bearer " + redacted);
  text = std::regex_replace(text, code_regex, "code=" + redacted);
  text = std::regex_replace(text, token_regex, "token=" + redacted);
  return text;
}

std::string DescribeCurrentException() {
  try {
    throw;
  } catch (const std::exception& e) {
    return e.what();
  } catch (...) {
    return "unknown exception";
  }
}

namespace detail {

void Log(Level level, std::string_view module, std::string message) {
  if (auto sink = GetSink()) {
    sink->Write(level, module, message);
  }
}

}  // namespace detail

}  // namespace yac::log
