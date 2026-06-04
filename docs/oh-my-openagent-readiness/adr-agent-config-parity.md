# Agent, Category, And Configuration Parity ADR

Task checkbox: `- [ ] 4. Agent, category, and configuration parity ADR`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This is a prep-only ADR. It decides how YAC should later represent OMO-style agents, categories, and configuration. It does not add production behavior, settings, migrations, registry structures, prompts, role text, or CLI/TUI changes.

## Context

Task 1 artifact `docs/oh-my-openagent-readiness/license-clean-room.md` sets the governing constraint: production implementation remains `Blocked pending license` until license compatibility and clean-room review clear behavior-only requirements. It also says not to copy upstream source, prompt text, role definitions, config templates, hook bodies, or docs prose.

Task 2 artifact `docs/oh-my-openagent-readiness/upstream-feature-inventory.md` records upstream evidence for `11 agents`, `categories`, `config discovery/migration`, and `model fallback/permissions`. The relevant pinned references are:

- OMO agents: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L5-L33
- OMO categories: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L127-L173
- OMO model fallback and per-agent/category configuration: https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/configuration.md#L232-L288

Task 3 artifact `docs/oh-my-openagent-readiness/yac-gap-matrix.md` identifies current YAC status as `Partial` for the `11 agents` family, `Missing` for categories, `Partial` for config discovery/migration, and `Partial` for model fallback/permissions. YAC has generic sub-agents but not a named multi-agent catalog, category routing layer, ordered model fallback chain, or category-level permission matrix.

Current YAC provider/config boundaries are narrower and provider-first:

- `src/chat/config.cpp` and `src/chat/settings_registry.cpp` load and validate supported settings through a known settings registry, with effective precedence documented as `env > TOML > SQLite > built-in defaults`.
- `src/app/provider_factory.cpp` chooses concrete language model providers from the active `provider.id` value, currently selecting Bedrock, OpenAI, or an OpenAI-compatible path.
- `settings.example.toml` and `docs/configuration.md` expose provider, model, auth, theme, compaction, web search, LSP, and MCP settings; they do not expose agent or category configuration.
- `docs/configuration.md` documents exact provider/model scoped reasoning-effort settings and explicitly says there is no global fallback for that setting.
- `src/chat/agent_mode.cpp` models Plan/Build tool availability, not agent identity, category routing, or per-agent permissions.

## Decision

YAC should later represent OMO-style parity through a YAC-native agent/category policy layer that sits above provider selection and below task dispatch, not by treating upstream names or upstream config shape as public YAC contracts.

The later design should separate three concerns:

- Agent profile: an internal identifier, user-facing display name chosen by YAC, behavior-only role purpose, allowed tool policy, model preference, and optional fallback policy.
- Category profile: a routing bundle for delegated work type, default agent profile, model preference, effort preference, approval/tool constraints, and concurrency expectations.
- Resolution policy: deterministic precedence from explicit request, category default, agent default, active provider config, stored runtime state, then built-in default, while preserving YAC's existing env/TOML/SQLite/default boundary for user settings.

The decision is to proceed later only after the prerequisites below are satisfied. This ADR does not clear production implementation by itself because Task 1 keeps source-derived implementation blocked pending license and clean-room review.

## Options

| Option | Mapping | Advantages | Costs |
| --- | --- | --- | --- |
| A. Provider-only extension | Add future model choices directly to existing provider settings and keep generic sub-agents. | Minimal conceptual change; preserves current provider-first loading. | Does not model categories, per-agent permissions, fallback chains, or named task routing; hides Task 3 gaps. |
| B. YAC-native agent/category policy layer | Add a future layer of agent and category profiles that resolves to provider/model/tool policy before each request. | Keeps config/provider boundaries clean; lets categories and agents be modeled together; avoids assuming upstream names are public YAC names; supports future fallback and permission decisions. | Requires a clean-room requirements pass, schema design, UI/docs decisions, and careful migration planning. |
| C. Full upstream-compatible config mirror | Mirror the upstream agent/category/config schema and names as user-facing YAC config. | May look close to upstream docs at first glance. | Conflicts with YAC architecture, risks copying licensed product structure, and could expose names/prompts before product review. |
| D. Runtime-only hardcoded presets | Keep all profiles internal and unconfigurable. | Avoids user-facing config migration in the first implementation wave. | Makes parity brittle, blocks user overrides, and still requires a policy model that would later need migration. |

Chosen option: Option B, with a clean-room behavior requirements document before any implementation work and with public names decided by YAC product/design rather than inherited automatically from upstream.

