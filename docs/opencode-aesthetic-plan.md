# YAC Aesthetic Alignment Plan

Repo-specific plan to move YAC's terminal UI closer to the current OpenCode
visual language while keeping YAC terminal-native, FTXUI-first, and simpler
than OpenCode's broader desktop and web shell.

Last reviewed: April 21, 2026

---

## Objective

Improve the aesthetics of YAC so the product feels calmer, more intentional,
and more "coding workspace" than "chat app".

The target is not a pixel-for-pixel clone of OpenCode. The target is to borrow
the parts of OpenCode that materially improve usability and polish:

- flatter, quieter surfaces
- one dominant work column
- stronger visual hierarchy
- clearer operational state
- better theme flexibility
- less noisy persistent chrome

YAC should keep its own identity as a compact fullscreen TUI.

---

## Target Traits

When we say "more in-line with OpenCode", this is the working definition:

1. The transcript should read like a work log, not a messaging app.
2. The composer should feel docked and intentional, not like a generic footer.
3. Status, context usage, queue state, approvals, and active mode should be
   visible without dominating the screen.
4. Tool output should look like part of the same system as messages.
5. Themes should be semantic and swappable, not hardcoded to one palette.
6. Empty states, overlays, menus, and approval flows should feel cohesive.

---

## Current State Summary

### Strengths

- YAC already has a centralized presentation layer in `src/presentation/`.
- Color usage is mostly centralized in `src/presentation/theme.*`.
- Transcript, tool cards, dialogs, command palette, and slash menu are separate
  enough that they can be restyled without large architectural churn.
- Theme behavior already has basic config wiring through `[theme]` in
  `settings.toml`.

### Current Gaps

- `src/presentation/theme.hpp` and `src/presentation/theme.cpp` define a single
  hardcoded Catppuccin Mocha theme.
- `src/presentation/chat_ui.cpp` uses stacked footer bands for stats, hints,
  and input, which creates too much chrome at the bottom of the screen.
- `src/presentation/message_renderer.cpp` renders user and assistant content as
  mirrored chat bubbles, which pushes the product toward "chat app" framing.
- `src/presentation/tool_call/renderer.cpp` uses a separate visual language for
  tool output, making the transcript feel fragmented.
- `src/presentation/dialog.cpp`, `command_palette.cpp`, and
  `slash_command_menu.cpp` use heavier boxed surfaces and simpler hierarchy than
  the target look.
- The only exposed theme configuration today is
  `theme.sync_terminal_background`.

---

## Design Principles

### 1. Content First

The assistant response, tool output, and active work should occupy the visual
center of gravity. Supporting UI must recede.

### 2. Semantic Styling

Colors and spacing should be driven by semantic roles such as:

- canvas
- panel
- raised panel
- accent
- weak text
- strong text
- success
- warning
- error

This is a prerequisite for multiple themes and consistent polish.

### 3. Flat but Layered

Prefer subtle surface changes, accent rails, and spacing over heavy borders and
nested boxes.

### 4. Explicit State

If the system is thinking, queued, awaiting approval, or running tool calls,
that should be obvious from the UI without requiring extra reading.

### 5. Keep the TUI Honest

We should not imitate OpenCode features that depend on a richer windowing shell
unless they clearly translate to a terminal experience.

---

## Non-Goals

These items are explicitly out of scope for this effort unless the aesthetic
work reveals a hard dependency:

- rebuilding YAC around a desktop app shell
- adding a sidebar, file tree, or web client just to resemble OpenCode
- copying OpenCode branding or exact layout
- major interaction changes to chat, tool execution, or service architecture
- feature parity with OpenCode's theme marketplace or titlebar system

---

## Workstreams

### 1. Theme System Overhaul

#### Goal

Replace the current single-theme implementation with a semantic theme system
that can support an OpenCode-inspired default theme and at least one fallback.

#### Primary Code Areas

