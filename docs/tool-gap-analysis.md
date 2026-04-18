# YAC Tool Gap Analysis & Recommendations

Comparative study of built-in tool systems across Claude Code, OpenAI Codex CLI, Crush,
Aider, Cursor, GitHub Copilot, Amazon Q Developer, Windsurf, Devin, Replit Agent, and
Google Jules — mapped against YAC's existing `ToolCallBlock` variant to identify gaps and
priorities.

---

## Current YAC Tools (14 in `ToolCallBlock`)

| Tool            | Coverage            |
|-----------------|---------------------|
| `BashCall`      | Shell execution     |
| `FileEditCall`  | Diff-based editing  |
| `FileReadCall`  | File reading        |
| `FileWriteCall` | File creation/overwrite |
| `ListDirCall`   | Directory listing |
| `GrepCall`      | Content search      |
| `GlobCall`      | File pattern match  |
| `WebFetchCall`  | URL retrieval       |
| `WebSearchCall` | Web search          |
| `LspDiagnosticsCall` | Language-server diagnostics |
| `LspReferencesCall` | Symbol reference lookup |
| `LspGotoDefinitionCall` | Definition lookup |
| `LspRenameCall` | Workspace rename |
| `LspSymbolsCall` | Document symbols |

This covers the baseline every major assistant agrees on. The additions below are where
YAC can close gaps and differentiate.

---

## Priority 1 — Expected by Every Major Assistant

These are table-stakes tools present in 3+ assistants that YAC should keep
polishing now that the initial implementations exist.

| Proposed Tool        | Who Has It                                       | What It Does                                       | Suggested Fields                                                            |
|----------------------|--------------------------------------------------|----------------------------------------------------|-----------------------------------------------------------------------------|
| `FileWriteCall`      | Claude Code, Crush, Codex, Copilot, Amazon Q     | Create or fully overwrite a file (distinct from edit) | `filepath`, `content`, `lines_added`, `lines_removed`                      |
| `ListDirCall`        | Crush (`ls`), Codex (`list_dir`), Amazon Q (`fs_read`) | List directory tree — glob finds by pattern; this is raw directory listing | `path`, `entries[]` (name, type: file/dir, size)                            |
| `LspDiagnosticsCall` | Claude Code (`LSP`), Crush (`diagnostics`)       | Get type errors, warnings, hints from a language server | `file_path`, `diagnostics[]` (severity, message, line)                     |
| `LspReferencesCall`  | Claude Code (`LSP`), Crush (`references`)        | Find all usages of a symbol across the workspace   | `symbol`, `file_path`, `references[]` (file, line)                         |
| `LspGotoDefinitionCall` | Claude Code (`LSP`)                           | Jump to symbol definition                          | `symbol`, `file_path`, `line`, `character`, `definitions[]` (file, line)   |
| `LspRenameCall`      | Claude Code (`LSP`)                              | Rename symbol across workspace                     | `file_path`, `line`, `character`, `old_name`, `new_name`, `changes_count`  |
| `LspSymbolsCall`     | Claude Code (`LSP`)                              | Document outline or workspace symbol search        | `file_path`, `symbols[]` (name, kind, line)                                |

---

## Priority 2 — Proven Value, Used by 3+ Assistants

| Proposed Tool     | Who Has It                                                    | What It Does                                          | Suggested Fields                                                         |
|-------------------|---------------------------------------------------------------|-------------------------------------------------------|--------------------------------------------------------------------------|
| `BackgroundJobCall` | Crush (`job_output`/`job_kill`), Codex (`exec_command` sessions), Claude Code (`Monitor`) | Run long-lived commands, poll output, kill them       | `action` (start/poll/kill), `command`, `session_id`, `output`, `exit_code`, `running` |
| `TodoWriteCall`   | Claude Code, Cursor, Amazon Q, Copilot                        | Track multi-step progress — agents need this for complex tasks | `todos[]` (content, status: pending/in_progress/completed)               |
| `AskUserCall`     | Claude Code (`AskUserQuestion`), Codex (`request_user_input`), Cursor (`askQuestions`) | Ask the user a clarifying question mid-task instead of guessing | `question`, `options[]`, `answer`                                        |
| `NotebookEditCall` | Claude Code, Codex (`code_mode`)                             | Edit Jupyter notebook cells                           | `notebook_path`, `cell_index`, `new_source`, `cell_type`                 |
| `ViewImageCall`   | Codex (`view_image`)                                          | Render/display a local image file for analysis        | `file_path`, `detail` (default/original)                                 |
| `McpResourceCall` | Claude Code, Codex, Crush, Copilot, Amazon Q, Windsurf       | Read resources from connected MCP servers — universal extensibility | `action` (list/read), `server`, `uri`, `content`                         |

