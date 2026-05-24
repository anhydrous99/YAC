# Contributing to YAC

This guide covers the build, test, and quality checks expected before opening a
change.

## Development Setup

Install Bazelisk or a `bazel` binary that honors `.bazelversion`, plus a C++20
toolchain.

Linux packages commonly needed:

```bash
sudo apt-get update
sudo apt-get install -y ripgrep clangd
```

macOS packages commonly needed:

```bash
brew install bazelisk ripgrep llvm
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

Build the app:

```bash
git clone <repo-url>
cd yac
bazel build //src:yac
```

The first build downloads pinned Bazel module dependencies and compiles the AWS
Bedrock SDK overlay. Later builds reuse Bazel's repository and action caches.

## Running Tests

Run the full suite:

```bash
bazel test //tests:all_tests
```

Run one test target:

```bash
bazel test //tests:yac_test_renderer
```

List test targets:

```bash
bazel query 'tests(//tests:*)'
```

Tests are registered in `tests/BUILD.bazel` with `yac_test(...)`,
`yac_aws_test(...)`, helper binaries, or shell tests. Integration tests under
`tests/integration/` use `yac_test_e2e_runner` and JSONL scripts; see
[tests/integration/README.md](tests/integration/README.md).

## Code Style

Google base, 2-space indent, 80-column limit. Naming rules from `.clang-tidy`:

| Kind | Convention |
|------|-----------|
| Functions, types, enum constants | `CamelCase` |
| Variables, parameters, members | `lower_case` |
| `constexpr` / global constants | `kCamelCase` |
| Private members | `lower_case_` |

Apply formatting before committing:

```bash
bazel run //tools:format
```

## Quality Gates

All three must pass before a PR can land:

```bash
bazel test //tests:all_tests
bazel test //tools:format_check
bazel run //tools:lint
```

CI runs these on Linux and macOS for every PR. Coverage runs only on PRs and
requires reviewer approval for the `coverage-approval` GitHub environment.

## Tooling Versions

CI pins to clang-format-21, clang-tidy-21, and run-clang-tidy-21. Local scripts
look for the `-21` suffixed tools first and then fall back to unsuffixed names.
Older local versions may pass locally and still fail CI.

Linux:

```bash
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
  | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
sudo add-apt-repository \
  "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-21 main"
sudo apt-get update
sudo apt-get install -y clang-format-21 clang-tidy-21 clang-tools-21
```

macOS:

```bash
brew install llvm
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```

The `lint` and `coverage` targets also require `python3` on PATH. Generate
editor compile commands with:

```bash
bazel run //tools:refresh_compile_commands
```

Run coverage with:

```bash
bazel run //tools:coverage
```

## Adding Source Or Test Files

Add new implementation files to the relevant `src/BUILD.bazel` target. Add new
tests to `tests/BUILD.bazel` with the appropriate test macro and any data
dependencies required by the test.

After changing build metadata, run the narrow target first, then the full suite
or the CI-equivalent commands above.

## PR Process

- One PR per logical change. Keep diffs focused.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
  `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, etc.
- CI must pass on Linux and macOS before merge.
- Coverage is gated by the `coverage-approval` environment on PRs.

## Adding a Provider

Implement the interface in `src/provider/provider_interface.hpp`. Follow the
existing OpenAI-compatible and Bedrock providers for registration, config
loading, streaming events, and tests.

## Adding an MCP Server

See [docs/mcp.md](docs/mcp.md) for the full config schema, OAuth flow, and
approval policy reference.

Register a server without editing TOML by hand:

```bash
yac mcp add <id> --transport stdio --command <cmd> --args '<arg1>,<arg2>'
```

From inside the TUI, use `/mcp add`. List servers with `yac mcp list`.

## Issue Reporting

Open a GitHub issue with:

- Steps to reproduce.
- OS, compiler, standard library, and Bazel/Bazelisk version.
- Relevant provider, model, and config details with secrets redacted.
- If MCP-related: attach `~/.yac/logs/mcp/<server-id>.log`.
- If a crash: stack trace or sanitizer output if available.
