# Architectural Review

A top-down audit of the codebase for structural violations — encapsulation
breaks, missed generalizations, and Open/Closed principle problems. Small-style
issues are out of scope; this document targets **high-leverage** refactoring
opportunities.

---

## 1. The Tool System Has No Interface — Everything Is If-Chains

**Severity: Critical · Effort: High**

Adding a tool requires editing **5–6 files**, each with a manual string-matching
branch:

| File | What to add |
|---|---|
| `core_types/tool_call_types.hpp` | New struct + extend `ToolCallBlock` variant |
| `executor_catalog.cpp` → `ToolDefinitions()` | `ToolDefinition{...}` entry |
| `executor_catalog.cpp` → `PrepareToolCall()` | `if (name == kXxxToolName)` branch |
| `executor.cpp` → `Execute()` | `if (name == kXxxToolName)` branch |
| `presentation/tool_call/descriptor.cpp` | `if constexpr` branch |
| `presentation/tool_call/renderer.cpp` | `if constexpr` branch |

There is **no `IToolHandler` interface, no registry map, no self-contained
handler class.** Every tool executor is a free function with an ad-hoc
signature:

```cpp
ExecuteBashTool(request, path, stop_token)          // 3 args
ExecuteEditTool(request, workspace_filesystem)       // 2 args
ExecuteLspDiagnosticsTool(request, lsp, workspace)   // 3 args
ExecuteSubAgentTool(prepared, manager, stop_token)   // 3 args, different first type!
```

**Why it matters.** Every tool addition modifies existing dispatch functions
rather than extending them — a textbook OCP violation. There is no way to add a
tool without touching the central dispatcher.

**Recommendation.** Introduce a `ToolHandler` interface (or C++20 concept):

```cpp
class IToolHandler {
 public:
  [[nodiscard]] virtual ToolDefinition GetDefinition() const = 0;
  [[nodiscard]] virtual PreparedToolCall Prepare(const ToolCallRequest&) const = 0;
  [[nodiscard]] virtual ToolExecutionResult Execute(
      const PreparedToolCall&, std::stop_token) const = 0;
};
```

Each tool becomes a self-contained class, registered into a `ToolRegistry` map.
The dispatcher collapses to a single `registry.Lookup(name)` call.

---

## 2. Subprocess I/O Loop Duplicated ~100 Lines

**Severity: High · Effort: Medium**

`bash_tool_executor.cpp` and `grep_tool_executor.cpp` share an almost
line-for-line identical `fork()/exec()/pipe()/poll()` I/O loop:

| Pattern | `bash_tool_executor.cpp` | `grep_tool_executor.cpp` |
|---|---|---|
| `pipe()` → `fork()` → child `dup2`/`execvp` | ~48 lines | ~32 lines |
| Parent `fcntl(O_NONBLOCK)` + `poll()` loop | ~100 lines | ~100 lines |
| `read()` into 4096-byte buffer with `kMaxOutputBytes` truncation | ✓ | ✓ |
| Kill grace period (`SIGTERM` → wait → `SIGKILL`) | ✓ | ✓ |

**Recommendation.** Extract a `SubprocessRunner` utility:

```cpp
struct SubprocessResult {
  std::string output;
  int exit_code{};
  bool truncated{};
  bool cancelled{};
};
SubprocessResult RunSubprocess(std::string_view command,
                                const std::vector<std::string>& args,
                                const std::filesystem::path& cwd,
                                std::stop_token stop_token = {});
```

---

## 3. `ToolCallBlock` Structs Conflate Request and Response

**Severity: High · Effort: High**

All 17 structs in `tool_call_types.hpp` mix input and output fields:

```cpp
struct BashCall {
  std::string command;   // input (what the model requested)
  std::string output;    // output (what the tool returned)
  int exit_code{};       // output
  bool is_error{};       // output
};
```