---

## Priority 3 — Differentiators, Used by 1–2 Assistants

| Proposed Tool    | Who Has It                                                     | What It Does                                       | Suggested Fields                                                            |
|------------------|----------------------------------------------------------------|----------------------------------------------------|-----------------------------------------------------------------------------|
| `MultiEditCall`  | Crush (`multiedit`)                                            | Apply multiple edits to a single file atomically   | `filepath`, `edits[]` (old_string, new_string)                              |
| `ApplyPatchCall` | Codex (`apply_patch`)                                          | Grammar-based diff for complex multi-file patches  | `patch_text`, `files_affected[]`, `operations[]` (add/update/delete/move)   |
| `SpawnAgentCall` | Claude Code (`Agent`), Codex (`spawn_agent`), Devin, Cursor    | Delegate work to a sub-agent for parallel execution | `task_name`, `message`, `agent_id`, `status`, `result`                      |
| `PlanModeCall`   | Claude Code (`EnterPlanMode`/`ExitPlanMode`), Cursor (`/plan`), Aider (`/architect`) | Plan before executing — show plan, get approval    | `action` (enter/exit), `plan_text`, `approved`                              |
| `GitWorktreeCall` | Claude Code (`EnterWorktree`/`ExitWorktree`), Codex (`--worktree`), Windsurf | Isolate changes in a git worktree                 | `action` (create/switch/exit), `path`, `branch`                             |
| `KnowledgeCall`  | Amazon Q (`knowledge`)                                         | Persistent memory — store/retrieve facts across sessions | `action` (store/retrieve/search), `key`, `content`, `results[]`            |
| `CronCall`       | Claude Code (`CronCreate`/`CronDelete`/`CronList`)            | Schedule recurring or one-shot tasks within a session | `action` (create/delete/list), `cron_id`, `schedule`, `prompt`             |
| `SkillCall`      | Claude Code (`Skill`), Replit (`Skills`)                      | Execute a pre-loaded skill/workflow                 | `skill_name`, `arguments`, `result`                                         |

---

## Cross-Assistant Coverage Matrix

| Capability      | Claude Code | Codex | Crush | Aider | Cursor | Copilot | Amazon Q | Windsurf | Devin |
|-----------------|:-----------:|:-----:|:-----:|:-----:|:------:|:-------:|:--------:|:--------:|:-----:|
| File Read       | ✅          | ✅    | ✅    | ✅    | ✅     | ✅      | ✅       | ✅       | ✅    |
| File Edit       | ✅          | ✅    | ✅    | ✅    | ✅     | ✅      | ✅       | ✅       | ✅    |
| File Write      | ✅          | ✅    | ✅    | ✅    | ✅     | ✅      | ✅       | ✅       | ✅    |
| Shell Exec      | ✅          | ✅    | ✅    | ✅    | ✅     | ✅      | ✅       | ✅       | ✅    |
| Grep            | ✅          | —     | ✅    | —     | ✅     | ✅      | —        | ✅       | —     |
| Glob            | ✅          | —     | ✅    | —     | ✅     | ✅      | —        | ✅       | —     |
| LSP             | ✅          | —     | ✅    | —     | —      | —       | —        | —        | —     |
| Web Fetch       | ✅          | —     | ✅    | ✅    | ✅     | ✅      | —        | ✅       | ✅    |
| Web Search      | ✅          | ✅    | —     | —     | —      | —       | —        | ✅       | —     |
| Background Jobs | ✅          | ✅    | ✅    | —     | —      | —       | —        | —        | —     |
| Todo/Task       | ✅          | ✅    | —     | —     | ✅     | ✅      | ✅       | —        | —     |
| Ask User        | ✅          | ✅    | —     | —     | ✅     | ✅      | —        | —        | —     |
| MCP Integration | ✅          | ✅    | ✅    | —     | ✅     | ✅      | ✅       | ✅       | —     |
| Sub-agents      | ✅          | ✅    | —     | ✅    | ✅     | ✅      | —        | —        | ✅    |
| Plan Mode       | ✅          | ✅    | —     | ✅    | ✅     | ✅      | —        | —        | —     |
| Git Worktree    | ✅          | ✅    | —     | —     | ✅     | —       | —        | ✅       | —     |

