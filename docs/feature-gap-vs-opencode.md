# YAC vs OpenCode: Feature Gap Analysis

> Generated 2026-04-24. Compared against OpenCode (sst/opencode) current state.

## Critical Missing Features (High Impact)

### 1. MCP (Model Context Protocol) Support

OpenCode supports local + remote MCP servers with OAuth, embedded MCPs in
skills, and a full management CLI (`mcp add/list/auth/logout/debug`). YAC has
zero MCP integration. This is the modern standard for tool extensibility --
every external integration (Sentry, Context7, GitHub, Linear, etc.) flows
through MCP.

### 2. Provider Coverage (2 vs 75+)

YAC ships only `openai-compatible` and `zai` presets. OpenCode integrates with Models.dev
for 75+ providers (Anthropic native, Bedrock, Vertex, Groq, OpenRouter, Ollama,
llama.cpp, LM Studio, Cerebras, etc.) plus OAuth flows for Claude Pro/Max,
GitHub Copilot, GitLab Duo. No Anthropic-native, no local model support, no
OAuth.

### 3. Session Persistence (memory-only)

YAC keeps history in RAM only -- lost on exit. OpenCode persists every session,
supports `/sessions` listing, resume, fork, search, import/export, and
hierarchical session-tree navigation across nested sub-agents.

### 4. Session Sharing / Web UI / Server Mode

OpenCode has `opencode share`, `opencode serve`, `opencode web`,
`opencode attach`, ACP server, public URLs at opncd.ai. YAC has none of this.

### 5. Skills System

On-demand SKILL.md loading is OpenCode's killer pattern (playwright, git-master,
frontend-ui-ux, review-work, ai-slop-remover). Skills can carry embedded MCPs.
YAC has nothing equivalent.

### 6. Plugin System

OpenCode has 25+ plugin event hooks (`session.*`, `tool.execute.before/after`,
`message.*`, `lsp.*`, `permission.*`, `tui.*`) and 40+ community plugins. YAC
has zero plugin extensibility.

### 7. Edit Tools (major gap)

YAC has `file_write` (full overwrite only). OpenCode has `edit` (precise string
replacement), `apply_patch` (multi-file patches), `glob`, `grep`. Writing whole
files vs surgical edits is a 10x UX difference for code agents.

### 8. Web Tools

OpenCode has `webfetch` and `websearch` (Exa). YAC has neither -- agent can't
research docs or fetch URLs.

## Important Missing Features

### 9. LSP Coverage (clangd-only vs 30+)

YAC supports only clangd (C/C++). OpenCode auto-installs LSP servers for
TypeScript, Python, Go, Rust, Ruby, Java, Swift, etc. -- automatic for any
project type.

### 10. Code Formatters

OpenCode auto-runs prettier/biome/ruff/rustfmt/gofmt/clang-format/etc. after
every edit. YAC has no formatter integration.

### 11. Custom Agents / Subagent Definitions

OpenCode lets users define custom agents in `~/.config/opencode/agents/*.md`
with model overrides, temperature, max_steps, permissions, custom prompts. YAC
has only built-in Plan/Build modes.

### 12. Granular Permission System

OpenCode: pattern-based rules (`"git *": allow`, `"rm -rf *": deny`), per-agent
overrides, allow/ask/deny levels, doom-loop protection, `external_directory`
control. YAC: binary "approve this exact call" only.

### 13. GitHub / GitLab Integration

OpenCode: GitHub Actions workflow (`/opencode` in PR comments), GitLab Duo
Agent Platform with workflow models. YAC: none.

### 14. AGENTS.md Hierarchy

OpenCode supports hierarchical AGENTS.md (root + nested + globs like
`packages/*/AGENTS.md`), Claude.md compatibility, external instruction files.
YAC has `/init` for a single AGENTS.md but no hierarchy/glob loading.

### 15. IDE Integration

OpenCode: VS Code/Cursor/Windsurf extension with Cmd+Esc quick-launch, context
awareness, file-reference shortcuts. YAC: terminal-only.

### 16. CLI Surface

OpenCode: `agent`, `auth`, `mcp`, `models`, `session`, `github`, `upgrade`,
`serve`, `web`, `attach`, `acp`, `stats`. YAC: only `run` (headless).

## Smaller Gaps

| Feature | OpenCode | YAC |
|---|---|---|
| Slash commands | 18+ built-in (`/share`, `/undo`, `/redo`, `/sessions`, `/models`, `/themes`, `/editor`, `/export`, `/import`, `/details`, `/thinking`, `/connect`, `/summarize`) | 6 (`/help`, `/clear`, `/cancel`, `/quit`, `/compact`, `/init`) |
| Custom commands | `$ARGUMENTS`, `$1..$N`, `!shell`, `@file` | `$ARGUMENTS` only |
| File references | `@file` fuzzy autocomplete in composer | None |
| Bash in composer | `!command` prefix | None |
| Themes | 10+ + custom JSON | 3 hardcoded (opencode/catppuccin/system) |
| Image attachments | Yes (multimodal) | No |
| Undo/redo | Snapshot-based file rollback | No |
| Auto-update | Built-in | No |
| Token/cost stats | `opencode stats` | No |
| Tool-call rounds | Unbounded with budget | Hard-capped at 8 |
| Background agents | Async with session-tree navigation | Sub-agents but no session-tree |
| Doom-loop detection | Yes | No |
| `.gitignore` respect | Yes | Unclear |

## What YAC Already Has (parity or unique)

- **Plan/Build mode toggle** (Shift+Tab) -- more ergonomic than OpenCode's
  agent switching
- **Native C++ binary** -- single static binary, no node/bun runtime needed
- **Sub-agent foreground/background distinction** -- clean abstraction
- **Tool approval dialogs** -- OpenCode has equivalent via permission system
- **Semantic theme color roles** -- actually quite rich, more granular than most
- **Command palette** -- model switching, slash commands
- **Markdown rendering** -- headings, lists, code blocks, inline formatting
- **Syntax highlighting** -- C++, Python, JavaScript, Rust
- **History compaction** -- `/compact` command

## Recommended Priority

### Tier 1 -- Table stakes for a serious AI coding TUI

1. [x] DONE `edit` tool (string-replacement) -- biggest immediate UX win (`file_edit`)
2. [x] DONE `grep` + `glob` tools
3. Anthropic native provider
4. Session persistence (SQLite or JSON in `~/.yac/sessions/`)
5. `webfetch` tool

### Tier 2 -- Modern competitive baseline

6. MCP client support
7. More LSP servers (TypeScript, Python, Go, Rust)
8. Custom agent definitions (markdown/JSON)
9. Pattern-based permission rules
10. AGENTS.md hierarchy + glob loading

### Tier 3 -- Differentiators

11. Skills system
12. Plugin hooks
13. Server mode + web UI
14. Session sharing
15. IDE extension
