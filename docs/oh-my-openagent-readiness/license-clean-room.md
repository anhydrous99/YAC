# License and clean-room readiness guardrails

This prep artifact covers `oh-my-openagent` at snapshot commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`. It frames engineering risk for YAC readiness work. It is not formal legal advice.

## Pinned references

- SUL-1.0 license terms: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/LICENSE.md#L10-L29
- Privacy policy telemetry details: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/legal/privacy-policy.md#L17-L48
- README telemetry opt-out notes: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/README.md#L168-L176

## Readiness position

The upstream project is under SUL-1.0 for the relevant materials. The license text allows use, copying, distribution, and derivative work only under stated limits, including internal business, non-commercial, and no-charge distribution constraints. YAC remains Apache-2.0. Until compatibility is reviewed and approved for the intended YAC use, any implementation based on upstream behavior is `Blocked pending license`.

This means readiness work may describe risk, inventory behavior, and plan a safe path. It must not treat upstream TypeScript, prompts, hook bodies, config snippets, or generated assets as material that can be copied into YAC.

## What YAC may inspect or summarize

- Inspect pinned documentation and source paths to understand behavior, product claims, user flows, data flow, and integration boundaries.
- Summarize feature families at a high level, such as agent orchestration, built-in tools, MCP surfaces, workflow loops, hashline edits, and telemetry behavior.
- Record small factual facts needed for traceability, such as the snapshot commit, license family, file paths, command names, and opt-out environment variable names.
- Compare upstream behavior to existing YAC modules without importing upstream implementation details.
- Write ADRs, gap matrices, backlog items, and QA notes that cite pinned URLs.

## What YAC must not copy

Do not copy upstream source code, prompt text, hook bodies, manager logic, config templates, command implementations, tests, docs prose, images, or generated assets into YAC.

Do not translate upstream TypeScript line by line into C++. A port that preserves structure, names, control flow, comments, or prompts from the upstream files is not clean-room work.

Do not paste the SUL-1.0 license into YAC, alter YAC's Apache-2.0 `LICENSE`, or add mixed-license notices without a separate legal decision.

Do not use unpinned upstream references. Every future readiness artifact should cite commit `e0846eb1fd221ba7d41706e71e853cb0480419ca` or a later approved snapshot.

## Clean-room implementation rule

Later implementation must use a clean-room split:

1. A reviewer may inspect the pinned upstream project and write behavior-only requirements for YAC.
2. Those requirements must avoid upstream code shape, file layout, exact prompt prose, private names, and implementation sequencing.
3. A separate implementer should build YAC-native behavior from the requirements, existing YAC architecture, and public API contracts.
4. Review must compare behavior, not source similarity.
5. Any case where exact upstream text, code structure, or assets appear necessary stays `Blocked pending license` until resolved.

## Telemetry and privacy caution

The upstream privacy policy and README describe anonymous telemetry that is enabled by default, sends at most one daily event per machine, uses PostHog, and includes opt-out environment variables. The policy also says prompt contents, source files, repository contents, access tokens, API keys, raw hostnames, and runtime error diagnostics are not collected through that telemetry path.

YAC must not inherit this posture by default. Any future telemetry parity proposal needs an explicit product and privacy decision, a YAC-native data minimization design, clear opt-in or opt-out behavior, user-facing documentation, and tests for disabled telemetry. Until that decision exists, telemetry behavior is `Blocked pending license` and blocked pending privacy review.

## Later implementation gate

Every future ADR, backlog epic, or implementation plan that ports `oh-my-openagent` behavior into YAC must cite this artifact before work starts. The citation must state:

- Which pinned upstream snapshot is being used.
- Which behavior-only requirement document is the source of truth.
- Who wrote the clean-room requirements and who implemented the YAC code.
- Why the work is no longer `Blocked pending license`, or why it remains blocked.

No production feature work should start from upstream files directly. The safe path is behavior inventory, license review, clean-room requirements, YAC-native implementation, then QA.
