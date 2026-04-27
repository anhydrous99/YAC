# Integration Tests

This directory holds JSONL script files and documentation for deterministic
integration testing via `yac_test_e2e_runner`.

## JSONL Script Format

Each line in a `.jsonl` script file is a JSON object:

```json
{"on_user_prompt_contains":"<substr>","emit_text":"<canned response>","finish_reason":"stop"}
```

| Field | Type | Description |
|---|---|---|
| `on_user_prompt_contains` | string | Substring to match against the last user message in the request. First matching entry wins. Use `""` to match everything (catch-all). |
| `emit_text` | string | Text emitted as a single `TextDeltaEvent`. |
| `finish_reason` | string | `"stop"` (default). Reserved for future `"tool_calls"` support. |

Lines starting with `#` and empty lines are ignored.

## Running the E2E Runner

```bash
# Provide a temporary HOME so the runner does not touch ~/.yac/settings.toml
export TMPDIR=$(mktemp -d)
HOME="$TMPDIR" ./build/tests/yac_test_e2e_runner run "hello" \
  --auto-approve \
  --mock-llm-script=tests/integration/scripts/sample.jsonl
```

The runner exits 0 when the script entry matches and the canned text is emitted.
It exits 1 when `--mock-llm-script` is omitted or no script entry matches.

## Request Logging

Pass `--mock-request-log=<PATH>` to capture every `ChatRequest` received by the
mock provider as a JSON line appended to `<PATH>`. Useful for asserting that
the history, tools, and model fields are assembled correctly.

```bash
HOME="$TMPDIR" ./build/tests/yac_test_e2e_runner run "hello" \
  --auto-approve \
  --mock-llm-script=tests/integration/scripts/sample.jsonl \
  --mock-request-log=/tmp/requests.jsonl
```
