# YAC

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
- `.clangd` reads `build/compile_commands.json`; rerun the CMake configure step whenever compile flags are missing/stale or after editing `CMakeLists.txt`.
- `format`/`lint` use `file(GLOB_RECURSE ALL_SOURCES ...)` from the root `CMakeLists.txt`, so rerun CMake configure after adding or renaming source/test files before trusting those targets.
- `lint` runs `clang-tidy -p build` over both `src/` and `tests/`; it needs a configured `build/` directory, not just the source tree.
- Dependencies are fetched by CMake: FTXUI tracks upstream `main`, Catch2 is pinned to `v3.5.2`. If a fresh configure/build breaks unexpectedly, check for FTXUI upstream drift before assuming local logic is wrong.

## Repo shape
- `src/main.cpp` is intentionally thin but it currently seeds demo messages before starting `ftxui::App::Fullscreen().Loop(...)`. Startup content changes belong there.
- `yac_presentation` is the main target; the `yac` executable is just `main.cpp` linked against that static library.
- UI flow is `chat_ui` -> `message_renderer` -> `markdown/{parser,renderer}` -> `syntax/highlighter` under `src/presentation/`.
- `ChatUI::AddMessage` pre-parses markdown only for `Sender::Agent` and stores it in `Message::cached_blocks`; `MessageRenderer` uses the cache first and reparses only as a fallback. Keep those two paths in sync when changing message creation/rendering.
- Markdown support is the custom subset implemented in `src/presentation/markdown/parser.cpp`: ATX headings, fenced code blocks (``` and `~~~`), blockquotes, ordered/unordered lists, horizontal rules, bold/italic/strikethrough, inline code, and links.
- `SyntaxHighlighter` is keyword-based, not a real parser. It only has built-in definitions for `cpp`, `python`, `javascript`, and `rust`; unknown languages render as plain text.

## Tests and conventions
- Tests are split into per-suite executables in `build/tests/yac_test_*` via `catch_discover_tests()`. Use `ctest -N` to see the exact discovered names instead of guessing.
- Renderer/highlighter tests render to an `ftxui::Screen` and assert on `ToString()` output; visual changes often require updating string-based expectations, not adding screenshot tooling.
- Input handling tests in `tests/test_chat_ui.cpp` use raw FTXUI escape sequences for Shift/Ctrl/Alt+Enter and Home/End variants. If you change key handling, update those escape-sequence tests too.
- Formatting/naming are enforced by `.clang-format` and `.clang-tidy`: 2-space indent, 80-column limit, quoted includes before system headers, `catch2/` and `ftxui/` includes after other angle-bracket headers; namespaces are `lower_case`, classes/functions `CamelCase`, variables/params/members `lower_case`, and constexpr/global constants use `k` prefixes.
- Header-only data/helpers are normal here (`markdown/ast.hpp`, `presentation/util/*.hpp`); keep logic-heavy behavior in `.cpp` files.
- If you add explicit FTXUI loop-exit handling, prefer `ExitLoopClosure()` patterns over direct `Exit()` calls.
