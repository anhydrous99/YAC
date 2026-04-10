# YAC

C++20 terminal chat UI with rich Markdown rendering, built on FTXUI.

## Verified commands
- Configure first: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- Build: `cmake --build build`
- Run app: `./build/yac`
- Full tests: `ctest --test-dir build --output-on-failure`
- List exact CTest names before filtering: `ctest --test-dir build -N`
- Run one discovered test: `ctest --test-dir build -R "^ATX heading level 1$" --output-on-failure`
- Format: `cmake --build build --target format`
- Lint: `cmake --build build --target lint`

## Tooling gotchas
- `.clangd` reads `build/compile_commands.json`; rerun CMake configure whenever compile flags are missing/stale or after editing `CMakeLists.txt`.
- `format`/`lint` use `file(GLOB_RECURSE ALL_SOURCES ...)` from the root `CMakeLists.txt`, so rerun CMake configure after adding or renaming source/test files before trusting those targets.
- `lint` runs `clang-tidy -p build` with `HeaderFilterRegex: src/.*\.hpp|tests/.*\.cpp` — it needs a configured `build/` directory, not just the source tree.
- Dependencies are fetched by CMake: FTXUI tracks upstream `main`, Catch2 is pinned to `v3.5.2`. If a fresh configure/build breaks unexpectedly, check for FTXUI upstream drift before assuming local logic is wrong.

## Repo shape
- `yac_presentation` is the static library target; the `yac` executable is just `main.cpp` linked against it.
- `src/main.cpp` seeds demo messages then runs `ftxui::App::Fullscreen().Loop(...)`. Startup content changes belong there.
- Presentation layer lives entirely under `src/presentation/`. Key flow:
  - **ChatUI** (`chat_ui.*`) — top-level component; owns messages, input, scroll, and command palette
  - **MessageRenderer** (`message_renderer.*`) — renders a `Message` into an FTXUI element; uses markdown cache first
  - **Markdown** (`markdown/{parser,renderer}`) — custom parser (ATX headings, fenced code blocks with ` ``` ` and `~~~`, blockquotes, ordered/unordered lists, horizontal rules, bold/italic/strikethrough, inline code, links) and FTXUI renderer
  - **SyntaxHighlighter** (`syntax/highlighter.*`) — keyword-based (not a real parser); built-in definitions for `cpp`, `python`, `javascript`, `rust` only; unknown languages render as plain text
  - **Tool call** (`tool_call/{types,renderer}`) — `std::variant`-based: BashCall, FileEditCall (unified diff), FileReadCall, GrepCall, GlobCall, WebFetchCall, WebSearchCall
  - **CommandPalette** (`command_palette.*`) — modal overlay, toggled from ChatUI
  - **Collapsible / Dialog** (`collapsible.*`, `dialog.*`) — shared FTXUI components
  - **Theme** (`theme.*`) — singleton (`Theme::Instance()`) returning CatppuccinMocha by default; color structs for role, markdown, code, syntax, chrome, cards, tool, dialog
  - **Util** (`util/*.hpp`) — header-only: `scroll_math`, `string_util`, `time_util` (includes `RelativeTimeCache`)

## Message caching
- `ChatUI::AddMessage` pre-parses markdown only for `Sender::Agent` and stores it in `Message::cached_blocks`; `MessageRenderer` uses the cache first and reparses only as a fallback. Keep those two paths in sync when changing message creation/rendering.
- `Message` also caches the rendered FTXUI element (`cached_element`) keyed by `cached_terminal_width` — width changes invalidate the element cache.
- `ChatUI::AddToolCallMessage` stores a `ToolCallBlock` variant in `Message::tool_call`; `Sender::Tool` is used for tool call messages.

## Tests and conventions
- Tests are split into per-suite executables via `catch_discover_tests()`. Use `ctest -N` to see the exact discovered names instead of guessing.
- Renderer/highlighter tests render to an `ftxui::Screen` and assert on `ToString()` output; visual changes often require updating string-based expectations, not adding screenshot tooling.
- Input handling tests in `tests/test_chat_ui.cpp` use raw FTXUI escape sequences for Shift/Ctrl/Alt+Enter and Home/End variants. If you change key handling, update those escape-sequence tests too.
- Formatting/naming are enforced by `.clang-format` and `.clang-tidy`: 2-space indent, 80-column limit, quoted includes before system headers, `catch2/` and `ftxui/` includes after other angle-bracket headers; namespaces are `lower_case`, classes/functions `CamelCase`, variables/params `lower_case`, **private members** have a `_` suffix, and constexpr/global constants use `k` prefixes.
- Header-only data/helpers are normal here (`markdown/ast.hpp`, `tool_call/types.hpp`, `presentation/util/*.hpp`); keep logic-heavy behavior in `.cpp` files.
- If you add explicit FTXUI loop-exit handling, prefer `ExitLoopClosure()` patterns over direct `Exit()` calls.
