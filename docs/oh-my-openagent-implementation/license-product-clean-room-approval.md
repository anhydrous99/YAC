# License/Product Clean-Room Approval Gate

Task checkbox: `- [ ] 1. License/product clean-room implementation gate`

This YAC-authored gate records the implementation status for `oh-my-openagent` parity work. It cites the local readiness guardrail at `docs/oh-my-openagent-readiness/license-clean-room.md` and uses upstream snapshot commit `e0846eb1fd221ba7d41706e71e853cb0480419ca` for traceability only.

## approval status

Status: approval not granted.

No external license approval, product approval, product/privacy approval, or license compatibility decision has been provided in this work session. This artifact doesn't claim legal approval, product approval, or compatibility between upstream materials and YAC.

## blocker status

Status: Blocked pending license and Blocked pending explicit product decision.

Production implementation tasks that derive behavior from upstream remain blocked. This applies to Tasks 3 through 15 in `.omo/plans/oh-my-openagent-implementation.md` while approval is absent. Task 1 should not be marked complete by this artifact writer; Atlas may mark it after verification.

## source snapshot

Source snapshot: `code-yeongyu/oh-my-openagent` at commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

The readiness source notes upstream uses SUL-1.0. This gate records that fact as project context only and doesn't restate or interpret upstream license terms.

## behavior-only source

The allowed source of truth for future planning is YAC-authored behavior-only requirements. For this gate, the local inputs are:

- `docs/oh-my-openagent-readiness/license-clean-room.md`
- `docs/oh-my-openagent-readiness/implementation-backlog.md`
- `docs/oh-my-openagent-readiness/adr-safety-hooks-ux.md`
- `.omo/plans/oh-my-openagent-implementation.md`

Task 2 may proceed after this gate artifact exists because Task 2 is behavior-only matrix work. It must continue to avoid upstream code shape, prompt prose, schemas, tests, docs prose, config examples, and assets.

## clean-room split

The clean-room split remains required before any production work:

1. A reviewer may inspect pinned upstream materials and write behavior-only requirements.
2. The requirements must avoid upstream code structure, exact prompts, file layout, private names, and implementation sequencing.
3. A separate YAC implementer must build YAC-native behavior from those requirements, existing YAC architecture, and public API contracts.
4. Review compares behavior and safety outcomes, not source similarity.

No clean-room implementer approval is recorded here.

## allowed materials

Allowed materials for this blocked planning stage:

- Pinned upstream snapshot identifiers and factual path references.
- Local readiness summaries that describe behavior at a high level.
- YAC-authored ADRs, backlog notes, gap matrices, and QA plans.
- Existing YAC architecture, source ownership boundaries, tests, and docs used to plan safe future work.

## prohibited materials

Prohibited materials unless separate approval clears them:

- Upstream source code, prompts, hook bodies, schemas, config examples, command templates, tests, docs prose, package metadata, images, or generated assets.
- Line-by-line translation of upstream TypeScript into C++.
- Unpinned upstream `/blob/main` or `/tree/main` references.
- Mixed-license notices or changes to YAC's Apache-2.0 license posture.

## telemetry excluded

Telemetry is excluded from implementation. YAC has no approved telemetry settings, collection pipeline, event plumbing, dependencies, stubs, or user-facing telemetry docs for this parity work.

Because no product/privacy decision exists in this work session, telemetry remains Blocked pending explicit product decision. Future telemetry work, if any, needs a separate YAC-native data minimization design, user-facing behavior, disabled-state tests, and product/privacy approval.

## downstream task impact

Task 2 may proceed as behavior-only matrix work after this gate artifact exists.

Tasks 3 through 15 are marked `[~]` in `.omo/plans/oh-my-openagent-implementation.md` because approval status is blocked/not granted. They must not modify production source, tests, `settings.example.toml`, `README.md`, `docs/configuration.md`, or other user-facing product docs for implementation purposes until separate approval is recorded.

## not legal advice

This artifact is an engineering gate and blocker record for YAC planning. It is not legal advice, doesn't interpret license compatibility, and doesn't replace legal, product, or privacy review.
