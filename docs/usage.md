# Usage

YAC starts in the fullscreen TUI by default and also supports headless runs for
automation.

## TUI Shortcuts

- `Enter` sends the current message.
- `Shift+Enter`, `Ctrl+Enter`, and `Alt+Enter` insert a newline.
- `Shift+Tab` toggles between Plan and Build mode.
- `Ctrl+P` opens the command palette.
- `Escape` closes the command palette or slash command menu.
- `Up` and `Down` move through palette or slash command results.
- `Tab` moves upward through slash command results.
- `PageUp` and `PageDown` scroll by a page.
- `Home` jumps to the top of the transcript.
- `End` jumps to the bottom.
- Mouse wheel and scrollbar dragging scroll the transcript.

The command palette filters by case-insensitive substring matching across names
and descriptions. It includes `New Chat`, `Clear Messages`, `Cancel Response`,
and `Help`; `Switch Model` appears after model discovery has results.

## Slash Commands

Typing `/` opens slash command autocomplete. Built-ins include `/help`, `/?`,
`/clear`, `/cancel`, `/task <description>`, `/mcp`, `/auth`, `/quit`, `/exit`,
and OpenAI `/effort` when supported by the active model.

YAC also loads predefined prompt commands from `~/.yac/prompts/*.toml`. The file
stem becomes the command name, so `~/.yac/prompts/review.toml` becomes
`/review`. Command arguments replace every literal `$ARGUMENTS` token in the
prompt body.

Example prompt file:

```toml
description = "Review current changes"
prompt = """
Review the requested target:
$ARGUMENTS
"""
```

Prompt files are loaded at startup, so restart YAC after editing them.

## Plan And Build

Press `Shift+Tab` in the TUI to switch between Plan and Build. Headless runs can
start in Plan with:

```bash
yac run --plan "prompt"
```

The first Plan request creates an active plan file under
`.opencode/plans/*.md`. Plan mode does not expose generic file write/edit tools;
workspace changes wait for Build.

When the plan is ready, the assistant calls `plan_exit` with the final plan. YAC
asks for approval, writes the approved plan to the active plan file, switches to
Build on approval, and sends one Build-mode reminder that points back to the
approved plan. Rejecting the approval leaves the chat in Plan.

## Headless And Admin Commands

```bash
yac run "prompt"
yac run "prompt" --auto-approve
yac run "prompt" --cancel-after-ms=5000
yac mcp list
yac mcp add <id> --transport stdio --command <cmd> --args '<arg1>,<arg2>'
yac mcp auth <server-id>
yac auth openai status
```

When running from the workspace without installing the binary, use Bazel:

```bash
bazel run //src:yac -- run "prompt"
bazel run //src:yac -- mcp list
bazel run //src:yac -- auth openai status
```

`yac run` is the headless entrypoint. The TUI is used when no subcommand is
provided.
