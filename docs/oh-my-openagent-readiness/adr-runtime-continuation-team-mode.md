# ADR: Runtime, Continuation, And Team Mode

Task checkbox: `- [ ] 6. Runtime, continuation, and Team Mode ADR`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This is a prep-only ADR for future YAC runtime orchestration. It does not implement nested subagents, background task persistence, continuation loops, task files, Team Mode UI, tmux integration, session recovery, source changes, tests, settings, or CLI flags.

## Context

Required readiness inputs:

- Task 1 license and clean-room guardrails: `docs/oh-my-openagent-readiness/license-clean-room.md` keeps implementation `Blocked pending license` until review clears behavior-only requirements.
- Task 2 upstream inventory: `docs/oh-my-openagent-readiness/upstream-feature-inventory.md` records source-backed upstream families for managers, hooks, background/task system, context injection/recovery, and documented Team Mode.
- Task 3 YAC gap matrix: `docs/oh-my-openagent-readiness/yac-gap-matrix.md` records YAC gaps for `nested subagents`, `task/session persistence`, `Team Mode visualization`, `richer planning/task graph`, and recovery-related workflow gaps.

Pinned upstream references used for behavior inventory only:

- Background agents, tasks, and Team Mode snapshot: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L55-L103
- Team Mode README UX: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/README.md#L256-L278
- Background task configuration: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/configuration.md#L448-L477
- Team Mode guide: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/guide/team-mode.md#L1-L133

Local YAC anchors reviewed:

- `src/chat/sub_agent_manager.cpp` owns in-memory foreground/background sub-agent sessions, bounded concurrency, timeouts, cancellation, progress events, and background-result continuation injection.
- `src/chat/agent_mode.cpp` allows `sub_agent` in Plan mode but filters mutating tools; `SubAgentManager` also excludes the `sub_agent` tool inside sub-agent prompt processors, which currently prevents nested subagents.
- `src/chat/chat_service.cpp` owns the main prompt queue, active-response cancellation, conversation reset, Plan/Build state, non-durable history, SQLite-backed runtime selections, and sub-agent continuation injection.
- `src/app/headless.cpp` runs one prompt through `ChatService`, optionally with `--plan`, `--auto-approve`, and `--cancel-after-ms`, then exits on `FinishedEvent`.
- `tests/test_sub_agent_manager.cpp`, `tests/test_sub_agent_executor.cpp`, and `tests/test_headless.cpp` cover capacity, cancellation, background progress, sub-agent tool preparation, no-approval behavior, and one-shot headless mode.

## Current YAC Versus OMO Runtime

YAC `SubAgentManager` differs from the OMO runtime family by treating sub-agents as bounded in-process `jthread` sessions owned by the current `ChatService`. It supports foreground/background execution, timeout and cancel signals, progress cards, MCP snapshot adapters, and result injection into the parent conversation, but it has no durable task store, cross-process recovery, named team roles, mailbox files, task graph, or nested delegation.

YAC headless mode differs by being a single-prompt entrypoint. It creates one `ChatService`, submits one inlined prompt, optionally auto-approves tools or starts in Plan mode, waits for completion, and exits. It is not a script runner, continuation loop, work queue runner, task-file consumer, or session-resume surface.

YAC `ChatService` differs by keeping orchestration local to one process and one active conversation. It queues prompts in memory, persists selected runtime settings in SQLite, supports active-response cancellation and reset, injects completed background sub-agent results as user-role continuation messages, and writes approved Plan output to `.opencode/plans/*.md`. It does not persist full transcripts, pending prompts, background task state, loop state, or recovery metadata.

OMO runtime claims in the readiness inventory are broader: background/task system configuration, manager composition, continuation/recovery hooks, file-backed task flows, and optional Team Mode/tmux-style visualization. Those are behavior families to design from clean-room requirements, not code or prompt surfaces to copy.

## Decision

Future parity should proceed only as a staged runtime design, not a direct Team Mode or continuation-loop port. The stage order is:

