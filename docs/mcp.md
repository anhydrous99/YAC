# MCP Reference

YAC supports the [Model Context Protocol](https://modelcontextprotocol.io/specification/2025-11-25/)
(version **2025-11-25**). External tool servers connect over stdio or HTTP and
expose their tools directly to the assistant, which calls them the same way it
calls built-in tools.

Quick admin commands:

```bash
yac mcp list                    # list configured servers
yac mcp add <id> ...            # register a server
yac mcp remove <id>             # remove a server
yac mcp auth <id>               # run OAuth PKCE flow for a server
```

From inside the TUI, prefix the same subcommands with `/mcp` (e.g. `/mcp list`,
`/mcp auth <id>`).

---

## Configuration Schema

MCP servers are declared as `[[mcp.servers]]` array-of-tables in
`~/.yac/settings.toml`. Each block maps to one server.

### Fields

| Field | Type | Default | Required | Description |
|---|---|---|---|---|
| `id` | string | | yes | Unique server identifier. Used in tool names and log paths. |
| `transport` | string | | yes | `"stdio"` or `"http"`. |
| `command` | string | | stdio only | Executable to launch. |
| `args` | array of strings | `[]` | no | Arguments passed to `command`. |
| `env` | table | `{}` | no | Extra environment variables injected into the child process. |
| `url` | string | | http only | Base URL of the HTTP MCP endpoint. |
| `enabled` | bool | `true` | no | Set to `false` to skip this server at startup. |
| `auto_start` | bool | `true` | no | Start the server automatically when YAC launches. |
| `requires_approval` | bool | `false` | no | Require user approval before every tool call on this server. |
| `approval_required_tools` | array of strings | `[]` | no | Per-tool approval list (raw tool names as reported by the server). |
| `[mcp.servers.auth.api_key_env]` | string | | no | Name of an env var holding a Bearer token. Sent as `Authorization: Bearer <value>`. |
| `[mcp.servers.auth.oauth2]` | table | | no | OAuth 2.0 PKCE config. See sub-fields below. |

#### `[mcp.servers.auth.oauth2]` sub-fields

| Field | Type | Required | Description |
|---|---|---|---|
| `authorization_url` | string | yes | Authorization endpoint URL. Must be `https://` or loopback `http://`. |
| `token_url` | string | yes | Token endpoint URL. Same URL restrictions apply. |
| `client_id` | string | yes | OAuth client ID. |
| `scopes` | array of strings | no | Requested scopes. Joined with spaces in the authorization request. |

`auth.api_key_env` and `auth.oauth2` are mutually exclusive. Omit both for
unauthenticated servers.

### Global MCP settings

```toml
[mcp]
# Maximum bytes for a single tool result payload (default: 262144 = 256 KB).
# result_max_bytes = 262144
```

### Environment variable overrides

Server fields can be overridden per-server without editing the TOML file. The
env var name is `YAC_MCP_<UPPER_ID>_<FIELD>`, where `<UPPER_ID>` is the server
`id` upper-cased with non-alphanumeric characters replaced by `_`.

| Field | Env var pattern |
|---|---|
| `command` | `YAC_MCP_<ID>_COMMAND` |
| `args` | `YAC_MCP_<ID>_ARGS` (comma-separated) |
| `url` | `YAC_MCP_<ID>_URL` |
| `enabled` | `YAC_MCP_<ID>_ENABLED` |
| `auth.api_key_env` | `YAC_MCP_<ID>_API_KEY_ENV` |

Example: for a server with `id = "my-server"`, set `YAC_MCP_MY_SERVER_COMMAND`
to override its command.

### Examples

Stdio server (Context7 via npx):

```toml
[[mcp.servers]]
id        = "context7"
transport = "stdio"
command   = "npx"
args      = ["-y", "@upstash/context7-mcp"]
enabled   = true
auto_start = true
```

HTTP server with OAuth 2.0:

```toml
[[mcp.servers]]
id               = "example-oauth"
transport        = "http"
url              = "https://mcp.example.com/api"
enabled          = true
auto_start       = false
requires_approval = false

[mcp.servers.auth.oauth2]
authorization_url = "https://auth.example.com/authorize"
token_url         = "https://auth.example.com/token"
client_id         = "your-client-id"
scopes            = ["read", "write"]
```

HTTP server with a static Bearer token:

```toml
[[mcp.servers]]
id        = "my-api"
transport = "http"
url       = "https://mcp.example.com/api"

[mcp.servers.auth]
api_key_env = "MY_API_TOKEN"
```

---

## OAuth Flow

YAC implements OAuth 2.0 Authorization Code flow with PKCE (RFC 7636). Run it
with:

```bash
yac mcp auth <server-id>
```

Or from the TUI: `/mcp auth <server-id>`.

### PKCE walkthrough

```mermaid
sequenceDiagram
    participant User
    participant YAC
    participant Browser
    participant AuthServer as Authorization Server
    participant TokenEndpoint as Token Endpoint

    User->>YAC: yac mcp auth <id>
    YAC->>YAC: Generate code_verifier (random 32 bytes, base64url)
    YAC->>YAC: Derive code_challenge = SHA-256(verifier), base64url
    YAC->>YAC: Bind loopback HTTP server on 127.0.0.1:<ephemeral-port>
    YAC->>Browser: Open authorization_url?response_type=code&client_id=...&code_challenge=...&code_challenge_method=S256&state=<random>&redirect_uri=http://127.0.0.1:<port>/callback
    Browser->>AuthServer: GET authorization_url (user logs in)
    AuthServer->>Browser: Redirect to http://127.0.0.1:<port>/callback?code=<code>&state=<state>
    Browser->>YAC: GET /callback?code=<code>&state=<state>
    YAC->>YAC: Validate state matches; respond 200 "Authorization successful"
    YAC->>TokenEndpoint: POST token_url {grant_type=authorization_code, code, code_verifier, client_id, redirect_uri}
    TokenEndpoint->>YAC: {access_token, refresh_token, expires_in, ...}
    YAC->>YAC: Persist tokens to keychain (or file fallback)
    YAC->>User: Auth complete
```

### Loopback server details

The callback server binds to `127.0.0.1` on an OS-assigned ephemeral port
(port 0). The redirect URI sent to the authorization server is
`http://127.0.0.1:<port>/callback`. The server polls for a connection every
100 ms and stops when the stop token is signalled (e.g. on cancellation).

Only `https://` and loopback `http://` URLs are accepted for `authorization_url`
and `token_url`. Any other scheme throws at runtime.

### Token refresh

When an access token expires, YAC calls the token endpoint with
`grant_type=refresh_token`. Concurrent refresh attempts are serialized: the
first caller performs the request; subsequent callers wait and receive the same
result.

### Headless / no-browser mode

If the browser cannot be launched, YAC prints the authorization URL and prompts
you to paste the callback URL manually:

```
Open the following URL in your browser to authorize:
https://auth.example.com/authorize?...

Paste the callback URL here:
```

---

## Token Storage

Tokens are stored as JSON. YAC tries the OS keychain first and falls back to a
file store if the keychain is unavailable.

### Keychain (primary)

| Platform | Backend |
|---|---|
| macOS | macOS Keychain (Security framework) |
| Windows | Windows Credential Manager |
| Linux | libsecret + DBus (requires `libsecret-1-dev` and a running DBus session) |

`KeychainTokenStore::IsKeychainAvailable()` is checked once at startup and
cached. If the keychain backend fails (e.g. no DBus session on Linux), YAC
falls back to the file store automatically.

### File fallback

Path: `~/.yac/mcp/auth/<server-id>.json`

The directory and file are created with permissions `0700` / `0600`
(owner-only). YAC refuses to read a token file whose permissions are wider than
`0600` and throws an error with a `chmod 0600 <path>` hint.

Writes are atomic: YAC writes to a `.tmp` sibling, calls `fsync`, then renames
into place.

### Secret redaction in debug logs

All frames written to `~/.yac/logs/mcp/<server-id>.log` pass through
`RedactSecrets()` before being written. The following patterns are replaced with
`[REDACTED]`:

- `Authorization: Bearer <token>` headers
- JSON fields: `access_token`, `refresh_token`, `id_token`, `client_secret`,
  `Mcp-Session-Id`
- OAuth authorization codes in query strings (`?code=...`)
- Bare `Bearer <token>` occurrences

---

## Approval Policy

YAC can require explicit user confirmation before executing MCP tool calls.
Approval is configured at two granularities.

### Per-server approval

Set `requires_approval = true` on a server to require approval for every tool
call that server exposes:

```toml
[[mcp.servers]]
id                = "dangerous-server"
transport         = "stdio"
command           = "my-tool-server"
requires_approval = true
```

### Per-tool approval

Use `approval_required_tools` to list specific tool names (as reported by the
server, before sanitization) that need approval. Other tools on the same server
run without prompting:

```toml
[[mcp.servers]]
id                      = "mixed-server"
transport               = "stdio"
command                 = "my-tool-server"
approval_required_tools = ["delete_file", "run_command"]
```

`requires_approval = true` takes precedence: if it is set, all tools require
approval regardless of `approval_required_tools`.

### Approval flow

When the assistant calls an MCP tool that requires approval, YAC:

1. Pauses execution and emits an approval-request event.
2. The UI displays the tool name, server ID, and arguments.
3. The user approves or rejects.
4. On approval, YAC forwards the call to the MCP server and returns the result.
   On rejection, the tool call returns an error to the assistant.

The `--auto-approve` flag (headless `yac run` only) bypasses all approval
prompts.

---

## Resource Lookup

MCP servers can expose resources in addition to tools. Resources are identified
by URI and carry optional metadata (name, description, MIME type).

### Listing resources

```bash
# CLI
yac mcp resources <server-id>

# TUI slash command
/mcp resources <server-id>
```

This calls `resources/list` on the server and prints each resource's URI, name,
and description.

### Reading a resource

```bash
yac mcp read <server-id> <uri>
```

This calls `resources/read` on the server and prints the resource content.
Text resources are printed as-is; binary (blob) resources are noted but not
decoded to the terminal.

### Using resources in prompts

Resources are not automatically injected into the assistant context. To include
a resource, read it with `yac mcp read` and paste the content into your prompt,
or instruct the assistant to call the appropriate MCP tool that returns the
resource content.

---

## Troubleshooting

### Server fails to start

**Symptom:** Server shows as disconnected; no tools appear from that server.

**Cause:** The `command` is not found in PATH, exits immediately, or the stdio
handshake times out.

**Fix:**
1. Run the command manually to confirm it works: `npx -y @upstash/context7-mcp`.
2. Check the debug log: `cat ~/.yac/logs/mcp/<server-id>.log`.
3. Verify `enabled = true` and `auto_start = true` in the config.
4. If the command needs a specific PATH, set it via the `env` table:
   ```toml
   [mcp.servers.env]
   PATH = "/usr/local/bin:/usr/bin:/bin"
   ```

### OAuth callback never arrives

**Symptom:** Browser opens but YAC hangs waiting for the callback.

**Cause:** The authorization server redirected to a different host or port, or
the loopback server was blocked by a firewall rule.

**Fix:**
1. Confirm the authorization server supports loopback redirect URIs
   (`http://127.0.0.1`). Some servers require pre-registered redirect URIs.
2. Check that no local firewall blocks connections to `127.0.0.1` on ephemeral
   ports.
3. If the browser cannot open, YAC falls back to printing the URL and asking
   you to paste the callback URL manually.

### Tool name collision or truncation

**Symptom:** Two tools from different servers appear as the same name, or a
tool name looks like `mcp_myserver__some_very_long_tool_name_h1a2b`.

**Cause:** Tool names are sanitized to fit within a 60-character budget
(`mcp_` prefix + up to 16 chars for server ID + `__` separator + up to 40
chars for tool name). Names longer than 40 characters are truncated to 35
characters and suffixed with `_h<4-hex-digit FNV-1a hash>` of the original
name.

**Fix:** This is expected behavior for servers with long tool names. The hash
suffix ensures uniqueness. No action needed unless you need to reference the
tool by name in `approval_required_tools`, in which case use the raw name as
reported by the server (before sanitization).

### libsecret unavailable on Linux

**Symptom:** Tokens are not persisted across restarts; a warning about keychain
unavailability appears.

**Cause:** `libsecret` is not installed or no DBus session is running.

**Fix:**
1. Install `libsecret-1-dev` and ensure a DBus session is active (standard in
   desktop environments; may be absent in SSH sessions or containers).
2. If a DBus session is not practical, YAC automatically falls back to the file
   store at `~/.yac/mcp/auth/<server-id>.json`. Tokens persist across restarts
   via the file store; just ensure the directory is on a persistent volume.

### Token file permissions error

**Symptom:** `FileTokenStore: refusing to read ... — file permissions are too open (expected 0600)`.

**Cause:** The token file has permissions wider than `0600`.

**Fix:**
```bash
chmod 0600 ~/.yac/mcp/auth/<server-id>.json
```
