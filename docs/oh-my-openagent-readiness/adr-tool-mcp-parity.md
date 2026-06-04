# ADR: Tool and MCP parity strategy

Task checkbox: `- [ ] 5. Tool and MCP parity ADR`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This ADR is prep-only. It decides a future strategy for OMO built-in tools and built-in MCPs in YAC without adding MCP commands, tool schemas, browser fetch behavior, source files, or tests.

## Context

Task 1, [license-clean-room.md](license-clean-room.md), makes later implementation `Blocked pending license` until clean-room requirements and license compatibility are approved. Task 2, [upstream-feature-inventory.md](upstream-feature-inventory.md), records source-backed upstream families for built-in tools and built-in MCPs, including `grep/glob`, `LSP`, `AST-grep`, session tools, interactive bash/tmux, websearch, Context7, Grep.app, MCP resources, and skill-embedded MCPs. Task 3, [yac-gap-matrix.md](yac-gap-matrix.md), records YAC's local baseline and gaps.

YAC's baseline is substantial but not full OMO parity. `src/tool_call/executor_catalog.cpp` defines built-in tools for file read/write/edit, list_dir, `grep/glob`, `LSP` diagnostics/references/definition/rename/symbols, sub-agents, todo_write, ask_user, plan_exit, bash, web_fetch, and web_search. `src/chat/tool_round_runner.cpp` applies Plan-mode allowlists, approval prompts, rejection handling, and MCP tool dispatch. `src/mcp/mcp_manager.cpp` manages stdio/HTTP MCP sessions, tool catalogs, server/tool approval policy, OAuth, MCP resources listing, and resource reads. These files are the YAC-native baseline, not evidence of complete OMO tool/MCP parity.

Current user-facing MCP admin parity is narrower than manager capability. `src/cli/mcp_cli_dispatch.cpp` implements `list`, `add`, `auth`, `logout`, and `debug`. `src/app/mcp_command_handlers.cpp` shows `/mcp resources <server-id>` as a notice requiring an active connection rather than a full list/read UX.

MCP `remove/resources/read` mismatch: Spike first; manager-level MCP resources support and documentation mentions must not be described as complete user-facing CLI/TUI parity.

## Decision

Use a YAC-native parity strategy. Later work should extend the existing tool catalog and MCP manager only after behavior-only clean-room requirements are written from the pinned snapshot and after each tool class has an explicit approval/security model. Treat current YAC built-ins as a strong foundation, but do not copy upstream schemas, tool prompt text, manager code, or MCP registry wiring.

The future strategy is:

- Keep current file, search, LSP, shell, sub-agent, TODO, web, and MCP execution paths as the baseline for comparison.
- Add no new implementation during readiness work.
- Require a focused spike before any MCP `remove/resources/read`, AST-grep, Context7, Grep.app, skill-embedded MCPs, tmux shell, session tools, or broader web/docs/search integration is planned as build work.
- Keep websearch limited to an explicit provider/API-key/network policy decision; do not promise browser/JavaScript web fetch parity.

## Options

1. Treat YAC's existing tool and MCP manager support as enough and move directly to backlog implementation.
2. Port upstream tool/MCP names and schemas into YAC.
3. Use YAC-native tool/MCP parity with a spike for gaps, clean-room requirements, and security review.

Option 3 is selected. It respects YAC architecture, avoids SUL-1.0 source copying risk, and prevents manager-level MCP resources support from being overstated as CLI/TUI parity.

## Rejected Options

Direct upstream schema or prompt reuse is rejected because Task 1 says Do not copy upstream source, prompt text, schemas, hook logic, or implementation structure before license and clean-room review.

Immediate browser/tool expansion is rejected because this ADR must not promise browser/JavaScript web fetch parity and YAC currently has HTTP(S) web_fetch plus Exa-backed web_search, not browser automation.

Declaring MCP resources complete is rejected because the CLI dispatcher lacks `resources`, `read`, and `remove` subcommands and the TUI resources command is not a full listing/read path.

## YAC Mapping

