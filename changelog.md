# Changelog

## v1.3.1
- **CBF (alpha) now measures in tenths of a frame.** Windows read like `2.6`, `0.1` or `10.6`. The 1/2, 1/4 and 1/8 step options are gone.
- Decimal windows show **one decimal at most** (markers, results table). Whole numbers stay plain, e.g. `2`.
- The AFW menu toggle reads **CBF (alpha): On / Off**.
- **Counter rows are now ranges**: `0-1`, `1-2`, `2-3` … instead of single numbers, e.g. `2-3: 4`. A row holds windows from its first number up to (not including) the next, so `2` and `2.6` both count in `2-3`. With *Max window shown* at 16, rows run `0-1` through `15-16`; a window of exactly 16 counts in the last row, and nothing above 16 shows. `0-1` only fills up with CBF windows under 1 frame.
- Only the **circle markers** show the decimal window (e.g. `2.6`). The counter and the *Next* line show the range (e.g. `Next 2-3 in 12`). The results table still lists exact values.

## v1.3.0
- **CBF on/off toggle in the AFW menu.** Tap **CBF (alpha)** to switch it on or off. It turns green with the step size (e.g. `CBF (alpha): 1/4`) when on, gray when off.
- **CBF is now labeled alpha** instead of beta: it's experimental, and results may be off on some gamemodes.
- CBF settings are now split into **CBF precision (alpha)** (on/off) and **CBF step (alpha)** (1/2, 1/4 or 1/8 frame, default 1/4). If you had CBF set before, turn it back on with the toggle.

## v1.2.4
- Developer name changed to **Logout22w** to match the GD / Geode account.

## v1.2.3
- **Credits**: added **NaN GD** for the L\* precision model (NaNDL), covering Base / Nerve / Fatigue / CPS, the default K constants and the 24-hour target.

## v1.2.2
Fixes from the Geode index guidelines:
- **Credits** added to the mod description and LICENSE: Frame Window Counter (C0nscious / hyper-5) for the L\* math and `.fwc` format, Click Between Frames (syzzi) for the CBF tick-splitting idea, and maxnut's GDR libraries. Required for mods based on others' work.
- **Mod-prefixed node IDs** on everything the mod adds to the game (click markers, AFW menu and its buttons), for compatibility with other mods.
- The index build has no network access or downloads at all, so the "runtime code" rule doesn't apply. The update checker only exists in the GitHub build.

## v1.2.1
- **Two versions now**:
  - **GitHub version** (Releases page): keeps the in-game **update checker** with one-click updates.
  - **Geode index version**: no update checker, because Geode updates it automatically.
- Both are built from the same code on every update. The index build is published as a separate `-index` pre-release on GitHub, which is the link used for the Geode index.

## v1.2.0
- Geode index prep: tried removing the update checker (brought back for the GitHub version in v1.2.1).

## v1.1.0
- **New icon**: new mod logo (stacked red/orange/yellow/green timing bars on a hit line), and the pause-menu button uses the same bars instead of the "AFW" text.
- **Update checker**: on the main menu, checks GitHub for a newer release. If there is one, it shows the version and changelog with an **Update** button that downloads it straight into `geode/mods`, then offers a restart. Turn off with *Check for updates*.
- Prep for the official Geode index: logo, source link, repository info. Once it's on the index, Geode will update it automatically and the built-in checker will be removed (index rules).

## v1.0.2
- Counter rows are now `window: count`, e.g. `1: 3`, `2: 7`. The count goes up live as you pass clicks with that window.

## v1.0.1
- **Counter shows every window 1 to your max**: one row per window, no "f". Max 10 gives rows 1 through 10, max 12 gives 1 through 12. 
- Clicks above **Max window shown** are left out of the counter too, not just the markers and sounds. The Next line skips them.
- Rows shrink automatically so up to 25 fit on screen.
- Windows above 4 get their own colors (green to cyan as they get looser); 1/2/3/4 keep red/orange/yellow/green. Markers use the same colors.
- *Max window shown* default is now 10.
- "Next" line drops the "f" (e.g. `Next 2 in 12`).

