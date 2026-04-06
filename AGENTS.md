# YAC

C++20 terminal chat UI built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run: `./build/yac`

No tests, linter, or formatter are configured.

## Test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests use [Catch2 v3](https://github.com/catchorg/Catch2) (fetched via CMake `FetchContent`). Test sources live in `tests/`.

## Architecture

- `src/main.cpp` — entrypoint, creates `ChatUI` and runs the FTXUI event loop
- `src/presentation/` — all UI code lives under `yac::presentation` namespace
  - `chat_ui` — main component: message list + input box
  - `message` — `Message` struct and `Sender` enum
  - `markdown/` — custom Markdown parser → AST → FTXUI element renderer
  - `syntax/` — keyword-based syntax highlighter (used by markdown code blocks)
  - `theme.hpp` — Catppuccin-inspired color constants

## Format & Lint

```bash
cmake --build build
cmake --build build --target format    # clang-format all sources
cmake --build build --target lint      # clang-tidy all sources
```

Style is Google C++ ([`.clang-format`](.clang-format)) with clang-tidy checks ([`.clang-tidy`](.clang-tidy)) covering naming, modern C++, readability, and bug-prone patterns. Warnings are informational (not errors).

## Conventions

- `#pragma once` for include guards
- `[[nodiscard]]` on all public query/parsing methods
- Header-only for types/AST; `.cpp` files for implementations with logic
- `compile_commands.json` is gitignored; regenerate via cmake configure