- `src/presentation/theme.hpp`
- `src/presentation/theme.cpp`
- `src/presentation/render_context.hpp`
- `src/chat/settings_toml.cpp`
- `src/chat/settings_toml_template.hpp`
- `tests/test_theme.cpp`
- `tests/test_theme_tool_colors.cpp`

#### Planned Changes

- Expand theme roles beyond the current fixed groups to support:
  - base canvas
  - panel background
  - raised panel background
  - border or separator
  - accent rail
  - strong, body, weak, and muted text
  - focused and selected states
  - status colors
- Introduce named themes such as:
  - `opencode`
  - `catppuccin`
  - `system`
- Add config fields under `[theme]`, likely:
  - `name`
  - `sync_terminal_background`
  - optional future flags like density or chrome style
- Keep `Theme::Instance()` as the runtime access point, but make it backed by
  chosen theme selection instead of a fixed compile-time skin.

#### Acceptance Criteria

- YAC can launch with a non-default theme selected in `settings.toml`.
- The default theme feels materially closer to OpenCode than Catppuccin Mocha.
- Existing theme tests are updated and expanded to validate the new theme map.

---

### 2. Transcript Layout and Message Styling

#### Goal

Shift the transcript from mirrored "chat bubbles" to a flatter, more editorial
single-column workspace feel.

#### Primary Code Areas

- `src/presentation/message_renderer.cpp`
- `src/presentation/message_renderer.hpp`
- `src/presentation/message.cpp`
- `src/presentation/render_context.hpp`
- `src/presentation/chat_ui_render_plan.*`

#### Planned Changes

- Reduce visual separation between user and assistant cards.
- Stop relying on left/right bubble symmetry as the main differentiator.
- Use lighter surface changes and accent rails instead of strong bubble blocks.
- Tighten header typography so timestamps and state labels read as metadata.
- Make active assistant output feel more "live session" and less "message sent".
- Ensure tool cards, sub-agent output, and approvals visually align with the
  transcript rather than interrupt it.

#### Specific Direction

- Assistant messages become the visual default.
- User messages remain distinct, but through accent and metadata, not by
  looking like a separate chat client.
- Status text like `thinking`, `queued`, `error`, and `cancelled` should become
  more systematic and visually consistent.

#### Acceptance Criteria

- A screenshot of a typical session reads as one coherent work surface.
- The transcript feels less boxed and less symmetrical.
- Streaming responses remain visually clear during animation.

---

### 3. Tool Card Unification

#### Goal

Make tool output look like a first-class transcript primitive instead of a
second design system.

#### Primary Code Areas

- `src/presentation/tool_call/renderer.cpp`
- `src/presentation/tool_call/descriptor.cpp`
- `src/presentation/collapsible.cpp`
- `src/presentation/theme.*`

#### Planned Changes

- Replace heavy tool headers with lighter section headers and accent rails.
- Use accent colors for semantics such as read, edit, search, diff, and error,
  but only as supporting cues.
- Rework collapsible group cards so expanded and collapsed states fit the same
  surface language as messages.
- Improve visual handling for:
  - diffs
  - file reads
  - searches
  - LSP results
  - sub-agent blocks

#### Acceptance Criteria

- Tool cards no longer feel visually detached from the rest of the session.
- Diff and error states remain easy to scan without requiring loud color blocks.
- Grouped tool calls feel like a compact summary of work, not stacked widgets.

---

### 4. Composer and Status Rail Redesign

#### Goal

Replace the current stacked footer chrome with a docked composer and a more
intentional status model.

#### Primary Code Areas

- `src/presentation/chat_ui.cpp`
- `src/presentation/composer_state.*`
- `src/presentation/chat_ui_input_controller.*`
- `src/presentation/chat_ui_overlay_state.*`

#### Planned Changes

- Collapse the current bottom stack into:
  - a compact status rail
  - a docked composer surface
