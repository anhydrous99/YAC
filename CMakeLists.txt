cmake_minimum_required(VERSION 3.16)
project(YAC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(
  openai_cpp
  GIT_REPOSITORY https://github.com/olrea/openai-cpp.git
  GIT_TAG main
)
FetchContent_GetProperties(openai_cpp)
if(NOT openai_cpp_POPULATED)
  FetchContent_Populate(openai_cpp)
endif()

add_library(openai_cpp INTERFACE)
target_include_directories(openai_cpp INTERFACE
  ${openai_cpp_SOURCE_DIR}/include/openai
)

find_package(CURL REQUIRED)

add_library(yac_presentation STATIC
  src/presentation/chat_session.cpp
  src/presentation/chat_ui.cpp
  src/presentation/composer_state.cpp
  src/presentation/command_palette.cpp
  src/presentation/collapsible.cpp
  src/presentation/dialog.cpp
  src/presentation/message.cpp
  src/presentation/message_renderer.cpp
  src/presentation/markdown/parser.cpp
  src/presentation/markdown/renderer.cpp
  src/presentation/slash_command_registry.cpp
  src/presentation/syntax/highlighter.cpp
  src/presentation/theme.cpp
  src/presentation/tool_call/renderer.cpp
)
target_include_directories(yac_presentation PUBLIC src)
target_link_libraries(yac_presentation PUBLIC ftxui::screen ftxui::dom ftxui::component)

add_library(yac_service STATIC
  src/chat/chat_service.cpp
  src/chat/config.cpp
  src/chat/env_file.cpp
  src/provider/openai_chat_provider.cpp
  src/provider/provider_registry.cpp
)
target_include_directories(yac_service PUBLIC src)
target_link_libraries(yac_service PUBLIC openai_cpp CURL::libcurl)

add_executable(yac src/main.cpp)
target_link_libraries(yac PRIVATE yac_presentation yac_service)

enable_testing()
add_subdirectory(tests)

file(GLOB_RECURSE ALL_SOURCES
  src/*.cpp src/*.hpp
  tests/*.cpp tests/*.hpp
)

add_custom_target(format
  COMMAND clang-format -i ${ALL_SOURCES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Formatting source files with clang-format"
)

add_custom_target(lint
  COMMAND clang-tidy -p ${CMAKE_BINARY_DIR} "-header-filter=^(src|tests)/" ${ALL_SOURCES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-tidy checks"
  VERBATIM
)