---

## Reference: Full Tool Inventories by Assistant

### Claude Code (32 tools)

Source: [Official Tools Reference](https://code.claude.com/docs/en/tools-reference)

| Tool                  | Description                                                        | Permission |
|-----------------------|--------------------------------------------------------------------|------------|
| `Agent`               | Spawns a subagent with its own context window                      | No         |
| `AskUserQuestion`     | Multiple-choice questions to gather requirements                   | No         |
| `Bash`                | Executes shell commands                                            | Yes        |
| `CronCreate`          | Schedules a recurring or one-shot prompt                           | No         |
| `CronDelete`          | Cancels a scheduled task by ID                                     | No         |
| `CronList`            | Lists all scheduled tasks in session                               | No         |
| `Edit`                | Makes targeted edits to specific files                             | Yes        |
| `EnterPlanMode`       | Switches to plan mode before coding                                | No         |
| `EnterWorktree`       | Creates an isolated git worktree                                   | No         |
| `ExitPlanMode`        | Presents a plan for approval                                       | Yes        |
| `ExitWorktree`        | Exits a worktree session                                           | No         |
| `Glob`                | Finds files based on pattern matching                              | No         |
| `Grep`                | Searches for patterns in file contents                             | No         |
| `ListMcpResourcesTool`| Lists resources from MCP servers                                   | No         |
| `LSP`                 | Code intelligence: definitions, references, diagnostics            | No         |
| `Monitor`             | Runs a command in background, feeds output lines back              | Yes        |
| `NotebookEdit`        | Modifies Jupyter notebook cells                                    | Yes        |
| `PowerShell`          | Executes PowerShell commands (Windows)                             | Yes        |
| `Read`                | Reads the contents of files                                        | No         |
| `ReadMcpResourceTool` | Reads a specific MCP resource by URI                               | No         |
| `SendMessage`         | Sends message to agent team teammate (experimental)                | No         |
| `Skill`               | Executes a skill within the main conversation                      | Yes        |
| `TaskCreate`          | Creates a new task in the task list                                | No         |
| `TaskGet`             | Retrieves full details for a specific task                         | No         |
| `TaskList`            | Lists all tasks with their current status                          | No         |
| `TaskOutput`          | (Deprecated) Retrieves output from a background task               | No         |
| `TaskStop`            | Kills a running background task by ID                              | No         |
| `TaskUpdate`          | Updates task status, dependencies, details                         | No         |
| `TeamCreate`          | Creates an agent team with multiple teammates (experimental)       | No         |
| `TeamDelete`          | Disbands an agent team (experimental)                              | No         |
| `TodoWrite`           | Manages the session task checklist                                 | No         |
| `ToolSearch`          | Searches for and loads deferred tools                              | No         |
| `WebFetch`            | Fetches content from a specified URL                               | Yes        |
| `WebSearch`           | Performs web searches                                              | Yes        |
| `Write`               | Creates or overwrites files                                        | Yes        |

### OpenAI Codex CLI (17+ tools)

Source: [github.com/openai/codex](https://github.com/openai/codex)

| Tool                         | Description                                                   |
|------------------------------|---------------------------------------------------------------|
| `exec_command`               | Run shell commands in a PTY                                   |
| `write_stdin`                | Write to stdin of running exec session                        |
| `shell_command`              | Execute shell commands with array-based syntax                |
| `apply_patch`                | Grammar-based multi-file diff (add/update/delete/move files)  |
| `list_dir`                   | List directory entries with pagination                        |
| `web_search`                 | Search the web (cached or live)                               |
| `spawn_agent`                | Spawn a new sub-agent for parallel work                       |
| `send_input` / `send_message`| Send message to existing agent                                |
| `wait_agent`                 | Wait for agent completion                                     |
| `close_agent` / `resume_agent`| Close or resume an agent                                     |
| `list_agents`                | List all active agents                                        |
| `list_mcp_resources`         | List MCP server resources                                     |
| `list_mcp_resource_templates`| List MCP resource templates                                   |
| `read_mcp_resource`          | Read specific MCP resource                                    |
| `js_repl` / `js_repl_reset`  | Persistent JavaScript REPL with top-level await               |
| `view_image`                 | View local image file                                         |
| `request_permissions`        | Request additional permissions                                |
| `request_user_input`         | Get user input during execution                               |
| `tool_search` / `tool_suggest`| Discover available tools                                     |
| `update_plan`                | Update execution plan                                         |

### Crush (Charmbracelet) (17 tools)

Source: [github.com/charmbracelet/crush](https://github.com/charmbracelet/crush)

| Tool               | Description                                                    |
|--------------------|----------------------------------------------------------------|
| `bash`             | Execute shell commands (background jobs, banned-command list)  |
| `grep`             | Search file contents with regex (ripgrep if available)         |
| `glob`             | Find files by name patterns (200-file truncation)              |
| `ls`               | List directory tree with metadata                              |
| `view`             | Read file contents with line numbers, offset/limit             |
| `edit`             | Precise text replacements with replace_all option              |
| `multiedit`        | Multiple edits to a single file atomically                     |
| `write`            | Create or overwrite files                                      |
| `fetch`            | Fetch URLs (HTTP client, 30s timeout)                          |
| `download`         | Download files from URLs to specified path                     |
| `diagnostics`      | Get LSP diagnostics for files                                  |
| `references`       | Find symbol references via LSP                                 |
| `sourcegraph`      | Search Sourcegraph code search engine                          |
| `job_output`       | Get output from background jobs                                |
| `job_kill`         | Terminate background jobs                                      |
| `read_mcp_resource`| Read MCP server resources                                      |
| `safe`             | Safety/permission checks                                       |

### Aider

Source: [aider.chat/docs](https://aider.chat/docs/usage/commands.html)

Notable for its **architect/editor split** — a strong model plans, a cheaper model edits.
Uses git-native workflow with auto-commits and tree-sitter-based repo maps.

### Cursor

Source: [cursor.com/docs](https://cursor.com/docs/agent/tools)

Notable for **real-time awareness** of editor actions, browser control for visual testing,
checkpoints with restore, and queued messages while the agent works.

### GitHub Copilot

Source: [docs.github.com/copilot](https://docs.github.com/en/copilot)

Notable for **GitHub-native workflow** (issues → PRs), agent skills system, and
enterprise governance via permission profiles.

### Amazon Q Developer CLI

Source: [aws.github.io/amazon-q-developer-cli](https://aws.github.io/amazon-q-developer-cli/built-in-tools.html)

Notable for **granular permission system** per tool, persistent knowledge base, and
AWS-native integration.

### Windsurf (Codeium) — Cascade

Source: [docs.codeium.com/windsurf/cascade](https://docs.codeium.com/windsurf/cascade)

Notable for **flow-aware context** (tracks your editor actions), bidirectional terminal
integration, and auto-fix linting.

### Devin (Cognition AI)

Source: [docs.devin.ai](https://docs.devin.ai)

Notable for **full desktop environment** (shell + IDE + browser), multi-agent delegation
(coordinator spawns child Devins), and checkpoint-based rollbacks.

---

## Implementation Notes

Each new tool follows the existing pattern in YAC:

1. Add a struct to `src/tool_call/types.hpp`
2. Extend the `ToolCallBlock` variant with the new struct
3. Implement a `RenderX()` function in `src/presentation/tool_call/renderer.cpp`
4. Add a summary case in `ToolCallRenderer::BuildSummary()`
5. Wire the tool into `ChatService` or the provider layer for execution
