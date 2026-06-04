# Safety, Hooks, UX, And Telemetry ADR

Task checkbox: `- [ ] 8. Safety, hooks, UX, and telemetry ADR`

Upstream snapshot: `code-yeongyu/oh-my-openagent` commit `e0846eb1fd221ba7d41706e71e853cb0480419ca`.

This prep-only ADR decides later strategy for safety hooks, hashline edits, comment checking, truncation and compaction preservation, notifications, browser tooling, telemetry, approval semantics, and YAC TUI lifecycle differences. It does not add telemetry, hooks, browser automation, approval behavior, source code, or tests.

## Context

- Task 1 guardrail: [license-clean-room.md](license-clean-room.md) says upstream behavior may be inspected and summarized, but upstream source, prompt text, hook bodies, privacy prose, and implementation structure must not be copied into YAC.
- Task 2 inventory: [upstream-feature-inventory.md](upstream-feature-inventory.md) records upstream `hooks`, `hashline edits`, `context injection/recovery`, `browser/tmux/notifications UX`, and `telemetry` as canonical feature families with pinned evidence.
- Task 3 matrix: [yac-gap-matrix.md](yac-gap-matrix.md) records YAC gaps for `hooks/continuation injection`, `hashline edit semantics`, `web/docs/search parity`, `Team Mode visualization`, and `telemetry policy`.
- Upstream evidence is pinned to: [README hashline notes](https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/README.md#L314-L330), [hook composition source location](https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/src/create-hooks.ts#L35-L98), [hook/UX/tooling reference](https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/features.md#L753-L905), and [notifications/browser/tmux configuration](https://github.com/code-yeongyu/oh-my-openagent/blob/e0846eb1fd221ba7d41706e71e853cb0480419ca/docs/reference/configuration.md#L554-L619).
- Local YAC evidence reviewed: `src/presentation/chat_ui.cpp`, `src/presentation/chat_ui_overlay_state.cpp`, `src/presentation/chat_ui_components.cpp`, `src/chat/chat_service_request_builder.cpp`, `src/tool_call/executor_catalog.cpp`, `tests/test_chat_service_approval.cpp`, `tests/test_renderer.cpp`, `tests/test_web_fetch.cpp`, and `tests/test_web_search.cpp`.

## Hooks

Decision: `Spike first`.

OpenCode plugin hooks run inside a plugin lifecycle that can compose startup managers, continuation behavior, prompt/context injection, recovery, notifications, and tool interception around OpenCode events. YAC has a C++ service and TUI lifecycle instead: `ChatServiceRequestBuilder` builds a request from workspace instructions and settings, `ChatService` and the tool runner own execution and approvals, and `ChatUI` renders transient state through FTXUI overlays and notices. There is no public hook extension point.

Later YAC hook work should start as a narrow lifecycle map, not as a generic plugin system. Candidate phases must be named in YAC terms: request construction, before tool preparation, approval requested, tool result normalized, compaction boundary, sub-agent completion, notification emission, and session shutdown. Each phase needs an owner module, synchronous/asynchronous behavior, cancellation semantics, data exposure rules, and tests before implementation.

Privacy and safety impact: hooks can see prompts, tool arguments, file paths, MCP server IDs, tool results, and possibly secrets in command output. Any hook API must default to no third-party network egress, preserve current approval prompts, and avoid letting hooks rewrite approvals, tool allowlists, or system prompts without explicit user configuration.

Rejected options:

- Do not copy upstream hook code or lifecycle names directly; the upstream TypeScript plugin model does not match YAC ownership or threading.
- Do not add a catch-all callback surface; it would make prompt, approval, and privacy behavior harder to audit.

## Hashline edits

Decision: `Spike first`.

YAC currently exposes exact-string `file_edit` and full-file `file_write` through `src/tool_call/executor_catalog.cpp`, with approvals for mutating tools. Task 3 records no hash-addressed line anchors and no comment-checker guardrail. Upstream hashline behavior is a separate safety family: it uses line identity to detect stale edits and reject edits against changed content.

Later hashline work should be a YAC-native edit safety spike that answers:

- Whether hashline identifiers are displayed only in file reads, accepted in edit arguments, or both.
- Whether stale-line detection augments or replaces exact-string matching.
- How generated comments or comment-checker warnings are represented without copying upstream prompt or checker logic.
- How approvals show the target file, changed lines, stale-anchor failures, and privacy-sensitive file snippets.

Privacy and safety impact: hashline data derives from source files and can expose file structure in prompts and approvals. It must not increase source disclosure beyond the current file read/edit surfaces without a deliberate approval and redaction strategy.

## Approval semantics

Decision: `Proceed later` for documenting and tightening YAC-native approval semantics only; `Spike first` for any behavior change.

YAC already has concrete approval points: `file_write`, `file_edit`, `bash`, `lsp_rename`, `ask_user`, and `plan_exit` require approval in `src/tool_call/executor_catalog.cpp`; `ChatUiOverlayState` renders a permission dialog; MCP previews include server and per-tool trust policy lines; `tests/test_chat_service_approval.cpp` covers rejection, sequencing, and Plan/Build approval outcomes. Read-only grep/glob/LSP reads, `web_fetch`, and configured Exa `web_search` currently do not require approval.

Later parity must not silently import upstream approval behavior. Approval changes are safety-sensitive because they can allow file mutation, shell execution, MCP calls, browser/network access, prompt rewriting, or user-question spoofing. Future work should first produce a table of tool classes, data classes, default policy, TUI rendering, headless behavior, auto-approve interaction, and test coverage.

YAC TUI lifecycle difference: approval is an event delivered to the C++ TUI overlay and resolved back into `ChatService`. Headless can auto-approve or fail/reject when approval is required. This differs from plugin hook interception, so future approval hooks must not bypass the existing approval manager or modal resolution path.

Privacy and safety impact: approval dialogs can display tool arguments, MCP arguments, file paths, and snippets. The policy must define truncation, redaction, and when network or browser-capable actions require user consent.

## Notifications/UI

Decision: `Spike first`.

YAC has terminal-native UI surfaces: status rail, command palettes, help, permission dialogs, ask-user dialogs, notices, sub-agent tool-call rendering, collapsed tool cards, thinking animation, and local browser-launch notices for OAuth fallback flows. It does not have desktop notifications, toasts outside the TUI, tmux pane management, or Team Mode pane visualization.

Later notification work should choose whether YAC needs only in-TUI notices, OS notifications, terminal bell behavior, tmux integration, or all of them. The first implementation plan should preserve TUI/headless differences: TUI can render transient notices; headless must emit deterministic stdout/stderr or structured events instead of hidden toasts.

Truncation and compaction preservation belong in this UI decision because YAC already summarizes tool/result rendering and request compaction differently from upstream hooks. Future work must specify which content survives compaction, what is visibly marked as truncated, and whether notification payloads ever include prompt text, file contents, tool arguments, or tool results.

Privacy and safety impact: notifications can leak filenames, prompts, model output, or command details to the desktop or tmux status. Default future behavior should be in-terminal only until an explicit notification privacy policy exists.

## Telemetry

Decision: `Blocked pending explicit product decision`.

Telemetry is not automatically desirable and must not be accepted as a parity default. Task 1 already records that upstream telemetry and privacy posture cannot be inherited silently. YAC has no reviewed telemetry settings, collection pipeline, or product policy in `docs/configuration.md` or `settings.example.toml` per Task 3.

Any future telemetry proposal must be a separate product/privacy ADR before implementation. It must decide whether YAC collects anything at all, whether collection is opt-in or opt-out, what data is forbidden, how installation identifiers are generated, where data is sent, how users disable it, how tests prove disabled behavior, and how source/license/privacy review clears the design.

Privacy and safety impact: telemetry can expose usage patterns, model/provider choices, tool names, errors, environment metadata, or accidental prompt/file details. Until the explicit product decision exists, do not port telemetry, do not add telemetry dependencies, and do not add placeholder telemetry hooks.

## Browser/web tooling

Decision: `Spike first`.

YAC currently has `web_fetch` and configured Exa `web_search` tool paths plus tests for URL validation, private-network blocking, timeouts, body limits, HTML-to-markdown/text transforms, provider error normalization, and secret redaction. These are not browser automation. YAC also launches a browser for OAuth flows, but that is an authentication helper, not an agent-controlled browser tool.

Browser automation, Playwright-style control, docs search providers, Context7, Grep.app, and tmux/browser UX must be spiked before any parity claim. The spike must decide whether browser tooling belongs as a built-in tool, MCP server, external user-configured MCP, or not at all. It must also decide approval requirements for network navigation, screenshots, page content extraction, downloads/uploads, credential boundaries, local/private network blocking, and headless output.

Privacy and safety impact: browser automation can access logged-in sessions, cookies, local services, page contents, downloads, screenshots, and user credentials. Future YAC behavior must default to least privilege, visible approval for sensitive navigation/actions, and no silent persistence of browser state.

## Status

Status: Spike first

This ADR makes a prep-only decision: safety hooks, hashline edits, notifications/UI, browser/web tooling, and any approval behavior changes require spikes before implementation; YAC-native approval documentation may proceed later; telemetry is `Blocked pending explicit product decision`. No production source, tests, telemetry, browser automation, hooks, or approval behavior are modified by this ADR.
