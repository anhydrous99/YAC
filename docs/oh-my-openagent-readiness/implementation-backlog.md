# Implementation Backlog And Sequencing Matrix

Task checkbox: `- [ ] 9. Implementation backlog and sequencing matrix`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This is a prep-only backlog for later planning. It synthesizes Tasks 1-8 into future build epics, prerequisites, blockers, clean-room constraints, and future test themes. It does not create production implementation tasks, source edits, tests, settings changes, CLI changes, or immediate TODOs.

## Source Inputs

- Task 1: [license-clean-room.md](license-clean-room.md) keeps later production implementation blocked until license compatibility and clean-room review clear behavior-only requirements.
- Task 2: [upstream-feature-inventory.md](upstream-feature-inventory.md) covers all 18 canonical upstream feature families, with no unclear family.
- Task 3: [yac-gap-matrix.md](yac-gap-matrix.md) records mandatory gaps including `MCP remove/resources/read`, `nested subagents`, `richer planning/task graph`, `headless script runner`, `web/docs/search parity`, `task/session persistence`, `Team Mode visualization`, `hooks/continuation injection`, `hashline edit semantics`, and `telemetry policy`.
- Task 4: [adr-agent-config-parity.md](adr-agent-config-parity.md) selects `Proceed later` for a YAC-native agent/category policy layer.
- Task 5: [adr-tool-mcp-parity.md](adr-tool-mcp-parity.md) selects `Spike first` for tool and MCP parity gaps.
- Task 6: [adr-runtime-continuation-team-mode.md](adr-runtime-continuation-team-mode.md) selects `Spike first` for runtime, continuation, nested delegation, and Team Mode work.
- Task 7: [adr-workflow-persistence-headless.md](adr-workflow-persistence-headless.md) selects `Proceed later` for hybrid SQLite plus filesystem workflow persistence, with no readiness migration.
- Task 8: [adr-safety-hooks-ux.md](adr-safety-hooks-ux.md) selects `Spike first` for hooks, hashline edits, notifications, browser/web tooling, and approval changes, while telemetry is blocked pending explicit product decision.

## Dependency Order

1. `license gate`: must clear before any epic becomes implementation-ready.
2. `config/agent registry`: defines policy names, model fallback, and permissions needed by runtime and tools.
3. `workflow/persistence/headless`: defines durable workflow, task, conversation, and headless-run substrate.
4. `tools/MCP parity`: can spike MCP admin gaps and tool families in parallel with runtime design after the license gate, but security policy must align with config and safety decisions.
5. `runtime/background/team mode`: depends on workflow persistence for truthful recovery and on config policy for agent/category routing.
6. `safety/hooks/UX`: must shape approvals, hooks, hashline edits, browser/network behavior, notifications, and telemetry before broad rollout.
7. `docs/testing parity`: follows accepted designs so docs and tests describe chosen YAC behavior, not upstream names or draft spikes.
8. `installer/admin/doctor parity`: should be last or near-last because it exposes only features that passed the license gate, product decisions, and tests.

## Sequencing Matrix

| Order | Epic | Main dependency | Main blocker | Readiness status |
| --- | --- | --- | --- | --- |
| 1 | license gate | Completed Task 1 artifact | License compatibility and clean-room approval | not ready |
| 2 | config/agent registry | license gate | YAC-native names, schema, permissions, fallback policy | not ready |
| 3 | workflow/persistence/headless | license gate, config policy | Storage ADR, migration plan, headless runner design | not ready |
| 4 | tools/MCP parity | license gate, safety policy | MCP remove/resources/read spike and network/tool review | not ready |
| 5 | runtime/background/team mode | workflow persistence, config policy | Nested delegation design, recovery honesty, Team Mode spike | not ready |
| 6 | safety/hooks/UX | license gate, tool/runtime designs | Telemetry product decision, hook phases, UX privacy policy | not ready |
| 7 | docs/testing parity | accepted epic designs | Clean-room docs/test plans, no copied upstream docs or tests | not ready |
| 8 | installer/admin/doctor parity | tested feature set | Product packaging decisions and admin surface scope | not ready |

