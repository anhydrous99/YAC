# OpenAI Auth

YAC has a dedicated `openai` provider id for OpenAI. It supports the existing
API-key path and stored OpenAI auth. Stored auth can be either an API key saved
by YAC, a browser OAuth login, or a device OAuth login.

Use `openai-compatible` when you want to point YAC at another service that
speaks the OpenAI chat completions API. That generic provider still uses
`provider.base_url`, `provider.api_key_env`, and optional `provider.api_key`.

## Provider Config

```toml
[provider]
id          = "openai"
model       = "gpt-4o-mini"
base_url    = "https://api.openai.com/v1/"
api_key_env = "OPENAI_API_KEY"
# api_key   = ""
```

When only `id = "openai"` is set, YAC fills in those defaults. Explicit
`provider.model`, `provider.base_url`, and `provider.api_key_env` TOML values are
not overwritten by the preset, and `YAC_*` env overrides still win over TOML.
API-key mode uses OpenAI-compatible chat completions. Browser and device OAuth
use the ChatGPT/Codex Responses endpoint at
`https://chatgpt.com/backend-api/codex/responses`. That OAuth path follows
YAC's Codex-facing backend behavior, not a rule about the public OpenAI
Responses API schema. Each OAuth Responses request includes a YAC-authored
built-in top-level `instructions` baseline. Workspace, config, and Plan
instructions are appended after that baseline. The baseline is YAC-authored
behavior and does not copy OpenCode prompt text. The OAuth/Codex backend does
not accept the public API sampling `temperature` field, so YAC omits it on this
path.
Set `YAC_API_KEY_ENV` if you need `provider.api_key_env` to name a different
secret env var.

Auth precedence is `env API key > stored OpenAI auth > inline settings API key`.
That means `OPENAI_API_KEY` wins over a stored OAuth login or stored API key. A
stored login wins over `[provider].api_key` in `~/.yac/settings.toml`. Keep
`provider.api_key` unset when possible so secrets stay out of plaintext TOML.

## Commands

```bash
yac auth openai login
yac auth openai login --device
printf 'sk-...' | yac auth openai set-api-key --stdin
yac auth openai status
yac auth openai logout
```

`yac auth openai login` starts browser OAuth with the fixed callback
`http://localhost:1455/auth/callback`. `yac auth openai login --device` starts
the OpenAI device flow for headless shells: it prints
`https://auth.openai.com/codex/device` and a user code, then waits until OpenAI
approves or rejects the login. `set-api-key --stdin` reads one line from stdin
and stores it without asking you to type the secret in the TUI. `status` prints
the configured provider, stored credential type, and effective auth source
without showing secrets. `logout` clears stored OpenAI auth.

The device command prints only the verification URL and user code. It does not
print access tokens, refresh tokens, authorization codes, or PKCE verifiers.

Inside the TUI, `/auth openai login`, `/auth openai status`, and
`/auth openai logout` call the same OpenAI auth logic. Don't paste API keys into
the TUI. Use `OPENAI_API_KEY` or run:

```bash
printf 'sk-...' | yac auth openai set-api-key --stdin
```


## Plan Mode Notes

OpenAI OAuth changes are limited to YAC's Codex-facing protocol behavior. Plan
mode follows the same observable workflow used for OpenCode-style planning
without making YAC a full OpenCode clone.

In the TUI, press `Shift+Tab` to move between Plan and Build. In headless mode,
start planning with:

```bash
yac run --plan "prompt"
```

The first Plan request creates the active markdown plan under
`.opencode/plans/*.md`. Plan-mode writes and edits are limited to that active
plan file; source changes are reserved for Build. When the plan is ready, the
assistant calls `plan_exit` with the final plan. Approving that request writes
the active plan file, switches to Build, and sends one Build-mode reminder that
references the approved `.opencode/plans/*.md` path. Rejecting the request keeps
the chat in Plan.

## Storage

OpenAI provider auth is stored by provider auth storage. YAC tries the OS
keychain first. If the keychain isn't available, it falls back to
`~/.yac/provider/auth/openai.json` with owner-only permissions.
Set `YAC_OPENAI_AUTH_STORE=file` when running in headless or CI environments
where probing the OS keychain can block or prompt.

Stored auth may contain either an API-key credential or an OAuth credential.
`yac auth openai logout` removes the stored credential, but it can't remove an
API key exported in your shell or an inline settings key.

## Troubleshooting

### Env API Key Shadows Stored OAuth

**Symptom:** `yac auth openai status` shows stored OAuth, but requests still use
an API key.

**Cause:** Auth precedence is `env API key > stored OpenAI auth > inline settings
API key`.

**Fix:** Unset `OPENAI_API_KEY` in that shell, or change `provider.api_key_env`,
then restart YAC and run `yac auth openai status` again.

### OAuth Expired Or Revoked

**Symptom:** Requests fail after a previous browser login, or status shows an
OAuth credential but requests can't refresh it.

**Cause:** The refresh token expired or was revoked by OpenAI.

**Fix:** Run:

```bash
yac auth openai login
```

### Browser Launch Failure

**Symptom:** `yac auth openai login` can't open your browser.

**Cause:** The desktop opener isn't available, or the session can't start a
browser.

**Fix:** Copy the printed authorization URL into a browser on the same machine,
finish the login, and let the browser return to
`http://localhost:1455/auth/callback`. If port 1455 is unavailable or you are on
a headless host, run:

```bash
yac auth openai login --device
```

### Safe API-Key Entry Through Stdin

**Symptom:** You want to store an OpenAI API key without putting it in the TUI or
in `~/.yac/settings.toml`.

**Fix:** Pipe one line into the stdin-only command:

```bash
printf 'sk-...' | yac auth openai set-api-key --stdin
```

Avoid adding a leading space or extra lines. YAC stores the key through provider
auth storage and `yac auth openai status` reports only the credential type.

### Device Login Denied Or Expired

**Symptom:** `yac auth openai login --device` prints a URL and code, then exits
with a denied, failed, expired, or stopped message.

**Cause:** The code was rejected, expired before approval, or the process was
stopped while waiting.

**Fix:** Run `yac auth openai login --device` again and approve the newest code
at `https://auth.openai.com/codex/device`.