10 of 17 structs carry `bool is_error{}` + `std::string error`. This forces
error-handling code in `executor.cpp` to use `std::visit` with `requires`-clauses
just to set error fields. The `PrepareToolCall` fallback reuses `BashCall` as a
generic error carrier because the variant lacks a dedicated error alternative.

**Recommendation.** Either:

- **(A)** Split into `XxxRequest` / `XxxResult` types, or
- **(B)** Compose in an `ErrorInfo` mixin and add a dedicated
  `ToolCallError` alternative to the variant for unknown-tool / parse-failure
  cases instead of hijacking `BashCall`.

---

## 4. `ChatUI` Is a 41-Method God Object

**Severity: High · Effort: Medium**

`ChatUI` is simultaneously:

- The **event sink** (20+ inbound mutation methods)
- The **widget factory** (`Build`, `BuildInput`, `BuildMessageList`,
  `BuildUserMessageComponent`, `BuildAgentMessageComponent`, ...)
- The **scroll controller** (virtualization logic inline in `BuildMessageList`)
- The **render cache manager** (8 scattered `render_cache_.Reset*()` calls
  across different methods)
- Home to a 150-line `DynamicMessageStack` component buried in an anonymous
  namespace inside `chat_ui.cpp`

The `ChatEventSink` interface is a good abstraction boundary, but `ChatUI`
itself carries too many responsibilities.

**Recommendation.** Extract:

- `MessageComponentFactory` — owns `BuildUserMessageComponent`,
  `BuildAgentMessageComponent`, `BuildToolCollapsible`, etc.
- `ScrollManager` — owns virtualization and scroll calculations
- `DynamicMessageStack` — promoted to its own header/source pair

Cache invalidation could be encapsulated behind a `SessionObserver` that
auto-invalidates instead of requiring every mutation site to remember the
correct `Reset*()` call.

---

## 5. `ChatConfig` Conflates Five Distinct Concerns

**Severity: Medium · Effort: Medium**

```cpp
struct ChatConfig {
  // Provider config
  std::string provider_id, model, base_url, api_key, api_key_env;
  double temperature;

  // Tool config
  int max_tool_rounds;
  std::optional<std::string> system_prompt;

  // UI config
  std::string theme_name, theme_density;
  bool sync_terminal_background;

  // LSP config
  std::string lsp_clangd_command;
  std::vector<std::string> lsp_clangd_args;

  // Workspace config
  std::string workspace_root;
};
```

`ProviderConfig` already exists as a subset in `types.hpp`, but `ChatConfig`
duplicates its fields rather than *containing* one. The current shape means the
provider layer, the UI layer, and the LSP layer all transitively depend on each
other's configuration fields.

**Recommendation.** Compose `ChatConfig` from focused sub-configs:

```cpp
struct ChatConfig {
  ProviderConfig provider;
  LspConfig lsp;
  ThemeConfig theme;
  std::string workspace_root;
  int max_tool_rounds = kDefaultToolRoundLimit;
  std::optional<std::string> system_prompt;
};
```

---

## 6. `bootstrap.cpp`: 572-Line God File With Duplicated Wiring

**Severity: Medium · Effort: Medium**

`bootstrap.cpp` mixes:

- Provider/registry construction (duplicated verbatim with `headless.cpp`)
- Theme management and terminal background syncing
- Help text formatting
- A `StreamingCoalescer` class with its own thread (150 lines inline)
- Slash command handlers with inline filesystem walking and sub-agent
  orchestration
- A `ConfigureChatUiCallbacks` function taking 6 parameters with an if/else
  command-dispatch chain

Meanwhile, `headless.cpp` re-implements a subset of `ChatEventBridge` inline
and duplicates provider construction.

**Recommendation.**

- Extract a shared `BuildProvider(config)` factory (used by both bootstrap and
  headless).
- Extract `StreamingCoalescer` to its own header/source pair.
- Replace the `SetOnCommand` if/else chain with the same registry pattern
  already used for slash commands.
