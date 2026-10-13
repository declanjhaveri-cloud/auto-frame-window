# Changelog

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
- **v0.3.1**: L* precision (Base, Nerve, Fatigue, CPS) from measured windows.
- **v1.0.0**: Click-between-frames (CBF) support, released as **beta**.