| Surface | Baseline | Future parity treatment | Approval/security impact |
| --- | --- | --- | --- |
| File reads and directory/search discovery | `file_read`, `list_dir`, `grep/glob`, and read-only `LSP` tools are present in `src/tool_call/executor_catalog.cpp`. | Keep as the baseline for read-only local discovery. AST-grep should be evaluated as a separate structural-search spike rather than folded into grep. | Reads expose workspace contents. Keep workspace scoping, path validation, Plan-mode restrictions, output truncation, and no write approval bypass. |
| File writes and edits | `file_write`, `file_edit`, `lsp_rename`, and `plan_exit` require approval. | Future hashline or AST-aware edit work must be designed separately and should not weaken existing approval policy. | Writes mutate files. Require user approval, precise previews, path safety, and tests before any later implementation. |
| Shell and interactive execution | `bash` exists and requires approval; no interactive bash/tmux session tool is present. | Treat interactive bash/tmux as a spike because persistent terminal sessions affect lifecycle, cancellation, output capture, and TUI rendering. | Commands can read/write files, start processes, and access environment/network. Require approval, timeout/cancel controls, environment handling, and transcript boundaries. |
| LSP and AST-grep | YAC has several `LSP` operations; no built-in AST-grep tool is in the catalog. | Preserve LSP operations; spike AST-grep for dependency, language coverage, diagnostics, and edit-safety interactions. | LSP reads are low-risk but can expose source structure; rename writes require approval. AST-grep must distinguish read-only matching from rewrite operations. |
| Session tools and sub-agents | YAC has sub-agent and todo_write tools but no durable session tools for transcript/session search or recovery parity. | Defer session tools to the workflow/persistence ADR and backlog; do not build persistence in this ADR. | Session tools may read stored conversation data and task files. Require privacy review, retention rules, and approval for destructive or cross-session operations. |
| Network web/docs/search | YAC has web_fetch and opt-in Exa web_search; upstream inventory names websearch, Context7, and Grep.app built-in MCP families. | Treat Context7 and Grep.app as future MCP or provider integrations, not built-in equivalents today. Do not promise browser/JavaScript web fetch parity. | Network calls can disclose prompts, paths, queries, and URLs. Require explicit provider config, credentials handling, logging policy, and opt-in behavior. |
| MCP tools | MCP tools are merged into the tool catalog and invoked through `ChatServiceMcp` and `McpManager`; server/tool approval policy is supported. | Preserve server-scoped tool names and approval policy. Later built-in MCPs should be added only after clean-room behavior requirements. | MCP tools can read files, write files, run commands, or call network depending on the server. Server-level and per-tool approval must remain visible to the user. |
| MCP resources | `McpManager` can list and read MCP resources, but user-facing CLI/TUI parity is incomplete. | Spike CLI/TUI/admin semantics for listing resources, reading resources, remove flows, active-session needs, output truncation, and tests. | MCP resources can expose remote or local data through an MCP server. Reads need server identity, URI visibility, truncation, and privacy documentation. |
| skill-embedded MCPs | YAC has general configured MCP servers, not an OMO-style skill-embedded MCP lifecycle. | Spike whether skills should be allowed to embed MCP server definitions or whether YAC should keep explicit user-configured MCP only. | Skill-embedded MCPs can introduce hidden network/process capability. Require explicit enablement, provenance display, approval policy inheritance, and cleanup rules. |

## Later Implementation Prerequisites

- License and clean-room gate from Task 1 cleared for the specific behavior family.
- Behavior-only requirements for each future tool/MCP family, with pinned upstream citations and no copied schemas or prompt text.
- A security review for every class that can read files, write files, run commands, call network, or use MCP.
- A product decision for network providers: websearch, Context7, Grep.app, and any docs/search provider must be opt-in and configurable.
- A UX decision for interactive bash/tmux and MCP resources so TUI, headless, and CLI behavior are not inconsistent.
- Tests planned for `tests/test_executor_catalog.cpp`, `tests/test_mcp_manager.cpp`, `tests/test_yac_mcp_cli.cpp`, and `tests/test_approval_mcp.cpp` before implementation begins.

## Risks

- Overstating YAC baseline could cause Task 9 to mark tool/MCP parity ready when web/docs/search parity and MCP resource UX remain incomplete.
- Adding built-in MCPs without opt-in network and approval controls could leak queries, local paths, resource URIs, or credentials.
- Adding interactive terminal or session tools without persistence and cancellation design could leave processes running or produce unrecoverable state.
- Copying upstream schemas or prompts would violate the clean-room boundary and keep the work blocked.

## Status

Status: Spike first