## v1.0.0
- **CBF precision (beta)**: measure windows to **1/2, 1/4 or 1/8 of a frame**. The shifted click fires partway through a physics tick, done by splitting the tick the way Click Between Frames does. Whole frames find the edge first, then it refines between the last good and first bad frame, so it's only somewhat slower. Off by default. *Beta: may desync on some gamemodes (slopes, moving platforms).* Fractional windows show everywhere: markers (`1.75`), HUD, results table, `.fwc`, `.csv` and L\*.
- **Custom sound import**: pick your own `.ogg` / `.mp3` / `.wav` in settings. One sound per window size (1f / 2f / 3f / 4f+) plus a fallback for any click. Plays when you pass each measured click. Volume slider, optionally follows the game's SFX volume.
- **Max window shown** (1 to 25): markers and sounds only appear for clicks with a window up to this. Set it to 16 and nothing above 16 shows. Counts and L\* still use every click.
- The status bar shows fractional shifts while measuring in CBF mode.

## v0.3.1
- **L\* precision** (NaNDL model) calculated from your measured windows, in the background on level load and after every measure:
  - **Base**: raw precision from window sizes.
  - **Nerve** (K_T): later clicks in the level weigh more.
  - **Fatigue** (K_U): the more clicks you've done, the more each one weighs.
  - **CPS** (K_C): fast clicking weighs more.
  - Plus the combos **N+F, N+C, F+C** and **All**.
- **HUD L\* row**: live L\* so far vs. the whole level, e.g. `L* All 412.3 / 1240.0`. Pick the metric (or Off) with *L\* in HUD*.
- **View results** now shows Base / Nerve / Fatigue / CPS / All L\* at the top.
- New settings: L\* target time (default 24h), respawn time, and the K_T / K_U / K_C constants (NaNDL defaults).
- L\* math ported from Frame Window Counter by C0nscious (MIT) and checked to match it exactly.

## v0.2.0
- **AFW menu**: the pause button now opens a menu with Measure, Re-measure, View results, Clear results and Settings.
- **Re-measure**: remembers the last macro per level, so you can re-run without picking the file again.
- **Faster re-runs**: remembers how the macro synced on each level and tries that first. Syncing is usually 1 run instead of several.
- **Section measuring**: new *Section start % / end %* settings. Only clicks in that range are measured, and the results merge into what's saved, so long levels can be done in chunks.
- **View results**: scrollable table of every click with frame, time, window and early/late frames, color-coded by window.
- **Next click** line in the HUD: the upcoming click's window and how many frames until it. Toggle with *Show next click*.
- **Marker detail** option: shows early/late leeway under each marker, e.g. `-1 +0`.
- Results now store early/late per click. Measuring with a different macro replaces old results for that level instead of mixing them.
- Stopping early only replaces saved results up to the last finished click.

## v0.1.1
- In-level **click markers**: colored circle with the frame window at every click (red 1f, orange 2f, yellow 3f, green 4f+, `+` = hit the test cap).
- **Top-right HUD** counting 1f / 2f / 3f / 4f+ clicks passed out of the total. Resets on restart, works with practice respawns.
- Results are saved per level and load automatically when you play it.
- New settings: *Show click markers*, *Show window HUD*.

## v0.1.0
- First release. Replays a bot macro (.gdr / .gdr.json / .gdr2) at max speed and shifts each click earlier and later until death to find its real frame window.
- Auto-syncs to how the bot counted frames.
- Exports `.fwc` (Frame Window Counter) and `.csv`.
- No attempts, jumps, progress or completions are saved while measuring. Pause to stop; finished clicks are kept.

## Planned
- **CBF hundredths** (e.g. `2.65`): test mid-week and keep refining until it works, then ship it as an update (also to the Geode index if the mod is approved).
