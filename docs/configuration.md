# Configuration

YAC loads configuration from `~/.yac/settings.toml`. On first launch it creates
that file from the built-in template. The checked-in
[settings.example.toml](../settings.example.toml) mirrors the supported shape
and is the best starting point for local edits.

Shell variables named `YAC_*` override TOML at startup. Provider presets fill
only missing fields: explicit TOML wins over preset defaults, and env overrides
win over both.

Runtime state lives in `~/.yac/state.sqlite`. The full precedence is
`env > TOML > SQLite > built-in defaults`. SQLite fills provider profiles,
provider credentials and OpenAI auth, and last-used runtime state only when an
env var or explicit TOML value has not already set that field. TOML and env
remain the right place for explicit configuration overrides.

## Files

- `~/.yac/settings.toml`: local settings.
- `~/.yac/state.sqlite`: provider profiles, credentials and OpenAI auth, and
  last-used runtime state.
- `settings.example.toml`: checked-in reference copy.
- `~/.yac/prompts/*.toml`: predefined prompt commands. The file stem becomes
  the slash command name, and `$ARGUMENTS` is replaced at invocation.

Built-in slash commands keep priority if a prompt file name collides. Missing
`init.toml` and `review.toml` prompt files are seeded automatically.

YAC uses `HOME` to resolve user config and auth paths. Executable lookup for
helper tools and subprocesses uses `PATH`.

## Core Settings

```toml
temperature = 0.7
# system_prompt = "You are a helpful assistant."
# workspace_root = "/path/to/workspace"

[provider]
id          = "openai"
model       = "gpt-4o-mini"
base_url    = "https://api.openai.com/v1/"
api_key_env = "OPENAI_API_KEY"

[lsp.clangd]
command = "clangd"
args    = []
```

| Setting | Env override | Default | Purpose |
| --- | --- | --- | --- |
| `provider.id` | `YAC_PROVIDER` | `openai-compatible` | Provider ID registered by the app. |
| `provider.model` | `YAC_MODEL` | `gpt-4o-mini` | Model sent to the provider. |
| `provider.base_url` | `YAC_BASE_URL` | `https://api.openai.com/v1/` | OpenAI-compatible API base URL. |
| `provider.api_key_env` | `YAC_API_KEY_ENV` | `OPENAI_API_KEY` | Env var holding the API key. |
| `provider.api_key` | unset | unset | Optional inline key; prefer env or stored auth. |
| `provider.context_window` | `YAC_CONTEXT_WINDOW` | `0` | Manual context window override; `0` means auto-detect. |
| `temperature` | `YAC_TEMPERATURE` | `0.7` | Sampling temperature from `0.0` to `2.0`. |
| `system_prompt` | `YAC_SYSTEM_PROMPT` | unset | Optional system prompt prepended to requests. |
| `workspace_root` | `YAC_WORKSPACE_ROOT` | launch CWD | Workspace root for scoped tools. |
| `lsp.clangd.command` | `YAC_LSP_CLANGD_COMMAND` | `clangd` | LSP server command. |
| `lsp.clangd.args` | `YAC_LSP_CLANGD_ARGS` | `[]` | LSP server arguments. |

## Model Settings

Use `[[provider.model_settings]]` entries for exact provider and model scoped
settings. There is no global fallback, and entries don't apply to other provider
IDs or model names.

```toml
[[provider.model_settings]]
provider = "openai"
model = "gpt-5.5"
effort = "high"
```

`provider` must match the active `provider.id`, and `model` must match the
active `provider.model`. `effort` is optional. Valid configured values are
`none`, `minimal`, `low`, `medium`, `high`, and `xhigh`; the active model's
capability lookup can still reject values outside that model's allowlist.

`/effort <value>` writes the scoped override for the active provider and model.
`/effort unset` clears that scoped override, so future requests for that exact
provider and model omit effort fields and use the provider default.
Persisted effort applies automatically to both TUI requests and headless
`yac run` requests; changing effort is currently TUI-only through `/effort`.

Effort support is implemented only for OpenAI request paths that pass capability
lookup. The `openai-compatible` provider only qualifies when its base URL trims
to `https://api.openai.com/v1`. It serializes as `reasoning.effort` on OpenAI
Responses and as top-level `reasoning_effort` on OpenAI Chat Completions. It is
not implemented for non-OpenAI providers, Z.ai, or AWS Bedrock.
Bedrock Claude thinking controls are not implemented by /effort.

## Providers

Set `[provider].id = "openai"` or `YAC_PROVIDER=openai` to use the OpenAI
preset. When only the ID is set, the preset fills in `gpt-4o-mini`,
`https://api.openai.com/v1/`, and `OPENAI_API_KEY`. API-key mode uses
OpenAI-compatible chat completions. Browser and device OAuth use stored provider
auth; see [OpenAI auth](openai-auth.md).

