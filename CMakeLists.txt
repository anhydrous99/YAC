cmake_minimum_required(VERSION 3.18)
project(YAC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Apple's libc++ keeps C++20 jthread / stop_token / stop_callback (P0660R10)
# behind -fexperimental-library through libc++ 19; YAC relies on these.
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fexperimental-library)
  add_link_options(-fexperimental-library)
endif()

# llvm/llvm-project#76807: condition_variable_any::wait* with stop_token can
# fail to wake on stop in libc++ < 18.1. Fix landed in libc++ 18.1.0.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([[
  #include <version>
  #if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 180100
  #error libc++ predates the stop_token wait fix
  #endif
  int main() {}
]] YAC_LIBCXX_HAS_STOP_TOKEN_FIX)
if(NOT YAC_LIBCXX_HAS_STOP_TOKEN_FIX)
  message(WARNING
    "Detected libc++ < 18.1 (llvm/llvm-project#76807): "
    "std::condition_variable_any::wait/wait_for/wait_until with std::stop_token "
    "may not wake on stop. YAC uses this pattern in chat_service, "
    "mcp_server_session, and the chat UI ticker/animation, so shutdown can "
    "stall. Build will proceed; for a clean runtime, use Homebrew GCC or "
    "upstream LLVM >= 18 (Apple Clang through Xcode 16.4 ships libc++ ~17).")
endif()

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  # Pin a known-good upstream revision for reproducible builds.
  GIT_TAG 84af6469fc54e2505802517ee62b4831897ab319
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
  # Pin a known-good upstream revision for reproducible builds.
  GIT_TAG 9554a4d86d7650bb3b46db772dbacd5f3e054b8c
  # Header-only consumed via our own INTERFACE target below; skip auto-add.
  SOURCE_SUBDIR yac_skip_auto_add
)
FetchContent_MakeAvailable(openai_cpp)

add_library(openai_cpp INTERFACE)
target_include_directories(openai_cpp INTERFACE
  ${openai_cpp_SOURCE_DIR}/include/openai
)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

FetchContent_Declare(
  keychain
  GIT_REPOSITORY https://github.com/hrantzsch/keychain.git
  GIT_TAG v1.3.1
  # We add_subdirectory conditionally below; skip MakeAvailable's auto-add.
  SOURCE_SUBDIR yac_skip_auto_add
)
FetchContent_MakeAvailable(keychain)

if(APPLE OR WIN32)
  add_subdirectory(${keychain_SOURCE_DIR} ${keychain_BINARY_DIR} EXCLUDE_FROM_ALL)
else()
  find_package(PkgConfig QUIET)
  set(YAC_KEYCHAIN_HAS_BACKEND OFF)
  if(PkgConfig_FOUND)
    pkg_check_modules(YAC_KEYCHAIN_GLIB QUIET glib-2.0)
    pkg_check_modules(YAC_KEYCHAIN_LIBSECRET QUIET libsecret-1)
    if(YAC_KEYCHAIN_GLIB_FOUND AND YAC_KEYCHAIN_LIBSECRET_FOUND)
      set(YAC_KEYCHAIN_HAS_BACKEND ON)
    endif()
  endif()

  if(YAC_KEYCHAIN_HAS_BACKEND)
    add_subdirectory(${keychain_SOURCE_DIR} ${keychain_BINARY_DIR} EXCLUDE_FROM_ALL)
  else()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/keychain_shim.cpp" CONTENT [[
#include <cstdlib>
#include <string>

#include "keychain.h"

namespace keychain {
namespace {

bool DbusUnavailable() {
  return std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr;
}

void ClearError(Error& err) {
  err.type = ErrorType::NoError;
  err.message.clear();
  err.code = 0;
}

}  // namespace

std::string getPassword(const std::string&, const std::string&, const std::string&, Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return {};
  }

  err.type = ErrorType::NotFound;
  err.message.clear();
  err.code = 0;
  return {};
}

void setPassword(const std::string&, const std::string&, const std::string&, const std::string&, Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return;
  }

  ClearError(err);
}

void deletePassword(const std::string&, const std::string&, const std::string&, Error& err) {
  if (DbusUnavailable()) {
    err.type = ErrorType::GenericError;
    err.message = "DBus unavailable";
    err.code = 0;
    return;
  }

  ClearError(err);
}

}  // namespace keychain
]])

    add_library(yac_keychain_impl STATIC "${CMAKE_BINARY_DIR}/keychain_shim.cpp")
    target_include_directories(yac_keychain_impl PRIVATE
      ${keychain_SOURCE_DIR}/include/keychain
    )

    add_library(keychain INTERFACE)
    target_include_directories(keychain INTERFACE
      ${keychain_SOURCE_DIR}/include
    )
    target_link_libraries(keychain INTERFACE yac_keychain_impl)
  endif()
