# UI style guide

This documents the drawing conventions `src/ui_pages.cpp`/`src/main.cpp`
already follow, written down after a real bug: the Battle page's HOST/FIND
buttons (and several others, once we went looking) always drew their label
in `COLOR_TEXT` regardless of the button's fill color. `COLOR_TEXT` is tuned
to read on this app's dark page chrome, and on several themes it goes close
to unreadable against a bright accent fill (Cyber Mint's cyan HOST button,
Amber Core's amber FIND button, etc.) — a WCAG contrast ratio as low as
1.02:1 in the worst case, against a 3:1 minimum for large/bold text. This
file exists so the next new button doesn't repeat it, and so any UI change
has one place to check itself against before merging.

If you only read one section, read "Text and icons on a colored fill" below
— it's the rule that was broken, and the one most likely to be broken again.

## The palette

Every page reads color exclusively through nine `COLOR_*` globals
(`include/ui_pages.h`), never a literal RGB565 value. `applyTheme()`
(`src/main.cpp`) repoints all nine at once when the user changes Settings >
Theme, which is what makes theme switching work at all — a hardcoded color
anywhere in a page renderer is invisible on 4 of the app's 5 themes' worth
of testing and a guaranteed readability bug on at least one of them.

The four fixed themes' actual RGB565 values live in exactly one place:
`kThemes[]` (`include/ui_pages.h`). `applyTheme()` reads it to switch
themes, the Settings > Theme picker's preview swatches read it to show what
a theme looks like before it's selected, and `tools/sim`'s render harness
reads it for `--theme=`. If you need a theme's literal colors for anything
— a preview, a test, a one-off tool — read them from `kThemes[]`; don't
retype the hex values a second time. Two hand-copied tables agreeing today
is luck, not a guarantee — `kThemes[]` replaced what used to be two
separate copies of the same nine hex values (`applyTheme()`'s own table and
the render harness's) precisely so a third copy — the Theme picker's
preview swatches, added alongside `kThemes[]` itself — didn't have to
become a third place to keep in sync by hand.

| Global | Role |
|---|---|
| `COLOR_BACKGROUND` | Page backdrop |
| `COLOR_CARD` | Card/panel fill — the neutral surface everything else sits on |
| `COLOR_TEXT` | Default label color, tuned for `COLOR_BACKGROUND`/`COLOR_CARD` only |
| `COLOR_MUTED` | Secondary/disabled text, unselected outlines — see its own contrast note below |
| `COLOR_MINT` | Primary accent — page titles, primary CTAs |
| `COLOR_CYAN` | Secondary accent |
| `COLOR_PURPLE` | Tertiary accent / selected-state fill |
| `COLOR_WARNING` | Caution state |
| `COLOR_DANGER` | Destructive/negative state, confirm-to-delete fills |

**`COLOR_MUTED`'s own contrast is checked now, not assumed.** Every fixed
theme's `COLOR_MUTED` shipped without ever being run through WCAG contrast
math against the two surfaces it's actually drawn on (`COLOR_CARD`/
`COLOR_BACKGROUND`) — three of the four landed at 3.1-4.4:1 against
`COLOR_CARD`, under the 4.5:1 floor for normal-size text, worst in Violet
Link at 3.11:1. `kThemes[]`'s `muted` field is now the minimal lerp toward
that theme's own `COLOR_TEXT` that clears 4.5:1 against both surfaces —
if you touch a fixed theme's palette, re-run the same check (a short
Python WCAG-contrast script) rather than eyeballing it, the same way
`readableTextColor()` exists so button fills don't get eyeballed either.
AUTO's `COLOR_MUTED` gets a lighter-touch version of the same idea
(blended toward its own `COLOR_TEXT` rather than scaled down from the
genome's own secondary color) since its background/card are genome-derived
and can land anywhere — a heuristic improvement, not a provable-for-every-
genome fix.

Five themes exist: AUTO (index 0, derived per-pet from the companion's own
genome palette in `applyTheme()` — background/card follow `primaryDark`,
accents follow the genome's own palette, so an AUTO-theme render isn't
reproducible from a fixed table), plus four fixed palettes — Cyber Mint,
Amber Core, Violet Link, Mono Signal — each a full override of all nine
globals. AUTO means `COLOR_TEXT` isn't even a fixed color in the general
case; treat it as arbitrary and untrustworthy against anything but the
background/card pair it's actually tuned for.

## Text and icons on a colored fill

**The rule:** any label or icon drawn on top of a fill color that isn't
`COLOR_BACKGROUND`/`COLOR_CARD` itself — a button, a selected grid cell, a
badge — must get its color from `readableTextColor(fill)`
(`include/ui_pages.h`), never a hardcoded `COLOR_TEXT`.

```cpp
// Wrong -- COLOR_TEXT is tuned for COLOR_BACKGROUND/COLOR_CARD, not
// whatever `fill` happens to be on the active theme.
display->fillRoundRect(x, y, w, h, radius, fill);
drawCenteredInRect(label, x, y, w, h, 2, COLOR_TEXT);

// Right
display->fillRoundRect(x, y, w, h, radius, fill);
drawCenteredInRect(label, x, y, w, h, 2, readableTextColor(fill));
```

`readableTextColor()` is a thin wrapper (`src/ui_pages.cpp`) around
`color_utils.h`'s `pickReadableColor(fill, light, dark)`: it compares
`fill`'s luma to `COLOR_TEXT` and `COLOR_BACKGROUND` and returns whichever
is farther away. That's a cheap integer-only proxy for contrast, not
gamma-correct WCAG math — it's right in 19 of the 24 (theme × accent-color)
combinations this app actually ships, and in the other 5 the "wrong" pick
still clears 3:1 contrast. Good enough for a virtual pet's buttons; don't
reach for it as a certified-accessible-anything primitive.

`color_utils.h`/`.cpp` have no Arduino/GFX/theme dependency of their own —
that's deliberate, so the color math is unit-testable on the host
(`test/test_color_utils`, run with `pio test -e native`) the same way
`pet_genome.cpp`/`familiar_battle_rules.cpp` are, independent of whether the
firmware itself builds.

Known call sites already following this rule, if you need a template:
`drawBattleButton()`, `drawChoiceRow()`, the Settings brightness grid, the
Genome Lab RANDOM/CLONE/BLEND buttons, the battle-result COPY GENOME/RETURN
buttons, and the Genome Profile/Player ID/Evolution Debug overlay buttons in
`src/main.cpp`.

## Cards, panels, and buttons

- Corner radius scales with element size: ~13–20px for buttons and grid
  cells, ~23–32px for full-width cards and page panels. Two nested
  `RoundRect` radii differing by ~5–7px is the standard card-with-outline
  look (`display->fillRoundRect(x, y, w, h, 20, COLOR_CARD)` immediately
  followed by `display->drawRoundRect(x, y, w, h, 20, accent)`).
- A neutral surface is `COLOR_CARD` fill + an accent-colored outline, not an
  accent fill — reserve a solid accent fill for the thing on the page that
  should read as actionable (a primary button, a selected grid cell).
- Buttons that glow (`drawPanelGlow()`) do so before the fill, as two
  concentric `drawRoundRect` calls at `radius+3`/`radius+6` in
  `scaleRgb565(color, 22)`/`scaleRgb565(color, 12)` — a soft falloff of the
  button's own fill color, not a fixed white/COLOR_MINT halo.
- Selected vs. unselected state is a fill-color swap (usually `COLOR_PURPLE`
  vs. `COLOR_CARD`) plus an outline-color swap (`COLOR_MINT` vs.
  `COLOR_MUTED`) together — swapping only one reads as broken/half-selected.

## Typography

Text size is `1`/`2`/`3` (`display->setTextSize()`), used consistently by
role, not by what happens to fit:

- **3** — page titles only (`drawCentered(title, 15-18, 3, COLOR_MINT)`).
- **2** — anything that's *content*: data the user actually reads to
  understand their pet or make a decision — stat values, stat labels, tile
  labels, row titles, button labels. This used to be narrower ("primary
  values" only) until a real bug showed why: a label at size 1 sitting
  right next to the size-2 value it names doesn't just look small, it
  reads as broken, because the two things a single row is trying to say
  together no longer look like they belong to each other. The Status
  page's own FOOD/JOY/ENERGY/HEALTH rows and the PVP stats sheet's
  LEVEL/HP/ATTACK/... rows were both exactly this bug.
- **1** — secondary/helper text only: subtitles under a title, swipe/tap
  hints, page-count indicators ("1 / 2"), and a tile's *description* line
  underneath its (size-2) label — "SCAN WIFI+BLE" under "FEED", "REROLLS
  ELEMENT" under "TYPE SHIFT". That last case isn't the label/value bug
  above: a tile's label and its description are different roles (title
  vs. subtitle, the same relationship a page title has with the subtitle
  under it), not two halves of one data point that need to match.

**The rule, concretely: if two pieces of text are naming and showing the
same one thing — a label and the value it labels, two buttons that are
equally-weighted alternatives — they're the same size.** Reserve size 1 for
text that's genuinely a different, lesser role than whatever's next to it,
never as a way to fit a label that would otherwise be too wide; if a label
doesn't fit at size 2, the fix is more width or a shorter label, not a
smaller label.

Page titles are `COLOR_MINT`; subtitles directly under them are usually
`COLOR_CYAN` or `COLOR_MUTED`. Neither of those is the fill-contrast problem
above — a page title sits on the plain backdrop, not a colored fill — but
keep the same instinct: if a title or subtitle ever gets a colored panel
behind it instead of the plain backdrop, it needs `readableTextColor()` too.

## Transitions

Every full-frame transition in the app — page swipes, Settings sub-view
pushes, Settings brightness-grid paging, and full-screen overlays opening
or closing (Genome Profile, Player ID, Rivals, Evolution Debug) — goes
through one function: `playSlideTransition()` (`src/main.cpp`). It composes
an outgoing and incoming frame into a single eased slide (smoothstep,
`frameCount` steps at `frameTimeMs` each, adaptively paced against however
long the panel blit itself actually takes) and is the only place that owns
that curve. A new full-frame UI change should render its outgoing/incoming
frames into `pageCanvasA`/`pageCanvasB` and hand them to it rather than
inventing a second transition style — that's what keeps every swipe in the
app feeling like the same app.

**Overlays specifically:** a full-screen overlay (something opened by a tap
that covers the whole display and is dismissed back to whatever was
underneath — the pattern Genome Profile/Player ID/Rivals/Evolution Debug
all follow) opens via `presentOverlayEntrance(fromPage, drawOverlay)` and
closes via `presentOverlayExit(drawOverlay, toPage)`, both in
`src/main.cpp`. Don't hard-cut an overlay open/closed with a plain
`renderPageToCanvas()` + blit — that was the state of all four of these
before this convention existed, and the instant pop-in read as
inconsistent next to every other transition in the app sliding smoothly.
The one deliberate exception is the OTA Update screen's repeated
in-progress redraws (`presentUpdatePage()`): those aren't opening or
closing anything, just refreshing a percentage in place many times a
second, so they stay a hard blit — see that call site's own comment before
changing it.

## Previewing a set of choices

If a picker lets the user choose among a small fixed set of *visual*
options — so far, only Settings > Theme, but the same would apply to a
future icon pack or color-accent picker — show what each option actually
looks like on the choice row itself, not just its name. `drawThemeSwatch()`
(`src/ui_pages.cpp`) is the existing example: four small dots of that
theme's own accent colors, read from `kThemes[]`, next to each row's label.
Before it existed, "AMBER CORE" vs. "VIOLET LINK" was a guess from the name
alone. A label-only list is the same failure mode as this file's central
contrast rule, one level up: making the user reason about what a color
choice will look like instead of just showing them.

## Verifying a UI change

`tools/sim/`'s native render harness (see `tools/sim/README.md`) renders
real page-drawing code to PNG without a physical board, and now takes an
optional `--theme=` flag (`cyber-mint`/`amber-core`/`violet-link`/
`mono-signal`, or a `kThemes[]` index 1–4) that re-points every `COLOR_*`
global the same way Settings > Theme does:

```bash
tools/sim/render.sh battle-fight /tmp/battle.png 2 amber-core
```

Before merging a UI change, render every page it touches across all 4 fixed
themes (AUTO isn't a fixed palette — see above — so it's not part of this
sweep) and eyeball each one for a label that's gone low-contrast against its
fill. This is exactly how the HOST/FIND bug and the other 7 sites like it
were actually found — by rendering, not by reasoning about hex values.

## Checklist for a new button/tile/badge

1. Fill comes from a `COLOR_*` global or a value derived from one — never a
   literal RGB565 constant. If it needs a fixed theme's literal colors,
   they come from `kThemes[]`, not a retyped copy.
2. If the fill isn't `COLOR_BACKGROUND`/`COLOR_CARD`, its label/icon color is
   `readableTextColor(fill)`, not `COLOR_TEXT`.
3. Selected/unselected (or available/unavailable) states swap both fill and
   outline color together.
4. A new full-screen overlay opens/closes through `presentOverlayEntrance()`/
   `presentOverlayExit()`, not a hard cut.
5. A picker among visual options (a theme, an icon set, ...) previews each
   option rather than naming it.
6. A label sits at the same text size as the value it labels (Typography
   above) — never smaller just because it's the shorter string.
6. Rendered and eyeballed across all 4 fixed themes via the render harness.