Use `[provider].id = "openai-compatible"` for other services that speak the
OpenAI chat completions API. Configure `provider.base_url`,
`provider.api_key_env`, and `provider.model` for that endpoint.

Set `[provider].id = "zai"` or `YAC_PROVIDER=zai` to use the Z.ai Coding API
preset. When only the ID is set, the preset fills in `glm-5.1`,
`https://api.z.ai/api/coding/paas/v4`, and `ZAI_API_KEY`.

Set `[provider].id = "bedrock"` to use AWS Bedrock via the Converse API.
Credentials come from the AWS SDK default chain.

| Setting | Env override | Default | Purpose |
| --- | --- | --- | --- |
| `provider.id` | `YAC_PROVIDER` | unset | Set to `"bedrock"`. |
| `provider.model` | `YAC_MODEL` | `anthropic.claude-3-5-haiku-20241022-v1:0` | Bedrock model ID. |
| `provider.options.region` | `YAC_BEDROCK_REGION`, `AWS_REGION` | `us-east-1` | AWS region. |
| `provider.options.max_tokens` | `YAC_BEDROCK_MAX_TOKENS` | `4096` | Max output tokens. |
| `provider.options.profile` | `YAC_BEDROCK_PROFILE` | unset | AWS profile name. |
| `provider.options.endpoint_override` | `YAC_BEDROCK_ENDPOINT_OVERRIDE` | unset | VPC endpoint URL. |
| `provider.options.credential_refresh_command` | `YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND` | unset | Command to run once on auth failure before retrying. |

Known-good Bedrock model IDs include
`anthropic.claude-3-5-sonnet-20241022-v2:0`,
`anthropic.claude-3-5-haiku-20241022-v1:0`, `amazon.nova-pro-v1:0`,
`amazon.nova-lite-v1:0`, `amazon.nova-micro-v1:0`,
`meta.llama3-1-70b-instruct-v1:0`, and `mistral.mistral-large-2407-v1:0`.
Inference profile prefixes such as `us.`, `eu.`, `apac.`, and `global.` are
supported.

## Auth And Secrets

Prefer `OPENAI_API_KEY`, `ZAI_API_KEY`, or stored OpenAI auth over inline
`provider.api_key`. For the built-in `openai` provider, auth precedence follows
`env > TOML > SQLite > built-in defaults`: env API keys win first, explicit TOML
`provider.api_key` overrides SQLite stored auth, and SQLite fills only when env
and TOML do not.

Plaintext `provider.api_key` values in TOML remain supported as explicit
overrides, but YAC does not migrate them into SQLite. Stock SQLite storage is
not encrypted and does not use SQLCipher or the SQLite Encryption Extension.
Protection comes from private `~/.yac` and `state.sqlite` file permissions.

Store an OpenAI API key without putting it in TOML:

```bash
printf 'sk-...' | yac auth openai set-api-key --stdin
```

OpenAI auth commands:

```bash
yac auth openai login
yac auth openai login --device
yac auth openai status
yac auth openai logout
```

The OpenAI auth CLI stores new credentials in the SQLite state store. Legacy
keychain or file auth may be imported best-effort into SQLite and is left
untouched after import. `YAC_OPENAI_AUTH_STORE=file` applies only to legacy file
auth access during import paths. `YAC_KEYCHAIN_DISABLED=1` keeps auth tests
hermetic; no keychain is required for normal SQLite-backed auth.

## Theme And Compaction

| Setting | Env override | Default | Purpose |
| --- | --- | --- | --- |
| `theme.name` | `YAC_THEME_NAME` | `"vivid"` | Active theme preset: `"vivid"` or `"system"`. |
| `theme.density` | `YAC_THEME_DENSITY` | `"comfortable"` | `"comfortable"` or `"compact"`. |
| `theme.sync_terminal_background` | `YAC_SYNC_TERMINAL_BACKGROUND` | `true` | Paint and restore the terminal background. |
| `compact.auto_enabled` | `YAC_COMPACT_AUTO_ENABLED` | `true` | Auto-compact when projected usage crosses the threshold. |
| `compact.threshold` | `YAC_COMPACT_THRESHOLD` | `0.8` | Fraction of context window that triggers compaction. |
| `compact.keep_last` | `YAC_COMPACT_KEEP_LAST` | `20` | Recent non-system messages preserved through compaction. |
| `compact.mode` | `YAC_COMPACT_MODE` | `"summarize"` | `"summarize"` or `"truncate"`. |

## MCP Settings

Global MCP payload size is controlled by `mcp.result_max_bytes` and
`YAC_MCP_RESULT_MAX_BYTES`. Servers are configured with `[[mcp.servers]]`
blocks and can be managed with `yac mcp <subcmd>` or `/mcp <subcmd>`.

See [MCP](mcp.md) for the full schema, OAuth flow, token storage, approval
policy, resource commands, and troubleshooting.