endif()

add_library(yac_keychain INTERFACE)
target_link_libraries(yac_keychain INTERFACE keychain)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(yac_core_types INTERFACE)
target_include_directories(yac_core_types INTERFACE src)

add_library(yac_presentation STATIC
  src/presentation/chat_session.cpp
  src/presentation/chat_ui.cpp
  src/presentation/chat_ui_notice_format.cpp
  src/presentation/chat_ui_composer_render.cpp
  src/presentation/chat_ui_dynamic_message_stack.cpp
  src/presentation/chat_ui_components.cpp
  src/presentation/chat_ui_input_controller.cpp
  src/presentation/chat_ui_overlay_state.cpp
  src/presentation/chat_ui_render_plan.cpp
  src/presentation/chat_ui_scroll_state.cpp
  src/presentation/chat_ui_clock_ticker.cpp
  src/presentation/chat_ui_thinking_animation.cpp
  src/presentation/composer_state.cpp
  src/presentation/command_palette.cpp
  src/presentation/collapsible.cpp
  src/presentation/dialog.cpp
  src/presentation/message.cpp
  src/presentation/message_render_cache.cpp
  src/presentation/message_renderer.cpp
  src/presentation/markdown/block_parser.cpp
  src/presentation/markdown/inline_tokenizer.cpp
  src/presentation/markdown/parser.cpp
  src/presentation/markdown/renderer.cpp
  src/presentation/slash_command_menu.cpp
  src/presentation/slash_command_registry.cpp
  src/presentation/syntax/highlighter.cpp
  src/presentation/syntax/internal/lexer.cpp
  src/presentation/syntax/language_alias.cpp
  src/presentation/syntax/language_registry.cpp
  src/presentation/syntax/languages/bash_lang.cpp
  src/presentation/syntax/languages/cpp_lang.cpp
  src/presentation/syntax/languages/diff_lang.cpp
  src/presentation/syntax/languages/go_lang.cpp
  src/presentation/syntax/languages/javascript_lang.cpp
  src/presentation/syntax/languages/json_lang.cpp
  src/presentation/syntax/languages/python_lang.cpp
  src/presentation/syntax/languages/rust_lang.cpp
  src/presentation/syntax/languages/typescript_lang.cpp
  src/presentation/syntax/languages/yaml_lang.cpp
  src/presentation/theme.cpp
  src/presentation/tool_call/descriptor.cpp
  src/presentation/tool_call/renderer.cpp
  src/presentation/tool_call/renderer_basic.cpp
  src/presentation/tool_call/renderer_helpers.cpp
  src/presentation/tool_call/renderer_lsp.cpp
  src/presentation/tool_call/renderer_mcp.cpp
  src/presentation/mcp/mcp_status_panel.cpp
  src/presentation/mcp/mcp_resources_command.cpp
  src/presentation/mcp/mcp_slash_commands.cpp
)
target_include_directories(yac_presentation PUBLIC src)
target_link_libraries(yac_presentation PUBLIC yac_core_types ftxui::screen ftxui::dom ftxui::component)

add_library(yac_service STATIC
  src/chat/chat_service.cpp
  src/chat/chat_service_history.cpp
  src/chat/chat_service_prompt_processor.cpp
  src/chat/chat_service_request_builder.cpp
  src/chat/chat_service_tool_approval.cpp
  src/chat/config.cpp
  src/chat/config_paths.cpp
  src/chat/prompt_library.cpp
  src/chat/settings_toml.cpp
  src/chat/sub_agent_event_adapter.cpp
  src/provider/openai_compatible_chat_provider.cpp
  src/provider/openai_compatible_chat_protocol.cpp
  src/provider/provider_registry.cpp
  src/tool_call/bash_tool_executor.cpp
  src/tool_call/edit_tool_executor.cpp
  src/tool_call/edit_replacers.cpp
  src/tool_call/executor_arguments.cpp
  src/tool_call/executor_catalog.cpp
  src/tool_call/executor.cpp
  src/tool_call/filesystem_tool_executor.cpp
  src/tool_call/grep_tool_executor.cpp
  src/tool_call/json_rpc_stdio_base.cpp
  src/tool_call/lsp_client.cpp
  src/tool_call/lsp_tool_executor.cpp
  src/mcp/mcp_server_config.cpp
  src/mcp/debug_log.cpp
  src/mcp/tool_naming.cpp
  src/mcp/secret_redaction.cpp
  src/mcp/file_token_store.cpp
  src/mcp/keychain_token_store.cpp
  src/mcp/protocol_messages.cpp
  src/mcp/mcp_server_session.cpp
  src/mcp/sse_parser.cpp
  src/mcp/stdio_mcp_transport.cpp
  src/mcp/streamable_http_mcp_transport.cpp
  src/mcp/oauth/pkce.cpp
  src/mcp/oauth/flow.cpp
  src/mcp/oauth/browser_launcher.cpp
  src/mcp/oauth/loopback_callback.cpp
  src/mcp/oauth/metadata_discovery.cpp
  src/mcp/mcp_manager.cpp
  src/cli/mcp_admin_command.cpp
  src/cli/mcp_cli_dispatch.cpp
  src/tool_call/sub_agent_tool_executor.cpp
  src/tool_call/subprocess_runner.cpp
  src/tool_call/todo_state.cpp
  src/tool_call/glob_matcher.cpp
  src/tool_call/workspace_filesystem.cpp
  src/tool_call/gitignore_filter.cpp
  src/tool_call/glob_tool_executor.cpp
)
target_include_directories(yac_service PUBLIC src)
target_link_libraries(yac_service
  PUBLIC yac_core_types openai_cpp CURL::libcurl OpenSSL::Crypto
  PRIVATE tomlplusplus::tomlplusplus yac_keychain)

