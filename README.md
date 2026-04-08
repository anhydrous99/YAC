# YAC

Yet Another Chat: a C++20 terminal chat UI with rich Markdown rendering, built on FTXUI.

## Features

- Markdown rendering for headings, lists, blockquotes, links, inline code, and fenced code blocks
- Keyword-based syntax highlighting for C++, Python, JavaScript, and Rust
- Distinct user/agent message styling in a scrollable terminal UI
- Custom Markdown parser and renderer under `src/presentation/markdown/`

## Build

Requirements: CMake 3.16+, a C++20 compiler, and Ninja recommended.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run the app:

```bash
./build/yac
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Format and lint

```bash
cmake --build build --target format
cmake --build build --target lint
```

## Project layout

- `src/main.cpp` - app entrypoint
- `src/presentation/chat_ui.*` - chat interface and input handling
- `src/presentation/message_renderer.*` - message rendering
- `src/presentation/markdown/` - Markdown parser and renderer
- `src/presentation/syntax/` - syntax highlighting

Dependencies are fetched by CMake with FetchContent.
