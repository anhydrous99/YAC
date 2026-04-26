#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp {

struct SseEvent {
  std::string id;
  std::string event;
  std::string data;
  int retry = -1;
};

class SseParser {
 public:
  [[nodiscard]] std::vector<SseEvent> FeedChunk(std::string_view chunk);

 private:
  void ConsumeLine(std::string_view line, std::vector<SseEvent>& events);
  void FlushEvent(std::vector<SseEvent>& events);

  std::string buffer_;
  SseEvent pending_event_;
  bool has_pending_field_ = false;
};

}  // namespace yac::mcp
