# YAC

C++20 terminal chat UI with rich Markdown rendering, streaming AI responses, and structured tool-call display, built on FTXUI.

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
- Dependencies are fetched by CMake with `FetchContent`: FTXUI and `openai-cpp` both track upstream `main`, Catch2 is pinned to `v3.5.2`. If a fresh configure/build breaks unexpectedly, check for upstream drift in FTXUI or openai-cpp before assuming local logic is wrong.
- The app requires `libcurl` (found via `find_package(CURL REQUIRED)`). If CMake can't find it, install the dev package (`libcurl4-openssl-dev` on Debian/Ubuntu).

## Configuration
- The app reads `.env` from the repo root via `EnvFile::FindAndParse()`. Copy `.env.example` to `.env` and set `OPENAI_API_KEY` (at minimum). Shell environment variables override `.env` values.
- Config keys: `YAC_PROVIDER`, `YAC_MODEL`, `YAC_BASE_URL`, `YAC_TEMPERATURE`, `YAC_SYSTEM_PROMPT`, `OPENAI_API_KEY`. Defaults match OpenAI (`gpt-4o-mini`, `https://api.openai.com/v1/`).
- `.env` is in `.gitignore` — never commit API keys.

## Repo shape

Three static libraries + one executable, linked as: `yac` → `yac_app` → `yac_presentation` + `yac_service`.

