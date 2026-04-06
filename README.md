<p align="center">
  <h1 align="center">YAC</h1>
  <p align="center">
    <strong>Yet Another Chat</strong> — a markdown-rich terminal chat UI, forged in C++20.
  </p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white" alt="C++20" />
  <img src="https://img.shields.io/badge/FTXUI-terminal-8A2BE2" alt="FTXUI" />
  <img src="https://img.shields.io/badge/Catppuccin-Mocha-1e1e2e?logo=catppuccin&logoColor=CBA6F7" alt="Catppuccin Mocha" />
  <img src="https://img.shields.io/badge/tests-Catch2_3-green?logo=catch" alt="Catch2" />
</p>

---

### Why?

Because every CLI deserves beautiful messages. YAC renders rich markdown inside a terminal chat interface — headings, bold, italic, strikethrough, inline code, code blocks with syntax highlighting, blockquotes, and lists — all styled with a Catppuccin Mocha palette.

### Features

- **Markdown engine** — hand-rolled parser producing an AST, then rendered into FTXUI elements. No external markdown library needed.
- **Syntax highlighting** — keyword-based highlighter for C++, Python, JavaScript, and Rust, with per-token coloring (keywords, types, strings, comments, numbers).
- **Dual-role chat** — distinct styling for User (blue) and Agent (green) messages, with colored role indicators.
- **Scrollable message list** — auto-scrolls to the latest message with a scroll indicator.
- **Clean architecture** — all UI lives under `yac::presentation`, with clear separation between parsing, rendering, and component logic.

### Architecture

```
src/
  main.cpp                          app entrypoint
  presentation/
    chat_ui.{hpp,cpp}               message list + input component
    message.{hpp,cpp}               Message struct & Sender enum
    message_renderer.{hpp,cpp}      routes messages to styled FTXUI elements
    theme.hpp                       Catppuccin Mocha color constants
    markdown/
      ast.hpp                       node types (Text, Bold, Heading, CodeBlock, ...)
      parser.{hpp,cpp}              markdown text -> vector<BlockNode>
      renderer.{hpp,cpp}            BlockNode tree -> FTXUI elements
    syntax/
      highlighter.{hpp,cpp}         source code -> colored FTXUI elements
```

### Build & Run

Requires [CMake](https://cmake.org/) >= 3.16 and a C++20 compiler. [Ninja](https://ninja-build.org/) recommended.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/yac
```

FTXUI is fetched automatically via CMake's FetchContent.

### Test

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests live in `tests/` and use [Catch2 v3](https://github.com/catchorg/Catch2).

### Format & Lint

```bash
cmake --build build --target format    # clang-format
cmake --build build --target lint      # clang-tidy
```

### License

See [LICENSE](LICENSE) if one exists. Otherwise, use at your own risk.