1. Keep YAC's current in-process `SubAgentManager` behavior as the supported baseline.
2. Specify durable task/session state before adding background task persistence, continuation loop cancellation beyond current active-response cancellation, or session recovery.
3. Keep nested delegation disabled until a separate recursion, approval, and resource-budget design is accepted.
4. Classify Team Mode visualization as `Spike first` because YAC does not yet have the persistence, team-role model, terminal layout strategy, or recovery substrate needed for truthful multi-agent visualization.
5. Defer implementation until Task 1 clean-room/license review and later backlog work produce behavior-only requirements.

## Decision: nested subagents

Future treatment: keep `nested subagents` disabled for now. Later parity may introduce controlled recursion only after an ADR defines depth limits, per-agent tool inheritance, cancellation propagation, approval policy, and parent/child result ownership.

YAC constraint: `SubAgentManager` currently constructs sub-agent prompt processors with `sub_agent` excluded, and `agent_mode.cpp` only describes top-level Plan/Build tool filtering. There is no recursion-depth counter, nested approval graph, or lineage store.

Risk: enabling nested subagents without lineage and budget controls can create runaway agent trees, ambiguous approvals, stale parent contexts, and cancellations that stop only part of the delegation tree.

Clean-room note: future requirements may describe desired recursion behavior, but must not copy upstream agent prompts, team tools, or manager logic.

## Decision: background task persistence

Future treatment: require a persistence design before background task persistence is added. Prefer a YAC-native model that records task identity, parent chat session, prompt summary, lifecycle status, cancellation state, result summary, timestamps, and recovery eligibility. The storage choice should be decided with Task 7's workflow/persistence ADR, likely SQLite for authoritative state plus optional task files only for user-visible plans or exported handoff material.

YAC constraint: active sub-agent sessions are held in memory inside `SubAgentManager`; completed sessions are removed from the active map; `ChatService` persists provider/model selections but not pending prompts, sub-agent histories, or task queues.

Risk: adding task files without a durable authoritative store could make task status appear recoverable when the underlying worker, approvals, tool outputs, and provider stream cannot actually resume.

Clean-room note: task-file behavior must be specified from YAC user needs and clean-room summaries, not upstream file formats or manager implementation.

## Decision: continuation loop cancellation

Future treatment: treat continuation loop cancellation as a new runtime contract, not an extension of the existing stop button alone. Later work should define loop IDs, explicit stop commands, stop-on-error policy, max-iteration or max-duration limits, cancellation propagation to active sub-agents and tool approvals, and a visible terminal state after cancellation.

YAC constraint: current `ChatService::CancelActiveResponse()` cancels one active response and pending approvals; `ResetConversation()` cancels sub-agents and clears in-memory TODO state; background completions can inject continuation messages, but no durable loop controller or queued-loop owner exists.

Risk: an OMO-style continuation loop without a first-class cancel contract can keep injecting follow-up work after a user believes work has stopped, or can leave background results detached from the loop that spawned them.

Clean-room note: slash-command naming and loop semantics should be specified in YAC terms after license review, not copied from upstream commands or prompts.

## Decision: Team Mode visualization

Future treatment: `Spike first`. Team Mode visualization must not be implemented until a spike proves an exact YAC fit or records a spike prerequisite for persistence, recovery, role modeling, and terminal layout. The spike should compare at least three YAC-native options: enhanced existing sub-agent cards, a dedicated FTXUI team board, and an optional external tmux-like pane strategy.

YAC constraint: current presentation code renders chat, tool cards, sub-agent progress, overlays, and MCP status in one TUI surface. YAC has no team-role model, mailbox/task-file model, tmux session manager, multi-pane ownership, or recovered task graph to visualize.

Risk: Team Mode without persistence can create misleading or non-recoverable UI state, where users see agents, panes, or team roles that cannot be resumed, reconciled with source changes, or safely cancelled after process exit.

Clean-room note: do not copy upstream team tools, prompts, tmux orchestration, or runtime code. A future spike may cite pinned upstream behavior only as inventory.

## Decision: session recovery

Future treatment: defer session recovery until durable runtime state is designed. Later recovery should distinguish restartable state from non-restartable state: chat transcript, user-visible task records, completed background results, active approvals, tool-result audit, provider stream status, Plan/Build mode, and active plan path. Recovery should be honest about tasks that can only be marked interrupted rather than resumed.

