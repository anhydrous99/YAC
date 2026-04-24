# Incomplete Features

Wiring added during the OpenCode aesthetic alignment that is not yet fully
consumed by the presentation layer.

## 1. `ThemeDensity` enum — defined but never read

`ThemeDensity::Compact` and `ThemeDensity::Comfortable` are declared in
`theme.hpp` and every preset sets `density = ThemeDensity::Comfortable`, but
no rendering code branches on the density value. The intent was to let a
`Compact` density tighten padding, row heights, and section gaps across the
UI. Currently it is a no-op field on the `Theme` struct.

**Files involved:**
- `src/presentation/theme.hpp` — enum + field declaration
- `src/presentation/theme.cpp` — set in all three presets

**What remains:**
- Read `density` in renderers and conditionally apply tighter spacing (e.g.
  halve `kCardPadY`, reduce `kRowGap`, single-line tool headers).
- Expose a `[theme] density = "compact"` config field + `YAC_THEME_DENSITY`
  env override.

## 2. `SemanticRoles` — partially adopted

The 13-field `SemanticRoles` struct is fully populated in all three presets
and used in most restyled components, but several semantic tokens are defined
without any consumer:

| Token | Consumers | Status |
|---|---|---|
| `focus_ring` | 0 | Unused — intended for focused input/button outlines |
| `selection_bg` | 0 | Unused — intended for selected text or list highlight bg |
| `surface_raised` | 0 | Unused — intended for elevated cards/popovers |
| `border_strong` | 0 | Unused — intended for prominent dividers |
| `surface_canvas` | 1 (`dialog.cpp` backdrop) | Lightly used |
| `surface_panel` | 1 (`chat_ui.cpp` composer) | Lightly used |

**What remains:**
- Apply `focus_ring` to the focused input element in the command palette and
  composer.
- Apply `selection_bg` to the selected command palette row and slash menu row
  (currently these use `accent_primary` left-bar instead of bg highlight —
  could layer both).
- Apply `surface_raised` to tool card bodies or popover overlays.
- Apply `border_strong` to section dividers (e.g. between transcript and
  composer).

## 3. `RegisterTheme` / `ListThemes` — public API without UI surface

The theme registry exposes `RegisterTheme()` for adding custom presets at
runtime and `ListThemes()` for enumerating available presets. Both work
correctly (tested), but neither is surfaced in the UI:

- No command palette entry to switch themes at runtime.
- No `/theme` slash command.
- `ListThemes()` is never called outside tests.

**What remains:**
- Add a "Switch Theme" command palette entry (similar to "Switch Model")
  that calls `ListThemes()` and lets the user pick a preset.
- Wire the selection back through `InitializeTheme()` — or, since
  `InitializeTheme` is init-once, implement a hot-reload path if runtime
  switching is desired.

## 4. `CardColors` — populated but unused after transcript flattening

`cards.user_bg` and `cards.agent_bg` are set in all three presets but the
transcript restyle (T20) removed their only consumer in `message_renderer.cpp`.
The user message no longer uses `CardSurface()` with `user_bg`, and the agent
message no longer wraps in `agent_bg`. The `CardSurface()` helper still
exists but is only called from `chat_ui.cpp` for sub-agent and setup panels.

**What remains:**
- Either remove `CardColors` from the theme struct (breaking change for
  custom presets) or re-introduce subtle card backgrounds as an option
  gated by `ThemeDensity::Comfortable`.
- Audit remaining `cards.agent_bg` uses in `chat_ui.cpp` to decide if they
  should migrate to `semantic.surface_canvas` or `semantic.surface_panel`.

## 5. Spacing constants `kSectionGap`, `kComposerPadX`, `kComposerPadY` — defined but unused

`ui_spacing.hpp` defines six constants. Three are consumed (`kCardPadX`,
`kCardPadY`, `kRowGap`), but three are never referenced:

| Constant | Used | Intended for |
|---|---|---|
| `kCardPadX` | Yes | Horizontal padding inside cards |
| `kCardPadY` | Yes | Vertical padding inside code blocks |
| `kRowGap` | Yes | Inline gap in menu rows |
| `kSectionGap` | No | Vertical gap between transcript sections |
| `kComposerPadX` | No | Horizontal padding inside composer |
| `kComposerPadY` | No | Vertical padding inside composer |

**What remains:**
- Apply `kSectionGap` in `RenderAll()` (message list) for the blank line
  between messages (currently hardcoded as `ftxui::text("")`).
- Apply `kComposerPadX` / `kComposerPadY` in `chat_ui.cpp` `BuildInput()`
  for composer internal padding.

## 6. `system` theme — functional but visually untested at depth

The `SystemPreset()` uses `ftxui::Color::Default` for all surface colors and
FTXUI named colors (`Color::Blue`, `Color::Green`, etc.) for accents. It
works — yac launches and renders — but the color mapping was not validated
against common terminal themes (Solarized, Dracula, Gruvbox, etc.). Some
combinations may produce poor contrast (e.g. `Color::GrayDark` on a light
terminal background).

**What remains:**
- Visual QA across 3-4 popular terminal color schemes.
- Consider a `Color::Default` fallback audit: any place that uses
  `bgcolor(Color::Default)` may render differently on light vs dark
  terminals.

## 7. vhs screenshot capture — tape scripts exist, SVGs not regenerated

`docs/yac-screenshot.tape` and `docs/yac-demo.tape` were created as
reproducible vhs scripts, but vhs was not available on the build machine.
The existing SVG files in `docs/` are from the pre-restyle era and do not
reflect the current UI.

**What remains:**
- Install vhs (`go install github.com/charmbracelet/vhs@latest`).
- Run `vhs docs/yac-screenshot.tape` and `vhs docs/yac-demo.tape` to
  regenerate the SVGs.
- Commit the updated SVGs.