include(src/chat/CMakeLists.txt)

add_library(yac_app STATIC
  src/app/bootstrap.cpp
  src/app/chat_event_bridge.cpp
  src/app/headless.cpp
  src/app/mcp_command_handlers.cpp
  src/app/model_context_windows.cpp
  src/app/model_discovery.cpp
  src/app/prompt_slash_commands.cpp
  src/app/streaming_coalescer.cpp
)
target_include_directories(yac_app PUBLIC src)
target_link_libraries(yac_app PUBLIC yac_presentation yac_service)

add_executable(yac src/main.cpp)
target_link_libraries(yac PRIVATE yac_app yac_presentation yac_service)

enable_testing()
add_subdirectory(tests)

file(GLOB_RECURSE ALL_SOURCES
  src/*.cpp src/*.hpp
  tests/*.cpp tests/*.hpp
)

find_program(CLANG_FORMAT NAMES clang-format-21 clang-format)
find_program(RUN_CLANG_TIDY NAMES run-clang-tidy-21 run-clang-tidy)

if(CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting source files with ${CLANG_FORMAT}"
  )

  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source files with ${CLANG_FORMAT}"
  )
endif()

if(RUN_CLANG_TIDY)

set(CLANG_TIDY_EXTRA_ARGS)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  foreach(IMPLICIT_INCLUDE_DIR IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    if(IMPLICIT_INCLUDE_DIR MATCHES "/c\\+\\+/")
      list(APPEND CLANG_TIDY_EXTRA_ARGS
        "--extra-arg=-isystem${IMPLICIT_INCLUDE_DIR}")
    endif()
  endforeach()
  if(CLANG_TIDY_EXTRA_ARGS)
    list(PREPEND CLANG_TIDY_EXTRA_ARGS "--extra-arg=-nostdinc++")
  endif()
endif()

string(REPLACE ";" "\",\n    \"" LINT_EXTRA_ARGS_PY
  "${CLANG_TIDY_EXTRA_ARGS}")

set(LINT_RUNNER_TEMPLATE [[
import json
import pathlib
import subprocess
import sys

source_dir = pathlib.Path(sys.argv[1]).resolve()
build_dir = pathlib.Path(sys.argv[2]).resolve()
excluded = {(source_dir / path).resolve() for path in sys.argv[3:]}

with (build_dir / "compile_commands.json").open(encoding="utf-8") as handle:
    entries = json.load(handle)

raw_extra_args = [
    "@LINT_EXTRA_ARGS_PY@"
]
extra_args = []
for arg in raw_extra_args:
    if arg.startswith("--extra-arg="):
        extra_args.append(
            "-extra-arg=" + arg.removeprefix("--extra-arg="))
    else:
        extra_args.append(arg)

files = []
for entry in entries:
    file_path = pathlib.Path(entry["file"]).resolve()
    if file_path in excluded:
        continue
    try:
        relative_path = file_path.relative_to(source_dir)
    except ValueError:
        continue
    if not relative_path.parts or relative_path.parts[0] not in {"src", "tests"}:
        continue
    files.append(str(relative_path))

command = [
    "@RUN_CLANG_TIDY@",
    "-p",
    str(build_dir),
    "-header-filter=^(src|tests)/",
    "-j",
    "0",
    *extra_args,
    *files,
]
subprocess.run(command, cwd=source_dir, check=True)
]])

string(CONFIGURE "${LINT_RUNNER_TEMPLATE}" LINT_RUNNER_SCRIPT @ONLY)

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/run_clang_tidy.py"
  CONTENT "${LINT_RUNNER_SCRIPT}")

add_custom_target(lint
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_BINARY_DIR}/run_clang_tidy.py
          ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-tidy checks (parallel)"
  VERBATIM
)

endif() # RUN_CLANG_TIDY