YAC constraint: `ChatService` generates a chat session ID and can persist selected app state, but its conversation history, prompt queue, active response, sub-agent sessions, and pending approvals are process-local. Headless mode exits after one prompt and has no resume argument.

Risk: claiming session recovery before transcript and task persistence exist could silently drop tool results, resurrect stale approvals, replay destructive prompts, or produce an inaccurate recovered conversation.

Clean-room note: recovery behavior must be product-designed around YAC's SQLite and Plan/Build model, not around upstream hook internals.

## Options

| Option | Summary | Outcome |
| --- | --- | --- |
| Direct port | Copy or translate upstream background, continuation, Team Mode, and recovery mechanisms. | Rejected: violates prep-only and clean-room guardrails; conflicts with YAC C++ architecture. |
| UI-first Team Mode | Build Team Mode visualization on current in-memory sub-agent events. | Rejected: risks misleading non-recoverable UI state without task/session persistence. |
| Persistence-first runtime | Specify YAC-native durable task/session contracts before loops, Team Mode, and recovery. | Chosen for later design. |
| Keep current runtime only | Treat current sub-agents and headless mode as enough parity. | Rejected: Task 3 records mandatory gaps for nested subagents, task/session persistence, continuation injection, and Team Mode visualization. |

## Rejected Options

- Implement Team Mode/tmux-like UI during readiness: rejected because this task is prep-only and no runtime, UI, or persistence source files may change.
- Add task files as a lightweight compatibility shim: rejected because file-backed status without authoritative runtime state would be misleading.
- Allow nested subagents by simply removing the excluded tool: rejected because recursion needs depth, approval, cancellation, and lineage controls.
- Treat headless `--cancel-after-ms` as continuation loop cancellation: rejected because it cancels one active response, not a durable loop or task graph.

## YAC Mapping

| Future area | Current YAC anchor | Current state | Required later readiness |
| --- | --- | --- | --- |
| nested subagents | `src/chat/sub_agent_manager.cpp`, `src/chat/agent_mode.cpp` | Disabled by sub-agent tool exclusion and no recursion model. | Recursion ADR with limits, lineage, approvals, cancellation, and tests. |
| background task persistence | `src/chat/sub_agent_manager.cpp`, `src/chat/chat_service.cpp` | In-memory sessions and injected completion messages. | Storage contract tied to Task 7 workflow/persistence decisions. |
| continuation loop cancellation | `src/chat/chat_service.cpp`, `src/app/headless.cpp` | Active-response cancellation and reset only. | Loop owner, loop IDs, stop commands, policy limits, and propagation rules. |
| Team Mode visualization | `src/presentation/chat_ui.cpp`, sub-agent event rendering | No team board, tmux session, role visualization, or mailbox model. | Spike first with persistence and recovery prerequisites. |
| session recovery | `src/chat/chat_service.cpp`, `src/chat/sqlite_state_store.cpp` | Runtime selections persisted; conversation/task state not persisted. | Honest recovery model for transcript, tasks, approvals, plans, and interrupted work. |

## Later Implementation Prerequisites

- License and clean-room review must clear behavior-only requirements from Task 1 before implementation begins.
- Task 7 must decide whether runtime/task persistence uses SQLite, task files, both, or neither.
- Safety/approval work must define how approvals, tool permissions, and cancellation propagate through background tasks and any future nested delegation.
- UI work must define whether Team Mode belongs in the existing FTXUI chat surface, an alternate TUI view, or an optional external terminal integration.
- Tests must cover cancellation, recovery honesty, no duplicate continuation injection, and no nested delegation unless explicitly enabled.

## Risks

- Runtime scope creep: background persistence, loops, Team Mode, and recovery are interdependent and can become a hidden implementation project if not staged.
- Safety ambiguity: nested delegation and continuation loops can run tools after the user's visible context has changed.
- Recovery overclaim: persisted labels or files can imply resumability even when provider streams, approvals, and tool calls cannot be replayed safely.
- UX mismatch: tmux-like visualization may not fit YAC's FTXUI-first terminal app or headless workflows.
- License risk: upstream team tools, prompts, and runtime code remain SUL-1.0 material; readiness may summarize behavior only.

## Status

Status: Spike first
