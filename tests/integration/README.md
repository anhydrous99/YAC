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
| `on_user_prompt_contains` | string | Substring to match against the last user message in the request. First matching entry wins. Use `""` to match everything. |
| `emit_text` | string | Text emitted as a single `TextDeltaEvent`. |
| `finish_reason` | string | `"stop"` by default. Reserved for future `"tool_calls"` support. |

Lines starting with `#` and empty lines are ignored.

## Bedrock JSONL Script Format

`MockBedrockProvider` (`tests/mock_bedrock_provider.hpp`) reads the same
line-oriented JSONL format but uses Bedrock-shaped fields and emits
Bedrock-style streaming events:

```json
{"on_user_prompt_contains":"<substr>","text":"<response>","tool_use":{},"usage":{},"inline_error":{},"stop_reason":"<reason>","delay_ms":0}
```

| Field | Type | Description |
|---|---|---|
| `on_user_prompt_contains` | string | Substring match against the last user message. First match wins. Use `""` for catch-all. |
| `text` | string | Emitted as a single `TextDeltaEvent`. |
| `tool_use` | object | Single tool call: `{"id":"<id>","name":"<name>","input":{...}}`. |
| `usage` | object | Token counts: `{"input_tokens":N,"output_tokens":M}`. |
| `inline_error` | object | Stream error: `{"type":"<type>","message":"<msg>"}`. |
| `stop_reason` | string | `"guardrail_intervened"` or `"content_filtered"` emits `ErrorEvent`. |
| `delay_ms` | int | Sleep before emitting, checked against cancellation every 10 ms. |

The provider ends with `FinishedEvent` on normal completion or `CancelledEvent`
when the `stop_token` was requested. If no entry matches, it emits `ErrorEvent`
and `FinishedEvent`.

## Running The E2E Runner

Build the runner:

```bash
bazel build //tests:yac_test_e2e_runner
```

Run it directly from Bazel's output tree:

```bash
export TMPDIR="$(mktemp -d)"
HOME="$TMPDIR" "$(bazel info bazel-bin)/tests/yac_test_e2e_runner" run "hello" \
  --auto-approve \
  --mock-llm-script=tests/integration/scripts/sample.jsonl
```

The runner exits 0 when the script entry matches and the canned text is emitted.
It exits 1 when `--mock-llm-script` is omitted or no script entry matches.

## Request Logging

Pass `--mock-request-log=<PATH>` to capture every `ChatRequest` received by the
mock provider as a JSON line appended to `<PATH>`. This is useful for asserting
that history, tools, and model fields are assembled correctly.

```bash
HOME="$TMPDIR" "$(bazel info bazel-bin)/tests/yac_test_e2e_runner" run "hello" \
  --auto-approve \
  --mock-llm-script=tests/integration/scripts/sample.jsonl \
  --mock-request-log=/tmp/requests.jsonl
```
