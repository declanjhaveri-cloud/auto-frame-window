# Changelog

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
- **v1.0.0**: Click-between-frames (CBF) support, released as **beta**.
