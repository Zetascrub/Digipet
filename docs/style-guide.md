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

| Global | Role |
|---|---|
| `COLOR_BACKGROUND` | Page backdrop |
| `COLOR_CARD` | Card/panel fill — the neutral surface everything else sits on |
| `COLOR_TEXT` | Default label color, tuned for `COLOR_BACKGROUND`/`COLOR_CARD` only |
| `COLOR_MUTED` | Secondary/disabled text, unselected outlines |
| `COLOR_MINT` | Primary accent — page titles, primary CTAs |
| `COLOR_CYAN` | Secondary accent |
| `COLOR_PURPLE` | Tertiary accent / selected-state fill |
| `COLOR_WARNING` | Caution state |
| `COLOR_DANGER` | Destructive/negative state, confirm-to-delete fills |

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
- **2** — button/tile labels, primary values (brightness percentage, stat
  numbers).
- **1** — secondary/helper text: subtitles under a title, hints, muted
  captions, small badges.

Page titles are `COLOR_MINT`; subtitles directly under them are usually
`COLOR_CYAN` or `COLOR_MUTED`. Neither of those is the fill-contrast problem
above — a page title sits on the plain backdrop, not a colored fill — but
keep the same instinct: if a title or subtitle ever gets a colored panel
behind it instead of the plain backdrop, it needs `readableTextColor()` too.

## Verifying a UI change

`tools/sim/`'s native render harness (see `tools/sim/README.md`) renders
real page-drawing code to PNG without a physical board, and now takes an
optional `--theme=` flag (`cyber-mint`/`amber-core`/`violet-link`/
`mono-signal`, or a 0–3 index) that re-points every `COLOR_*` global the
same way Settings > Theme does:

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
   literal RGB565 constant.
2. If the fill isn't `COLOR_BACKGROUND`/`COLOR_CARD`, its label/icon color is
   `readableTextColor(fill)`, not `COLOR_TEXT`.
3. Selected/unselected (or available/unavailable) states swap both fill and
   outline color together.
4. Rendered and eyeballed across all 4 fixed themes via the render harness.