## Rejected Options

- Reject Option A as the final parity strategy because provider settings alone cannot represent category routing, per-agent tool policy, ordered fallback, or nested/background constraints identified by Task 3.
- Reject Option C because Task 1 forbids copying upstream config examples, prompts, role definitions, and product structure without license clearance; it also assumes upstream agent names must become public YAC names, which this ADR explicitly avoids.
- Reject Option D as the long-term strategy because hidden runtime presets would create migration debt and make later user-visible category behavior harder to explain.
- Reject any immediate settings migration because readiness work is documentation-only and should not change user files, SQLite state, CLI behavior, or default TOML shape.

## YAC Mapping

Future YAC mapping should use existing ownership boundaries rather than upstream file structure:

| Future concept | YAC boundary | Mapping guidance |
| --- | --- | --- |
| Agent profile | Chat/service policy layer near sub-agent request construction, not provider construction. | Resolve role purpose, model preference, fallback preference, and tool policy before a request is sent. Keep prompt wording clean-room and YAC-authored. |
| Category profile | Task delegation/routing layer above sub-agent launch. | Map a work type to a default agent profile and policy bundle. Category labels can be YAC-native aliases and do not need to match upstream labels. |
| Provider/model selection | Existing provider config and `src/app/provider_factory.cpp`. | Continue selecting concrete providers from `provider.id`; agent/category policy may request a model or fallback candidate, but provider construction remains provider-driven. |
| Fallback policy | New future resolver, after active config and before provider request. | Model fallback as ordered behavior requirements, with explicit failure triggers and audit logging requirements, not as copied upstream chains. |
| Permissions/tool policy | Existing Plan/Build and approval concepts plus future per-agent policy. | Keep Plan/Build safety as a floor. Per-agent/category policy may narrow tools but should not bypass approval-required operations. |
| User configuration | Existing settings registry, TOML docs, and env override conventions. | Any later user-facing fields need registry metadata, validation, docs, examples, and migration decisions as one coherent change. |
| Runtime state | Existing SQLite state scope. | Store only last-used or profile state if a later design requires it; secrets remain in env/TOML/SQLite auth paths according to current rules. |

This mapping keeps OMO evidence at the behavior level: upstream has documented agent roles, categories, and model fallback/config capabilities; YAC should translate those into native policy concepts only after clean-room requirements exist.

## Later Implementation Prerequisites

- License and clean-room gate: cite `docs/oh-my-openagent-readiness/license-clean-room.md` and produce behavior-only requirements that do not contain upstream prompts, role definitions, source structure, or config examples.
- Naming decision: choose YAC public names and display labels independently; upstream names may remain evidence labels in readiness docs but are not automatically product names.
- Schema decision: define whether future agent/category profiles are built-in only, user-configurable TOML, prompt-file metadata, SQLite state, or a combination.
- Precedence decision: specify how explicit user request, category default, agent default, active provider settings, env overrides, TOML, SQLite state, and built-in defaults interact.
- Permission decision: define how per-agent/category tool policy composes with Plan/Build mode, approval-required tools, MCP approvals, and headless auto-approve behavior.
- Fallback decision: define failure triggers, retry limits, provider compatibility checks, user visibility, and logging for any ordered model fallback chain.
- Migration decision: define whether existing `~/.yac/settings.toml`, `settings.example.toml`, and SQLite state need migration; if so, write a separate migration ADR and tests in the later implementation phase.
- QA decision: define future tests for config parsing, policy resolution, permission narrowing, fallback behavior, docs examples, and no-regression provider selection.

## Risks

- License risk: upstream agent roles, category descriptions, prompts, and config examples are high-sensitivity product material under the Task 1 clean-room guardrail.
- Naming risk: adopting upstream names as public YAC names could imply compatibility, preserve licensed product identity, or constrain YAC UX before product review.
- Safety risk: per-agent/category permissions and fallback chains could accidentally bypass Plan/Build restrictions, approvals, or provider auth expectations.
- Config risk: adding user-visible fields without a complete registry/docs/validation plan could create unsupported settings or migration ambiguity.
- Provider risk: fallback behavior can conflict with provider-specific authentication, model capability checks, reasoning effort support, context window detection, and Bedrock/OpenAI-compatible differences.
- Parallel-readiness risk: Tasks 5-8 may make adjacent ADR decisions about tools, runtime, workflow, and safety; Task 9 should reconcile those before any backlog item is marked ready.

## Status

Status: Proceed later