- Remove always-visible shortcut boilerplate from the main chrome.
- Preserve high-signal status only:
  - provider/model
  - token usage
  - queue depth
  - transient status
  - active agent state
- Give the composer a stronger docked identity with better padding, subtle
  layering, and cleaner interaction affordances.

#### Desired Result

The bottom of the app should feel closer to a working dock than a multiline
status footer.

#### Acceptance Criteria

- The bottom region occupies less visual noise while still showing critical
  state.
- The composer feels raised and intentional.
- Shortcut guidance remains discoverable through help, not constant chrome.

---

### 5. Overlay, Modal, and Menu Refresh

#### Goal

Bring dialogs, command palette, slash menu, and approval UI into the same
surface and hierarchy language as the refreshed transcript.

#### Primary Code Areas

- `src/presentation/dialog.cpp`
- `src/presentation/command_palette.cpp`
- `src/presentation/slash_command_menu.cpp`
- `src/presentation/chat_ui_overlay_state.cpp`

#### Planned Changes

- Reduce border heaviness and visual boxing.
- Use more intentional selected states and focus treatment.
- Improve density and spacing in command palette rows.
- Make slash command autocomplete feel closer to a lightweight command surface
  than a generic bordered popup.
- Restyle tool approval dialogs so they feel operational and serious without
  looking like system alerts from another app.

#### Acceptance Criteria

- Overlays feel related to the rest of the app, not bolted on.
- The command palette becomes cleaner and easier to scan.
- Approval flows remain obvious and trustworthy.

---

### 6. Typography, Rhythm, and Spacing Cleanup

#### Goal

Replace ad hoc spacing and blank-line composition with reusable layout rhythm.

#### Primary Code Areas

- `src/presentation/message_renderer.cpp`
- `src/presentation/tool_call/renderer.cpp`
- `src/presentation/dialog.cpp`
- `src/presentation/command_palette.cpp`
- `src/presentation/markdown/renderer.cpp`

#### Planned Changes

- Standardize card padding values.
- Standardize metadata spacing.
- Standardize menu row spacing and widths.
- Reduce reliance on literal `"  "` and empty text rows to create rhythm.
- Improve markdown/code block balance so long technical output remains readable.

#### Acceptance Criteria

- Shared spacing rules exist and are reused.
- The UI feels intentionally typeset rather than manually padded.

---

### 7. Empty States and Documentation Assets

#### Goal

Make sure the refreshed UI is reflected in product-facing docs and empty-state
copy.

#### Primary Code Areas

- `README.md`
- `docs/yac-screenshot.svg`
- `docs/yac-demo.svg`
- any empty-state rendering in `src/presentation/chat_ui.cpp`

#### Planned Changes

- Refresh empty-state language and spacing.
- Replace preview assets with screenshots that reflect the new aesthetic.
- Update README copy if theme or configuration capabilities change.

#### Acceptance Criteria

- README screenshots no longer advertise the old look.
- Empty transcript and palette states feel polished.

---

## Implementation Phases

### Phase 1: Theme Foundation

#### Scope

- semantic theme model
- theme registry
- settings support for theme selection
- updated theme tests

#### Why First

Without this phase, every later restyle will bake OpenCode-inspired colors and
surface assumptions directly into render code.

#### Deliverables

- `opencode` theme
- fallback theme(s)
- config parsing
- updated default settings template

---

### Phase 2: Transcript and Tool Surface Rewrite

#### Scope

- message card restyle
- tool card unification
- updated sub-agent and status presentation

#### Why Second

This creates the largest visual improvement with the least product-level risk.

#### Deliverables

- flatter transcript
- coherent tool surfaces
- better metadata and status hierarchy

---

### Phase 3: Composer and Footer Chrome

#### Scope

- docked composer
- compact status rail
- reduced always-on shortcut hints

#### Why Third

Once transcript surfaces settle, the bottom dock can be designed to complement
them instead of fighting them.

#### Deliverables

