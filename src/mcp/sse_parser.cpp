#include "mcp/sse_parser.hpp"

#include <charconv>
#include <string>
#include <utility>
#include <vector>

namespace yac::mcp {
namespace {
// Mirror the stdio transport's frame cap so a server streaming a long run of
// bytes with no newline cannot grow buffer_ without bound.
constexpr std::size_t kMaxLineBytes = 64UL * 1024UL * 1024UL;
}  // namespace

std::vector<SseEvent> SseParser::FeedChunk(std::string_view chunk) {
  buffer_.append(chunk.data(), chunk.size());

  std::vector<SseEvent> events;
  std::size_t newline_pos = 0;
  while ((newline_pos = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, newline_pos);
    buffer_.erase(0, newline_pos + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    ConsumeLine(line, events);
  }

  // Oversized partial line with no delimiter: drop it to bound memory.
  if (buffer_.size() > kMaxLineBytes) {
    buffer_.clear();
  }
  return events;
}

void SseParser::ConsumeLine(std::string_view line,
                            std::vector<SseEvent>& events) {
  if (line.empty()) {
    FlushEvent(events);
    return;
  }
  if (line.starts_with(':')) {
    return;
  }

  const std::size_t colon_pos = line.find(':');
  const std::string_view field = line.substr(0, colon_pos);
  std::string_view value;
  if (colon_pos != std::string::npos) {
    value = line.substr(colon_pos + 1);
    if (!value.empty() && value.front() == ' ') {
      value.remove_prefix(1);
    }
  }

  if (field == "data") {
    if (!pending_event_.data.empty()) {
      pending_event_.data.push_back('\n');
    }
    pending_event_.data.append(value.data(), value.size());
    has_pending_field_ = true;
    return;
  }

  if (field == "id") {
    pending_event_.id.assign(value.data(), value.size());
    has_pending_field_ = true;
    return;
  }

  if (field == "event") {
    pending_event_.event.assign(value.data(), value.size());
    has_pending_field_ = true;
    return;
  }

  if (field == "retry") {
    int retry = -1;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    if (const auto [ptr, error] = std::from_chars(begin, end, retry);
        error == std::errc{} && ptr == end) {
      pending_event_.retry = retry;
      has_pending_field_ = true;
    }
  }
}

void SseParser::FlushEvent(std::vector<SseEvent>& events) {
  if (!has_pending_field_) {
    return;
  }
  events.push_back(std::move(pending_event_));
  pending_event_ = SseEvent{};
  has_pending_field_ = false;
}

}  // namespace yac::mcp
