#pragma once

#include <string>
#include <string_view>

namespace yac::tool_call {

[[nodiscard]] std::string TransformWebFetchBody(std::string_view body,
                                                std::string_view content_type,
                                                std::string_view format);

}  // namespace yac::tool_call
