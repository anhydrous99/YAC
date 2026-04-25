#include "message.hpp"

#include <utility>

namespace yac::presentation {

Message::Message(Sender sender, std::string content, std::string role_label,
                 std::string timestamp)
    : sender(sender),
      role_label(std::move(role_label)),
      timestamp(std::move(timestamp)) {
  segments.emplace_back(TextSegment{std::move(content)});
}

void Message::AppendText(std::string delta) {
  if (segments.empty() ||
      !std::holds_alternative<TextSegment>(segments.back())) {
    segments.emplace_back(TextSegment{std::move(delta)});
    return;
  }
  std::get<TextSegment>(segments.back()).text += std::move(delta);
}

std::string Message::CombinedText() const {
  std::string out;
  for (const auto& segment : segments) {
    if (const auto* text = std::get_if<TextSegment>(&segment)) {
      out += text->text;
    }
  }
  return out;
}

ToolSegment* Message::FindToolSegment(MessageId tool_id) {
  for (auto& segment : segments) {
    if (auto* tool = std::get_if<ToolSegment>(&segment)) {
      if (tool->id == tool_id) {
        return tool;
      }
    }
  }
  return nullptr;
}

const ToolSegment* Message::FindToolSegment(MessageId tool_id) const {
  for (const auto& segment : segments) {
    if (const auto* tool = std::get_if<ToolSegment>(&segment)) {
      if (tool->id == tool_id) {
        return tool;
      }
    }
  }
  return nullptr;
}

std::string Message::DisplayLabel() const {
  if (!role_label.empty()) {
    return role_label;
  }
  switch (sender) {
    case Sender::User:
      return "You";
    case Sender::Agent:
      return "Assistant";
  }
  return "Unknown";
}

}  // namespace yac::presentation
