#pragma once

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"

#include <cstdint>
#include <functional>

namespace yac::presentation::detail {

struct DynamicMessageStackViewport {
  int scroll_offset_y = 0;
  int viewport_height = 0;
};

ftxui::Component MakeDynamicMessageStack(
    std::function<std::uint64_t()> get_generation,
    std::function<ftxui::Components()> get_children,
    std::function<DynamicMessageStackViewport()> get_viewport,
    std::function<bool()> get_active_tail_dirty);

ftxui::Component MakeSlashMenuInputWrapper(
    ftxui::Component input,
    std::function<bool(const ftxui::Event&)> pre_handler,
    std::function<void()> post_handler);

}  // namespace yac::presentation::detail
