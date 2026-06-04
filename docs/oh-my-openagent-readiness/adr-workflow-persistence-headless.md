# Workflow, Persistence, And Headless ADR

Task checkbox: `- [ ] 7. Workflow, persistence, and headless ADR`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This is a prep-only ADR. It decides a future strategy for OMO-style workflow commands, plan/task persistence, full conversation persistence, headless scripting, and Plan/Build interaction in YAC. It does not implement commands, storage migrations, CLI flags, source changes, prompt commands, or tests. The readiness guardrail is exact: no persistence migration implementation occurs in readiness work.

## Context

Task 1 establishes that upstream behavior can be inventoried and summarized, but implementation must follow clean-room requirements and must not copy upstream source, prompts, hook bodies, command templates, or docs prose. Task 2 inventories OMO workflow command families at pinned references, including `/start-work`, `/ralph-loop`, `/ulw-loop`, `/stop-continuation`, `/handoff`, command templates, and recovery behavior. Task 3 maps YAC as Partial for mode words/commands, background/task system, context injection/recovery, and headless operation, with mandatory gaps for `headless script runner`, `task/session persistence`, `richer planning/task graph`, and full conversation recovery.

Pinned upstream evidence used for this ADR:

- Workflow commands: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L468-L575
- Context injection and compatibility behavior: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L1042-L1094

Local YAC evidence used for this ADR:

- `src/chat/plan_session.cpp`: creates one plan path under `.opencode/plans` from the first Plan prompt and writes the approved plan.
- `src/chat/chat_service.cpp`: starts a chat session ID in memory, queues prompts, creates a plan path when Plan mode receives a prompt, applies `plan_exit`, writes the plan, and switches to Build.
- `src/chat/sqlite_state_store.cpp`: SQLite schema version 1 stores provider profiles, provider credentials, and generic app state only.
- `src/app/headless.cpp`: `yac run` submits one prompt, can start in Plan mode, can auto-approve tools, can cancel after a timer, and prints streaming output.
- `src/presentation/slash_command_registry.cpp` and `src/app/prompt_slash_commands.cpp`: built-in slash commands plus user prompt files from `~/.yac/prompts/*.toml`; no OMO workflow commands are built in.
- `tests/test_headless.cpp` and `tests/test_prompt_slash_commands.cpp`: coverage anchors for one-shot Build/Plan headless behavior and prompt slash command dispatch.

## Decision

Status: Proceed later

Future workflow parity should use a hybrid persistence model: SQLite for durable session/task/conversation indexes and lifecycle metadata, plus filesystem task and plan files for human-readable workflow artifacts. This means workflow parity needs both SQLite schema changes and filesystem task files later. It does not need neither, because current in-memory state cannot recover work across processes. It should not use SQLite-only storage, because plans, handoffs, and task artifacts need readable files that can be reviewed, linked, and version-controlled when appropriate. It should not use filesystem-only storage, because full conversation persistence, recovery status, background task state, provider/model selections, and cross-process locking need structured metadata.

The current `.opencode/plans/*.md` single-plan model should remain as the Plan/Build minimum path for ordinary YAC sessions. Later parity should extend around it rather than replace it: keep one active plan per active Plan session, but add a future workflow run record that can link one or more plan files, task files, handoff files, and conversation checkpoints. The plan file remains the user-facing approved plan artifact; the workflow record becomes the durable coordination layer.

## Options

| Option | Storage choice | Fit for workflow commands | Fit for full conversation persistence | Decision |
| --- | --- | --- | --- | --- |
| Keep current state only | Existing SQLite app/provider state plus `.opencode/plans/*.md` | Insufficient for `/start-work`, loops, handoff, and task recovery | Insufficient; history and active work are process-local | Rejected |
| SQLite-only workflow store | New SQLite tables for tasks, plans, loops, handoffs, conversations | Strong indexing, locking, and recovery | Strong for structured replay | Rejected as user-hostile for plans/tasks that should be readable artifacts |
| Filesystem-only workflow store | `.opencode/plans` plus future task/handoff files | Strong for readable artifacts | Weak for locking, status indexes, and conversation replay | Rejected as too fragile for background/headless recovery |
| Hybrid durable workflow store | SQLite metadata plus filesystem plans/tasks/handoffs | Strong for workflow orchestration and auditability | Strong if conversations are indexed in SQLite with file artifact links | Chosen |

## Compatibility Matrix

| Behavior | TUI later behavior | headless later behavior | Compatibility decision |
| --- | --- | --- | --- |
| Start from a plan | TUI can enter Plan mode, approve `plan_exit`, then use the approved `.opencode/plans/*.md` file as the visible handoff into Build. | headless can run a future scripted workflow from a named plan or prompt without opening the TUI. | Preserve the same plan artifact semantics; expose different controls for interactive vs non-interactive use. |
| OMO-style `/start-work` | TUI slash or prompt command can select a stored plan/workflow run and begin Build-mode execution with progress visible in the transcript. | headless should use explicit command/runner input rather than interactive slash dispatch. | Same workflow record and task files; different invocation surface. |
| Continuation loops | TUI can show loop state, cancellation affordances, and warnings before repeated autonomous turns. | headless must have bounded iteration, timeout, approval, and exit-code behavior suitable for scripts. | Require shared persisted loop metadata and stricter headless bounds. |
| `/handoff` equivalent | TUI can create a readable handoff artifact from the current conversation, active plan, and task status. | headless can emit or write a handoff artifact at the end of a script. | Store handoff as filesystem artifact linked from SQLite metadata. |
| Full conversation persistence | TUI can resume prior conversation state after restart once future schema exists. | headless can resume or inspect a named run only when explicitly requested by future flags/commands. | Do not silently make every one-shot run stateful; persistence must be deliberate. |
| Approval behavior | TUI keeps interactive approval prompts and Plan/Build mode reminders. | headless keeps deterministic behavior: fail without auto-approval unless a future script policy states otherwise. | Approval semantics stay surface-specific but read from one policy record. |
| Plan/Build transition | TUI approval of `plan_exit` writes the plan and switches to Build. | headless `--plan` can still produce an approved plan path and then finish or continue only under a future explicit workflow runner. | Current transition remains; later continuation after Plan requires explicit workflow design. |