### `yac_presentation` (`src/presentation/`) — UI layer
- **ChatUI** (`chat_ui.*`) — top-level FTXUI component; owns `ChatSession`, `ComposerState`, `MessageRenderCacheStore`, scroll state, slash command menu, and command palette. Exposes `StartAgentMessage`, `AppendToAgentMessage`, `SetTyping` for streaming.
- **ChatSession** (`chat_session.*`) — owns the `Message` vector and tool-call expanded states. Manages `MessageId` allocation and tool-call expand/collapse via `SetToolExpanded`.
- **ComposerState** (`composer_state.*`) — input field state: content string, cursor position, newline insertion, and submit logic.
- **MessageRenderer** (`message_renderer.*`) — static methods that render `Message` into FTXUI elements via `RenderContext`. Receives `MessageRenderCache&` from the caller for caching.
- **MessageRenderCache** (`message_render_cache.*`) — `MessageRenderCache` stores parsed markdown blocks, rendered element, `terminal_width`, and `RelativeTimeCache` per message. `MessageRenderCacheStore` maps `MessageId` → `MessageRenderCache`; owned by `ChatUI`.
- **RenderContext** (`render_context.hpp`) — header-only struct threading `Theme` pointer + `terminal_width` through the render pipeline.
- **Message** (`message.*`) — `MessageContent = std::variant<TextContent, ToolContent>`; `SenderSwitch()` utility for sender-dispatch. `Message` does NOT own its render cache.
- **SlashCommandRegistry** (`slash_command_registry.*`) — registers named commands with handlers; `TryDispatch` matches `/command` input. Slash commands (e.g. `/quit`, `/exit`) are distinct from the command palette.
- **SlashCommandMenu** (`slash_command_menu.hpp`) — header-only renderer for the slash command dropdown.
- **CommandPalette** (`command_palette.*`) — modal overlay, toggled from ChatUI. Displays registered `Command` objects with name + description.
- **Markdown** (`markdown/{parser,renderer}`) — custom parser (ATX headings, fenced code blocks with ` ``` ` and `~~~`, blockquotes, ordered/unordered lists, horizontal rules, bold/italic/strikethrough, inline code, links) and FTXUI renderer.
- **SyntaxHighlighter** (`syntax/highlighter.*`) — keyword-based (not a real parser); built-in definitions for `cpp`, `python`, `javascript`, `rust` only; unknown languages render as plain text.
- **Collapsible / Dialog** (`collapsible.*`, `dialog.*`) — shared FTXUI components.
- **Theme** (`theme.*`) — singleton (`Theme::Instance()`) returning CatppuccinMocha by default; color structs for role, markdown, code, syntax, chrome, cards, tool, dialog.
- **Util** (`util/*.hpp`) — header-only: `scroll_math`, `string_util`, `time_util` (includes `RelativeTimeCache`).

### `yac_service` (`src/chat/` + `src/provider/`) — business logic
- **ChatService** (`chat/chat_service.*`) — owns conversation history, provider registry, and a `std::jthread` worker loop. `SubmitUserMessage` enqueues a prompt; the worker calls `LanguageModelProvider::CompleteStream` and emits `ChatEvent`s via callback. Uses `std::stop_token` for cancellation. Thread-safe (mutex-protected history and queue).
- **Config** (`chat/config.*`) — `LoadChatConfigFromEnv()` reads `.env` + environment and returns `ChatConfig`.
- **EnvFile** (`chat/env_file.*`) — parses `key=value` files (skips comments/blanks). `FindAndParse()` walks up from cwd to find `.env`.
- **Types** (`chat/types.hpp`) — header-only: `ChatMessage`, `ChatRequest`, `ChatEvent`, `ChatConfig`, `ProviderConfig`, enums for `ChatRole`/`ChatEventType`/`ChatMessageStatus`.
- **LanguageModelProvider** (`provider/language_model_provider.hpp`) — abstract interface: `Id()` + `CompleteStream(request, sink, stop_token)`. All providers are non-copyable.
- **OpenAiChatProvider** (`provider/openai_chat_provider.*`) — concrete provider using `openai-cpp` + libcurl. Streams responses via the `ChatEventSink`.
- **ProviderRegistry** (`provider/provider_registry.*`) — `Register`/`Resolve` by provider ID string.

### `yac_app` (`src/app/`) — glue layer
- **ChatEventBridge** (`chat_event_bridge.*`) — translates `chat::ChatEvent` into `ChatUI` calls: `UserMessageQueued` → `AddMessageWithId`, `TextDelta` → `AppendToAgentMessage`, `Started` → `StartAgentMessage` + `SetTyping`, etc. Converts `ChatRole` → `Sender`. Wired in `main.cpp` via `screen.Post()` to marshal events onto the FTXUI thread.

### Shared types
- **Tool call types** (`src/tool_call/types.hpp`) — header-only: `ToolCallBlock = std::variant<BashCall, FileEditCall, FileReadCall, GrepCall, GlobCall, WebFetchCall, WebSearchCall>`. Used by both presentation and service layers.
- **Tool call renderer** (`src/presentation/tool_call/renderer.*`) — renders `ToolCallBlock` variants into FTXUI elements.

### `src/main.cpp`
- Loads config, registers the OpenAI provider, creates `ChatService`, wires `ChatEventBridge` to post events onto the FTXUI screen, sets up send/command/slash-command callbacks, and runs `screen.Loop(component)`.

## Message caching
- `ChatUI` owns a `MessageRenderCacheStore` (`render_cache_`) that maps `MessageId` → `MessageRenderCache`. This is separate from `Message` and `ChatSession`.
- `MessageRenderCache` holds: parsed `markdown_blocks` (lazily populated on first render), rendered `element` (cached FTXUI Element), `terminal_width` (invalidation key), and `RelativeTimeCache`.
- `ChatUI` invalidates caches on content changes (`ResetContent`) and on width changes (`ResetElement`). `ClearMessages` clears the entire store.
- Only `User` and `Agent` messages are element-cached; `Tool` messages re-render every time.
- `ChatSession::AddMessage` does NOT pre-parse markdown — parsing happens lazily in `MessageRenderer::RenderAgentMessage` via the cache. Keep `MessageRenderer` and `ChatUI` cache invalidation in sync when changing message creation/rendering.

## Tests and conventions
- Tests are split into per-suite executables via `catch_discover_tests()`. Use `ctest -N` to see the exact discovered names instead of guessing.
- Renderer/highlighter tests render to an `ftxui::Screen` and assert on `ToString()` output; visual changes often require updating string-based expectations, not adding screenshot tooling.
- Input handling tests in `tests/test_chat_ui.cpp` use raw FTXUI escape sequences for Shift/Ctrl/Alt+Enter and Home/End variants. If you change key handling, update those escape-sequence tests too.
- Service-layer tests (`test_chat_service`, `test_chat_event_bridge`, `test_openai_chat_provider`, `test_env_file`) test event flow, config loading, and provider wiring without a live API.
- Formatting/naming are enforced by `.clang-format` and `.clang-tidy`: 2-space indent, 80-column limit, quoted includes before system headers, `catch2/` and `ftxui/` includes after other angle-bracket headers; namespaces are `lower_case`, classes/functions `CamelCase`, variables/params `lower_case`, **private members** have a `_` suffix, and constexpr/global constants use `k` prefixes.
- Header-only data/helpers are normal here (`markdown/ast.hpp`, `tool_call/types.hpp`, `chat/types.hpp`, `presentation/util/*.hpp`); keep logic-heavy behavior in `.cpp` files.
- If you add explicit FTXUI loop-exit handling, prefer `ExitLoopClosure()` patterns over direct `Exit()` calls.
