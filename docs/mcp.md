# MCP Integration Reference

YAC implements the [Model Context Protocol](https://modelcontextprotocol.io/specification/2025-11-25/)
(version 2025-11-25). External MCP servers expose tools that the assistant
calls exactly like built-in tools. This document covers everything you need to
configure, authenticate, and troubleshoot MCP servers in YAC.

## Contents

1. [Configuration schema](#configuration-schema)
2. [Transport types](#transport-types)
3. [OAuth flow](#oauth-flow)
4. [Token storage](#token-storage)
5. [Approval policy](#approval-policy)
6. [Resource lookup](#resource-lookup)
7. [CLI and TUI commands](#cli-and-tui-commands)
8. [Troubleshooting](#troubleshooting)
9. [Linux keychain requirements](#linux-keychain-requirements)
10. [Testing without a real server](#testing-without-a-real-server)

---

## Configuration schema

MCP servers live under `[[mcp.servers]]` in `~/.yac/settings.toml`. Each
entry is an array element, so you can define as many as you need.

### Common fields

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| `id` | string | required | Unique server identifier. Used in CLI commands and tool names. |
| `transport` | `"stdio"` or `"http"` | required | Wire protocol. |
| `enabled` | bool | `true` | Set `false` to skip this server at startup. |
| `auto_start` | bool | `true` | Start the server process automatically when YAC launches. |
| `requires_approval` | bool | `false` | Require human approval before every tool call on this server. |
| `approval_required_tools` | list of strings | `[]` | Require approval only for the named tools (overrides `requires_approval`). |

### Stdio transport fields

| Field | Type | Purpose |
| --- | --- | --- |
| `command` | string | Executable to launch. |
| `args` | list of strings | Arguments passed to the command. |
| `env` | table | Extra environment variables injected into the child process. |

Stdio servers communicate over stdin/stdout using newline-delimited JSON. This
is distinct from the LSP Content-Length framing; do not confuse the two.

```toml
[[mcp.servers]]
id        = "context7"
transport = "stdio"
command   = "npx"
args      = ["-y", "@upstash/context7-mcp"]
enabled   = true
auto_start = true
```

### HTTP transport fields

| Field | Type | Purpose |
| --- | --- | --- |
| `url` | string | Base URL of the MCP HTTP endpoint. |
| `headers` | table | Static headers sent with every request. |

HTTP servers use Streamable HTTP with a `Mcp-Session-Id` header to multiplex
requests over a single connection.

```toml
[[mcp.servers]]
id        = "my-http-server"
transport = "http"
url       = "https://mcp.example.com/api"
enabled   = true
auto_start = false
```

### Bearer token auth

For HTTP servers that accept a static API key:

```toml
[[mcp.servers]]
id        = "my-bearer-server"
transport = "http"
url       = "https://mcp.example.com/api"

[mcp.servers.auth.bearer]
api_key_env = "MY_SERVER_API_KEY"
```

`api_key_env` names the environment variable that holds the secret. YAC reads
it at startup and injects it as a `Bearer` token in the `Authorization` header.

### OAuth2 auth

For HTTP servers that require OAuth2 PKCE:

```toml
[[mcp.servers]]
id        = "example-oauth"
transport = "http"
url       = "https://mcp.example.com/api"
enabled   = true
auto_start = false

[mcp.servers.auth.oauth2]
authorization_url = "https://auth.example.com/authorize"
token_url         = "https://auth.example.com/token"
client_id         = "your-client-id"
scopes            = ["read", "write"]
```

Run `yac mcp auth example-oauth` (or `/mcp auth example-oauth` in the TUI)
to complete the flow before starting the server.

### Global MCP settings

```toml
[mcp]
result_max_bytes = 262144   # 256 KB cap on tool result payloads
```

### Environment variable overrides

Any configured server can be overridden at runtime without editing the TOML
file. The override key format is `YAC_MCP_<ID>_<FIELD>` where `<ID>` is the
server id uppercased with non-alphanumeric characters replaced by `_`.

| Variable | Overrides |
| --- | --- |
| `YAC_MCP_<ID>_COMMAND` | `command` |
| `YAC_MCP_<ID>_ARGS` | `args` (comma-separated) |
| `YAC_MCP_<ID>_URL` | `url` |
| `YAC_MCP_<ID>_ENABLED` | `enabled` |
| `YAC_MCP_<ID>_API_KEY_ENV` | `auth.bearer.api_key_env` |

---

## Transport types

### Stdio

YAC spawns the configured `command` with `args` and communicates over its
stdin/stdout. Messages are newline-delimited JSON objects (one per line). The
child process inherits the YAC environment plus any `env` overrides from the
config.

Stdio servers are the simplest option: no network, no auth, no port conflicts.
They're ideal for local tools installed via npm, pip, or cargo.

### HTTP (Streamable HTTP)

YAC connects to the configured `url` using libcurl. Each request carries an
`Mcp-Session-Id` header that the server uses to correlate streaming responses.
HTTP servers can be remote or local; they require explicit auth configuration
if the endpoint is protected.

---

## OAuth flow

YAC implements OAuth2 PKCE (Proof Key for Code Exchange). The flow:

1. YAC generates a code verifier and challenge.
2. It opens the `authorization_url` in your browser (or prints it if
   `--no-browser` is set).
3. After you approve, the provider redirects to a loopback callback
   (`http://127.0.0.1:<port>/callback`) that YAC listens on.
4. YAC exchanges the authorization code for tokens via `token_url`.
5. Tokens are persisted via the token store (see below).

### Interaction modes

**Default (browser opens automatically):**

```bash
yac mcp auth my-oauth-server
```

**`--no-browser` (interactive paste):** YAC prints the authorization URL and
waits for you to paste the callback URL after approving in your browser:

```bash
yac mcp auth my-oauth-server --no-browser
```

**`--no-browser --callback-url=<url>` (automation):** Skips the interactive
prompt and uses the provided callback URL directly. Useful in CI or scripted
environments where you can drive the browser externally:

```bash
yac mcp auth my-oauth-server --no-browser \
  --callback-url="http://127.0.0.1:9876/callback?code=abc&state=xyz"
```

### Token refresh

YAC automatically refreshes expired access tokens using the stored refresh
token. If the refresh token is also expired, re-run `yac mcp auth <server-id>`
to start a fresh flow.

---

## Token storage

YAC stores OAuth tokens in one of two backends, chosen at runtime:

**Keychain (primary).** On macOS this is the system Keychain. On Linux it
requires `libsecret` and a running DBus session (see
[Linux keychain requirements](#linux-keychain-requirements)). Tokens stored
here are encrypted by the OS and never written to disk in plaintext.

**File store (fallback).** When the keychain is unavailable, YAC writes tokens
to `~/.yac/tokens/<server-id>.json` with permissions `0600`. This file is
plaintext JSON; protect it accordingly.

YAC detects which backend is available at startup and logs the choice. You can
check which backend is active with `yac mcp debug <server-id>` (the `auth`
section of the report shows "keychain" or "file").

---

## Approval policy

By default, YAC allows all MCP tool calls without prompting. You can tighten
this per server or per tool.

**Require approval for all tools on a server:**

```toml
[[mcp.servers]]
id               = "risky-server"
transport        = "stdio"
command          = "my-tool"
requires_approval = true
```

**Require approval only for specific tools:**

```toml
[[mcp.servers]]
id                     = "mixed-server"
transport              = "stdio"
command                = "my-tool"
approval_required_tools = ["delete_file", "run_command"]
```

When approval is required, YAC pauses the response and shows an approval card
in the transcript. Press `y` to approve or `n` to reject. Rejected tool calls
return an error to the assistant, which can then decide how to proceed.

`approval_required_tools` takes precedence over `requires_approval`. If both
are set, only the listed tools require approval.

---

## Resource lookup

MCP servers can expose resources (files, database records, API responses) in
addition to tools. To list available resources for a server:

**CLI:**

```bash
yac mcp resources context7
```

**TUI:**

```
/mcp resources context7
```

The output lists each resource by URI and description. Resources are read-only
references; the assistant can fetch them when constructing context.

---

## CLI and TUI commands

All MCP administration is available from both the CLI and the TUI. The CLI
uses `yac mcp <subcommand>`; the TUI uses `/mcp <subcommand>`.

| Action | CLI | TUI |
| --- | --- | --- |
| Add a server | `yac mcp add <id> [options]` | `/mcp add <id> [options]` |
| List servers | `yac mcp list` | `/mcp list` |
| Authenticate | `yac mcp auth <id>` | `/mcp auth <id>` |
| Log out | `yac mcp logout <id>` | `/mcp logout <id>` |
| Debug report | `yac mcp debug <id>` | `/mcp debug <id>` |
| List resources | `yac mcp resources <id>` | `/mcp resources <id>` |

The `add` subcommand appends an `[[mcp.servers]]` block to `~/.yac/settings.toml`
without touching any existing content or comments.

---

## Troubleshooting

### Debug logs

Each MCP server writes a debug log to:

```
~/.yac/logs/mcp/<server-id>.log
```

The log captures all JSON-RPC messages (with secrets redacted), process
lifecycle events, and connection errors. It's the first place to look when a
server isn't responding.

`yac mcp debug <server-id>` prints the last 50 lines of this log alongside a
connectivity probe result and auth status.

### Common failure modes

**Server not starting (stdio).** Check that `command` is in PATH and
executable. Try running it manually: `npx -y @upstash/context7-mcp`. If it
prints usage or errors, fix those first.

**Connection refused (HTTP).** Confirm the server is running and `url` is
correct. Check for TLS certificate issues if the URL is HTTPS.

**OAuth redirect fails.** The loopback callback requires that port 9876 (or
whichever port YAC picks) is not blocked by a firewall. On Linux, check
`iptables` rules. If the browser can't reach `127.0.0.1`, use
`--no-browser --callback-url=` to supply the redirect manually.

**Token expired / invalid.** Run `yac mcp logout <id>` then
`yac mcp auth <id>` to get fresh tokens.

**Tools not appearing.** Confirm `enabled = true` and `auto_start = true` in
the server config. Restart YAC after editing `settings.toml`. Check the debug
log for initialization errors.

**Result truncated.** If tool output is cut off, raise `result_max_bytes` in
the `[mcp]` section. The default is 256 KB.

---

## Linux keychain requirements

On Linux, the keychain backend uses `libsecret` (via `hrantzsch/keychain`
v1.3.1) and requires:

- `libsecret-1-dev` installed (`apt install libsecret-1-dev` on Debian/Ubuntu)
- A running DBus session bus
- A secrets service (GNOME Keyring, KWallet, or any `org.freedesktop.secrets`
  provider) active in the session

If none of these are available (e.g. in a headless CI container), YAC falls
back to the file store automatically. No configuration is needed; the fallback
is silent.

To verify which backend is active:

```bash
yac mcp debug <server-id>
```

Look for `keychain` or `file` in the `auth` section of the report.

---

## Testing without a real server

For local testing and CI, you can point YAC at a minimal stdio server or a
mock HTTP server. The canonical pattern for an isolated test run:

```bash
HOME=$TMPDIR ./build/yac run "list mcp tools" --auto-approve
```

`HOME=$TMPDIR` gives YAC a clean config directory so it doesn't pick up your
real `~/.yac/settings.toml`. Combine with a test-specific TOML file:

```bash
HOME=$TMPDIR YAC_CONFIG=/path/to/test-settings.toml \
  ./build/yac run "call my_tool" --auto-approve
```

This pattern is used by the integration test suite in `tests/e2e/`.
