# Contributing to YAC

Thanks for your interest in contributing. This guide covers everything you need
to build, test, and submit changes.

## Development Setup

Requires CMake >= 3.21, Ninja, and git submodules.

```bash
git clone <repo-url>
cd yac
git submodule update --init --recursive
./external/vcpkg/bootstrap-vcpkg.sh   # one-time, ~30s
cmake --preset debug
cmake --build build
```

The first configure downloads and builds `aws-sdk-cpp` (~15 min on a cold
cache). Subsequent configures are under 30 seconds because vcpkg caches
installed packages under `external/vcpkg/`.

System packages needed: `libcurl`, OpenSSL, `ripgrep`, `clangd`. On Linux,
also `libsecret-1-dev` and DBus for keychain support (gracefully degrades
without them).

## Running Tests

List all discovered tests:

```bash
ctest --test-dir build -N
```

Run the full suite:

```bash
ctest --test-dir build --output-on-failure
```

Filter by name (Catch2 test name, not binary name):

```bash
ctest --test-dir build -R "<name>" --output-on-failure
```

Tests are discovered as `<binary_name>::<Catch test name>`, e.g.
`yac_test_chat_service::Reset clears history`. Register a new test by adding
`yac_add_test(<binary> <source>)` to `tests/CMakeLists.txt`, then reconfigure.

## Code Style

Google base, 2-space indent, 80-column limit. Naming rules from `.clang-tidy`:

| Kind | Convention |
|------|-----------|
| Functions, types, enum constants | `CamelCase` |
| Variables, parameters, members | `lower_case` |
| `constexpr` / global constants | `kCamelCase` |
| Private members | `lower_case_` (trailing underscore) |

Run `cmake --build build --target format` to auto-apply clang-format before
committing.

## Quality Gates

All three must pass before a PR can land:

```bash
cmake --build build --target format-check   # fails on any formatting diff
cmake --build build --target lint           # parallel clang-tidy
ctest --test-dir build --output-on-failure  # full test suite
```

CI runs these on both Linux and macOS for every PR.

## Adding Source or Test Files

After adding or renaming any source file, **reconfigure**:

```bash
cmake --preset debug
```

The `format`, `lint`, and `ALL_SOURCES` targets glob at configure time. New
files are invisible to them until you reconfigure. This applies to both
`src/` additions and new test binaries in `tests/CMakeLists.txt`.

## Tooling Versions

CI pins to clang-format-21 and clang-tidy-21. CMake's `find_program` looks for
the `-21` suffixed binaries first, then falls back to unsuffixed. Older local
versions may pass locally and still fail CI.

**Linux** (Ubuntu/Debian):

```bash
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
sudo add-apt-repository \
  "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-21 main"
sudo apt-get update
sudo apt-get install -y clang-format-21 clang-tidy-21
```

**macOS** (Homebrew):

```bash
brew install llvm
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

Add the `PATH` export to your shell profile so CMake finds the right binaries
on every session.

The `lint` and `coverage` targets also require `python3` on PATH to drive
`run-clang-tidy` and `llvm-cov`.

## PR Process

- One PR per logical change. Keep diffs focused.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
  `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, etc.
- CI must pass on both Linux and macOS before merge.
- Coverage runs only on PRs and requires a reviewer to approve the
  `coverage-approval` environment in GitHub before the job starts.

## Adding a Provider

Implement the interface in `src/provider/provider_interface.hpp`. Look at the
existing `OpenAiChatProvider` and `BedrockChatProvider` implementations for
patterns: registration in `src/main.cpp`, config loading, and streaming event
emission.

## Adding an MCP Server

See **[docs/mcp.md](docs/mcp.md)** for the full config schema, OAuth flow, and
approval policy reference.

The quickest way to register a server without editing TOML by hand:

```bash
yac mcp add <id> --transport stdio --command <cmd> --args '<arg1>,<arg2>'
```

From inside the TUI, use `/mcp add` instead. List servers with `yac mcp list`.

## Issue Reporting

Open a GitHub issue with:

- Steps to reproduce (minimal, copy-pasteable)
- OS, compiler, and libc++/libstdc++ version
- CMake and Ninja versions (`cmake --version`, `ninja --version`)
- If MCP-related: attach `~/.yac/logs/mcp/<server-id>.log`
- If a crash: stack trace or sanitizer output if available