## Epic: license gate

Upstream families: all 18 Task 2 families, especially `11 agents`, `categories`, `hooks`, `background/task system`, `Team Mode`, `hashline edits`, `built-in tools`, `built-in MCPs`, `telemetry`, and `tests/docs/site`.

YAC modules: readiness documentation under `docs/oh-my-openagent-readiness/`, future behavior-only requirements, future legal/product review records, and later implementation plans that cite Task 1.

Prerequisites: Task 1 clean-room artifact, pinned upstream snapshot, a license compatibility decision for the intended YAC use, and a clean-room process that separates upstream reviewers from implementers.

Blockers: `Blocked pending license`; no later production work may start from upstream source, prompt text, schemas, command templates, docs prose, or hook bodies.

Clean-room constraints: summarize behavior only, cite pinned URLs only, keep YAC Apache-2.0 materials separate, produce YAC-authored requirements, and compare future output by behavior rather than source similarity.

Future tests: no production tests now. Later planning should add artifact checks that every implementation plan cites the license gate, uses pinned evidence, names a behavior-only requirements source, and avoids copied upstream text or code.

Ready status: not ready.

Suggested future category: writing for requirements and review artifacts, then unspecified-high for implementation gate verification.

## Epic: config/agent registry

Upstream families: `11 agents`, `categories`, `config discovery/migration`, `model fallback/permissions`, `mode words/commands`, and `package/dependency structure` where config packaging affects public settings.

YAC modules: future policy layer near sub-agent request construction, `src/chat/config.cpp`, `src/chat/settings_registry.cpp`, `src/app/provider_factory.cpp`, `src/chat/agent_mode.cpp`, `settings.example.toml`, and `docs/configuration.md`.

Prerequisites: license gate clearance, behavior-only agent/category requirements, YAC product naming decision, schema and precedence decision, fallback policy, permission composition with Plan/Build, and migration policy.

Blockers: `Blocked pending license`; Task 4 status is `Proceed later`, but public names, config schema, fallback chains, and per-agent tool policy are undecided.

Clean-room constraints: do not mirror upstream agent names, prompts, role text, category descriptions, config examples, fallback chains, or permission schemas. Define YAC-native policy terms and YAC-authored prompts if later approved.

Future tests: config parsing and validation, env/TOML/SQLite/default precedence, agent/category policy resolution, provider fallback failure paths, permission narrowing, Plan/Build interaction, docs examples, and migration failure handling.

Ready status: not ready.

Suggested future category: deep for schema and policy design, then quick only for narrow follow-up fixes after architecture is accepted.

## Epic: tools/MCP parity

Upstream families: `built-in tools`, `built-in MCPs`, `hashline edits`, `browser/tmux/notifications UX`, `model fallback/permissions`, `context injection/recovery`, and `tests/docs/site`.

YAC modules: `src/tool_call/executor_catalog.cpp`, `src/chat/tool_round_runner.cpp`, `src/mcp/mcp_manager.cpp`, `src/cli/mcp_cli_dispatch.cpp`, `src/app/mcp_command_handlers.cpp`, `src/presentation/mcp/mcp_resources_command.cpp`, and approval tests under `tests/`.

Prerequisites: license gate clearance, security review by tool class, MCP `remove/resources/read` spike, network provider policy for web/docs/search, AST-grep decision, skill-embedded MCP decision, interactive bash/tmux lifecycle decision, and approval/UX alignment with Task 8.

Blockers: `Blocked pending license`; Task 5 status is `Spike first`; MCP manager resource support is not the same as user-facing CLI/TUI/admin parity for `remove/resources/read`.

Clean-room constraints: do not copy upstream tool schemas, MCP registry code, prompt text, manager wiring, or command templates. Treat Context7, Grep.app, browser, and AST-grep behavior as clean-room integration requirements rather than upstream implementation ports.