## YAC Mapping

YAC already has a useful split: Plan mode controls tool availability, `PlanSession` writes approved plans to `.opencode/plans`, and SQLite stores provider/app state. The gap is that these are not a durable workflow system. A generated chat session ID is not a persisted conversation. The in-memory `ChatHistoryStore`, prompt queue, active response state, TODO state, and background task progress do not survive process exit. Current headless mode is intentionally one-shot and should not be described as a script runner.

The future implementation should define these concepts before any code change:

- Workflow run: durable unit that ties together a command invocation, selected plan, tasks, conversation checkpoints, approvals, and completion state.
- Task file: human-readable filesystem artifact for later work items, loop state summaries, or handoff material.
- Conversation checkpoint: durable representation of messages, tool calls, tool results, compaction boundaries, model/provider metadata, and recovery status.
- Plan link: pointer from a workflow run to the existing `.opencode/plans/*.md` approved plan artifact.
- Surface policy: whether a workflow can run in TUI, headless, or both, with approval and cancellation rules.

## Plan/Build Interaction

Plan/Build remains the safety boundary. Plan mode should keep read-only tool restrictions and `plan_exit` approval. Build mode should remain the place where code-changing tools can run. Future OMO-style workflow commands must not blur this boundary. `/start-work`-style behavior should require an approved plan or a consciously Build-mode workflow run. Loop commands should not auto-escalate from Plan to Build without the same approval trail that current `plan_exit` establishes.

The current single active plan path should remain during a session because it is simple, auditable, and already covered by tests. The extension should be at the workflow layer: a run can link to multiple artifacts over time, but a live Plan session still has one active plan until approved or abandoned.

## Persistence Strategy

Future workflow parity needs both SQLite schema changes and filesystem task files.

SQLite should later hold structured metadata: workflow run IDs, conversation IDs, message ordering, tool-call/result linkage, plan file links, task file links, loop status, approval decisions, timestamps, and recovery markers. This requires a future migration and tests outside readiness scope.

Filesystem artifacts should later hold human-facing material: `.opencode/plans/*.md` approved plans, task/handoff summaries, and possibly workflow-readable task lists. Task files should be designed as YAC-native artifacts, not copied from upstream layouts or prompt text.

This ADR deliberately does not define a concrete SQLite schema. It records the architectural direction only. The exact guardrail remains: no persistence migration implementation occurs in readiness work.

## Headless Strategy

`yac run` should continue to mean one prompt by default. Future headless scripting should be a separate, explicit workflow surface so existing shell users do not get implicit session persistence, hidden loops, or surprise file mutations. A future runner may accept named workflow runs, plan files, bounded loop configuration, cancellation policy, approval policy, and output format, but those are later design items.

Headless parity must be deterministic: non-interactive approval should fail unless explicitly allowed, loop commands need maximum iterations or timeouts, and resumed runs must identify the exact workflow/conversation target. TUI affordances such as slash menus, overlays, and visible cancellation can map to headless flags or subcommands later, but this readiness task does not add CLI flags.

## Rejected Options

- Do not port upstream workflow commands by copying prompt files or command templates; Task 1 clean-room rules block that path.
- Do not treat current `~/.yac/prompts/*.toml` prompt commands as full OMO workflow parity; they submit prompt text but do not persist task/session state.
- Do not treat current SQLite app state as conversation persistence; it has no message, task, plan-run, or tool-result tables.
- Do not replace `.opencode/plans/*.md` with opaque database rows; the readable approved-plan artifact is valuable.
- Do not add workflow commands, SQLite migrations, CLI flags, source files, or tests during readiness.

## Later Implementation Prerequisites

- A clean-room behavior requirements document citing Task 1 before any command or prompt behavior is implemented.
- A concrete storage ADR or migration plan for SQLite schema versioning, locking, retention, privacy, export, and corruption recovery.
- A filesystem artifact layout for task and handoff files that coexists with `.opencode/plans`.
- A headless workflow runner design that separates one-shot `yac run` from durable scripted runs.
- Tests for TUI and headless Plan/Build transitions, recovery, approval persistence, loop cancellation, and migration failure handling.

## Risks

- Confusing compaction or runtime session IDs with full conversation persistence could lead to false recovery claims.
- Adding loops before durable cancellation and approval state could create runaway autonomous behavior.
- Persisting full conversations may store sensitive prompt, file, and tool-result data; privacy and retention policy must be explicit.
- A schema migration without careful versioning could break existing `~/.yac/state.sqlite` provider credentials and app state.
- Filesystem task artifacts can drift from SQLite metadata unless future code defines ownership and reconciliation rules.

## Status

Status: Proceed later

Proceed later with a hybrid persistence strategy. Keep current `.opencode/plans/*.md` Plan/Build behavior as the minimum model, extend around it with future workflow-run metadata and filesystem task/handoff artifacts, and preserve the guardrail that no persistence migration implementation occurs in readiness work.