- Move slash-command handler business logic out of bootstrap and into the
  `prompt_slash_commands` module.

---

## 7. Layer Violation: Presentation Depends on `chat::AgentMode`

**Severity: Medium · Effort: Low**

`chat_ui.hpp` directly includes `chat/agent_mode.hpp` — a service-layer enum.
The UI stores and switches on `chat::AgentMode::Build/Plan` for rendering. The
bridge then uses a `dynamic_cast` to reach the concrete type:

```cpp
// chat_event_bridge.cpp
if (auto* ui = dynamic_cast<presentation::ChatUI*>(&chat_ui_.get())) {
    ui->SetAgentMode(event.mode);
}
```

This is a double violation: the presentation layer depends on a service-layer
type, and the bridge breaks its own `ChatEventSink` abstraction by downcasting.

**Recommendation.** Introduce a `presentation::UIMode` enum, map at the bridge
layer, and promote `SetAgentMode` to the `ChatEventSink` virtual interface.

---

## 8. `chat/` Layer Inspects `tool_call::SubAgentStatus` for Control Flow

**Severity: Medium · Effort: Low**

In `chat_service_prompt_processor.cpp`, the prompt processor explicitly checks:

```cpp
if (sub_agent_call.status == SubAgentStatus::Running) { ... }
```

This is `tool_call` domain knowledge leaking into the `chat` orchestration
layer. The tool executor should return a richer result type that encodes "still
running in background" without the chat layer needing to understand
`SubAgentStatus` semantics.

**Recommendation.** Add an `is_background_running` flag to
`ToolExecutionResult` (or a richer status enum) so the chat layer doesn't need
to inspect tool-call-variant internals.

---

## 9. `ErrorResult` Helper Duplicated — No Uniform Error in Variant

**Severity: Low · Effort: Low**

`executor.cpp` and `sub_agent_tool_executor.cpp` contain identical
`std::visit`-based functions that set `is_error`/`error` on variant blocks.
This duplication exists because `ToolCallBlock` has no uniform error interface —
some types have error fields, some don't.

**Recommendation.** Either compose an `ErrorInfo` base into all error-carrying
structs, or add a `ToolCallError` alternative to the variant for the
fallback case.

---

## 10. Argument Parsing Done Twice Per Tool Call

**Severity: Low · Effort: Medium**

Arguments are parsed once in `PrepareToolCall()` (executor_catalog.cpp) to
build the preview block, then re-parsed from raw JSON in each `Execute*Tool()`
function. The prepared result isn't passed through to execution. Every tool's
argument schema is therefore defined in two places that must be kept in sync.

**Recommendation.** Have `PrepareToolCall()` produce a typed argument struct
that `Execute()` receives instead of re-parsing the raw JSON. This would also
naturally lead toward per-tool handler classes (item 1).

---

## Summary

| # | Violation | Severity | Effort |
|---|---|---|---|
| 1 | No tool handler interface — if-chains in 6 locations | Critical | High |
| 2 | Subprocess I/O loop duplicated ~100 lines | High | Medium |
| 3 | Request/response conflation in ToolCallBlock structs | High | High |
| 4 | ChatUI 41-method god object | High | Medium |
| 5 | ChatConfig conflates 5 concerns | Medium | Medium |
| 6 | bootstrap.cpp: 572-line god file + duplicated wiring | Medium | Medium |
| 7 | Layer violation: UI → chat::AgentMode + dynamic_cast | Medium | Low |
| 8 | chat/ inspects tool_call::SubAgentStatus | Medium | Low |
| 9 | ErrorResult duplicated (no uniform error in variant) | Low | Low |
| 10 | Argument parsing done twice per tool call | Low | Medium |

Items **1–4** are the highest leverage: introducing a tool handler interface,
a subprocess runner, a request/response split, and ChatUI decomposition would
eliminate entire categories of future bugs and boilerplate.