Future tests: tool catalog contract tests, MCP CLI/TUI resource list and read tests, MCP remove tests, approval policy tests for files/shell/network/MCP, web/docs/search provider tests, AST-grep read and rewrite safety tests if selected, and headless approval behavior tests.

Ready status: not ready.

Suggested future category: deep for spikes and security design, unspecified-high for QA of tool and MCP surfaces.

## Epic: runtime/background/team mode

Upstream families: `background/task system`, `Team Mode`, `managers`, `hooks`, `context injection/recovery`, `browser/tmux/notifications UX`, `mode words/commands`, and `11 agents`.

YAC modules: `src/chat/sub_agent_manager.cpp`, `src/chat/chat_service.cpp`, `src/chat/agent_mode.cpp`, `src/app/headless.cpp`, `src/presentation/chat_ui.cpp`, `src/presentation/chat_ui_components.cpp`, and sub-agent/headless tests.

Prerequisites: license gate clearance, config/agent policy decision, workflow persistence substrate, recursion-depth design, lineage model, approval propagation, cancellation propagation, recovery honesty model, Team Mode visualization spike, and terminal layout decision.

Blockers: `Blocked pending license`; Task 6 status is `Spike first`; `nested subagents` remain disabled and Team Mode visualization lacks durable task/session state.

Clean-room constraints: do not copy upstream team tools, prompts, tmux orchestration, task-file formats, manager logic, or runtime control flow. Define YAC-native roles, task states, and UI surfaces.

Future tests: no nested delegation unless explicitly enabled, recursion depth limits, cancellation propagation, background task persistence, session interruption and recovery status, Team Mode rendering if selected, no duplicate continuation injection, and approval lineage tests.

Ready status: not ready.

Suggested future category: deep or ultrabrain for runtime design, visual-engineering only if a later TUI Team Mode surface is approved.

## Epic: workflow/persistence/headless

Upstream families: `mode words/commands`, `background/task system`, `context injection/recovery`, `config discovery/migration`, `tests/docs/site`, and `package/dependency structure` where workflow artifacts affect packaging.

YAC modules: `src/chat/plan_session.cpp`, `src/chat/chat_service.cpp`, `src/chat/sqlite_state_store.cpp`, `src/app/headless.cpp`, `src/presentation/slash_command_registry.cpp`, `src/app/prompt_slash_commands.cpp`, `.opencode/plans/`, and future workflow artifacts.

Prerequisites: license gate clearance, storage ADR or migration plan, SQLite schema versioning plan, filesystem artifact layout, retention and privacy policy, headless script runner design, approval persistence model, and Plan/Build transition rules.

Blockers: `Blocked pending license`; Task 7 status is `Proceed later`, but no storage migration, task-file layout, full conversation persistence model, or headless script runner exists in readiness.

Clean-room constraints: do not copy upstream workflow command templates, loop prompts, task file layouts, recovery hooks, or handoff prose. Preserve the exact guardrail from Task 7: no persistence migration implementation occurs in readiness work.

Future tests: SQLite migration success and rollback, workflow run lifecycle, task file reconciliation, full conversation checkpointing, headless script bounds and exit codes, Plan/Build transition safety, approval persistence, loop cancellation, and recovery of interrupted sessions.

Ready status: not ready.

Suggested future category: deep for storage and headless architecture, unspecified-high for migration and recovery QA.

## Epic: safety/hooks/UX

Upstream families: `hooks`, `hashline edits`, `browser/tmux/notifications UX`, `telemetry`, `built-in tools`, `context injection/recovery`, `Team Mode`, and `model fallback/permissions`.

YAC modules: `src/presentation/chat_ui.cpp`, `src/presentation/chat_ui_overlay_state.cpp`, `src/presentation/chat_ui_components.cpp`, `src/chat/chat_service_request_builder.cpp`, `src/chat/chat_service_compactor.cpp`, `src/tool_call/executor_catalog.cpp`, web tool modules, and renderer/approval/web tests.