- new bottom layout in `chat_ui.cpp`
- help-driven shortcut guidance
- cleaner provider/model/context display

---

### Phase 4: Overlays and Menus

#### Scope

- command palette refresh
- slash menu refresh
- help modal refresh
- approval modal refresh

#### Why Fourth

These should inherit the final spacing, selection, and surface rules from the
first three phases.

#### Deliverables

- coherent overlays
- cleaner search surfaces
- stronger operational approval UI

---

### Phase 5: Final Polish and Asset Refresh

#### Scope

- empty states
- screenshots
- README updates
- final spacing and contrast pass

#### Deliverables

- updated docs assets
- final visual cleanup

---

## Suggested Backlog by File Cluster

### Cluster A: Theme and Config

- `src/presentation/theme.hpp`
- `src/presentation/theme.cpp`
- `src/chat/settings_toml.cpp`
- `src/chat/settings_toml_template.hpp`
- `tests/test_theme.cpp`
- `tests/test_theme_tool_colors.cpp`

### Cluster B: Transcript

- `src/presentation/message_renderer.cpp`
- `src/presentation/chat_ui_render_plan.cpp`
- `src/presentation/chat_ui.cpp`

### Cluster C: Tool Surfaces

- `src/presentation/tool_call/renderer.cpp`
- `src/presentation/collapsible.cpp`

### Cluster D: Overlays

- `src/presentation/dialog.cpp`
- `src/presentation/command_palette.cpp`
- `src/presentation/slash_command_menu.cpp`
- `src/presentation/chat_ui_overlay_state.cpp`

### Cluster E: Docs and Assets

- `README.md`
- `docs/yac-screenshot.svg`
- `docs/yac-demo.svg`

---

## Validation Plan

### Functional Validation

- build the app successfully
- run the existing test suite
- add or update tests for theme selection and theme defaults
- verify no regressions in:
  - streaming responses
  - scrolling
  - command palette navigation
  - slash menu navigation
  - tool approval flows
  - grouped tool call collapse state

### Visual Validation

Manual screenshot review should cover:

- empty session
- normal user + assistant exchange
- streaming assistant response
- tool-heavy coding session
- permission request
- command palette
- slash menu
- help view
- error state

### Terminal Validation

Verify behavior in at least:

- one truecolor terminal with dark background
- one lighter or custom terminal palette if `system` theme is added

---

## Risks

### 1. Overfitting to OpenCode

If we copy surface decisions without respecting FTXUI and the TUI medium, YAC
will feel derivative and awkward.

Mitigation:

- adopt design principles, not exact structure
- keep YAC simpler than OpenCode

### 2. Theme Refactor Spillover

Because theme roles are used broadly, expanding the model may touch many files.

Mitigation:

- do theme work first
- land semantic roles before major restyles

### 3. Readability Regression

A flatter UI can reduce separation too far if the contrast and spacing are not
carefully tuned.

Mitigation:

- validate long markdown answers, diffs, and tool-heavy sessions

### 4. Footer Rewrite Regressions

The bottom area currently carries several behaviors in one place.

Mitigation:

- separate visual cleanup from interaction changes
- keep existing input behavior stable while changing layout

---

## Success Criteria

This effort is successful when:

1. The default YAC UI feels materially closer to OpenCode in tone and quality.
2. The transcript reads as a coding workspace rather than a bubble chat app.
3. The composer and status areas are calmer and more intentional.
4. Tool output visually belongs to the same system as normal messages.
5. Theme selection is configurable and tested.
6. README screenshots reflect the new UI.

---

## Recommended First Implementation Slice

If this work starts immediately, the best first slice is:

1. add semantic theme roles
2. add `theme.name` config support
3. create an `opencode`-inspired default theme
4. restyle message cards using the new theme tokens
5. restyle tool cards to match the new transcript

This slice creates the biggest visible improvement without requiring a full
footer or modal rewrite on day one.