Prerequisites: license gate clearance, hook lifecycle map, approval policy table, hashline edit spike, notification privacy policy, browser/web tooling spike, telemetry product/privacy decision, and UX choices for TUI versus headless surfaces.

Blockers: `Blocked pending license`; Task 8 status is `Spike first`; telemetry is `Blocked pending explicit product decision` and must not be silently accepted.

Clean-room constraints: do not copy upstream hook bodies, lifecycle names, hash algorithms, comment checker prompts, privacy prose, notification behavior, browser tooling code, or approval semantics. Write YAC-specific safety contracts.

Future tests: hook phase isolation if hooks are approved, approval rendering and headless behavior, hashline stale-anchor failure cases, comment-checker behavior if selected, notification redaction, browser/private-network blocking, telemetry-disabled proof if telemetry is ever approved, and compaction/truncation preservation.

Ready status: not ready.

Suggested future category: unspecified-high for safety review and QA, deep for hook and browser spikes.

## Epic: docs/testing parity

Upstream families: `tests/docs/site`, `editions/install/uninstall`, `built-in tools`, `built-in MCPs`, `mode words/commands`, `context injection/recovery`, and every family whose later implementation becomes user-visible.

YAC modules: `README.md`, `docs/usage.md`, `docs/configuration.md`, `docs/mcp.md`, `docs/architecture.md`, `tests/BUILD.bazel`, `tests/yac_test_macros.bzl`, future docs pages, and future parity test targets.

Prerequisites: license gate clearance, accepted behavior-only requirements, selected epic designs, YAC-authored docs outline, test strategy by surface, and decision on whether any docs site or search experience belongs in YAC.

Blockers: `Blocked pending license`; upstream docs and tests are SUL-1.0 material and cannot be copied, and the later feature designs are not accepted yet.

Clean-room constraints: write original YAC docs and tests from behavior requirements, not upstream prose or test bodies. Keep pinned upstream citations in readiness artifacts, not copied reference text in public user docs unless approved.

Future tests: artifact validators, CLI/TUI/headless integration tests, MCP/admin tests, config examples, docs link checks if added, behavior-driven tests for each implemented epic, and regression tests that prove prep-only claims did not become implementation drift.

Ready status: not ready.

Suggested future category: writing for docs, unspecified-high for QA report and full gate execution.

## Epic: installer/admin/doctor parity

Upstream families: `editions/install/uninstall`, `config discovery/migration`, `package/dependency structure`, `built-in MCPs`, `model fallback/permissions`, and `tests/docs/site`.

YAC modules: future installer or packaging docs, `src/main.cpp`, `src/cli/mcp_cli_dispatch.cpp`, provider auth CLI paths, `settings.example.toml`, `docs/configuration.md`, `docs/mcp.md`, Bazel packaging or release scripts if later selected, and future doctor/admin commands if approved.

Prerequisites: license gate clearance, product decision on whether YAC needs edition-like packaging, installer scope, uninstall scope, config migration policy, doctor checks, admin command inventory, and completed tests for features exposed by the doctor.

Blockers: `Blocked pending license`; YAC has no edition-aware installer, uninstall flow, admin doctor, or split Ultimate/Light packaging surface, and later feature epics are not complete enough to expose through a doctor.

Clean-room constraints: do not copy upstream package metadata, installer scripts, command templates, config migration logic, or docs prose. If this epic proceeds, use YAC-native packaging and admin checks.

Future tests: install and uninstall dry-run tests if supported, doctor output tests, config validation tests, MCP admin command tests, provider auth status tests, release packaging smoke tests, and docs examples for supported platforms.

Ready status: not ready.

Suggested future category: deep for installer/admin design, quick for narrow CLI checks after product scope is approved.

## Readiness Summary

No epic is ready for implementation today. The backlog is decision-complete enough for Task 10 QA readiness reporting, but every later build epic remains behind the license gate plus at least one product, architecture, security, or spike prerequisite. This is intentional: the readiness package prepares later work without adding production source, tests, settings, or immediate implementation TODOs.
