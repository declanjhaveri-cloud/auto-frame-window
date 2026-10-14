# Auto Frame Window

Measures the frame window of every input in a level, automatically.

1. Open the level, pause, press the **AFW** button, then **Measure**.
2. Pick your bot macro (.gdr, .gdr.json or .gdr2).
3. Press **Start** and leave the game alone. It replays the level at max speed, nudging one input at a time earlier and later until you'd die.
4. When it's done, the level shows a colored marker with the window at every click, plus a top-right counter with one row per frame window (1 up to your *Max window shown*). A `.fwc` for Frame Window Counter and a `.csv` are saved too.

Marker colors: red = 1 frame, orange = 2, yellow = 3, green = 4+. A `+` means the window hit the test cap and may be bigger.

Pause at any time to stop. Finished inputs are still saved.

No attempts, jumps or progress are saved to your level while it runs.

The AFW menu also has **Re-measure** (same macro again), **View results** (table of every click) and **Clear results**. Use *Section start / end %* in settings to measure a long level in chunks.

**L\*** (Base / Nerve / Fatigue / CPS / All) is calculated from the measured windows and shown in the HUD and results.

**Sounds:** import your own click sounds (per window size) in settings.
**Max window shown:** counter rows go 1 up to this (max 25); clicks above it are hidden everywhere.
**CBF precision (alpha):** toggle it in the AFW menu to measure windows to a tenth of a frame (e.g. 2.6). Experimental.

## Credits
- The **L\* precision model** (Base / Nerve / Fatigue / CPS and the default constants) and the 24-hour target are from **NaN GD**'s [NaNDL](https://nandl.pages.dev/).
- **L\* precision math** and the **.fwc** file format come from [Frame Window Counter](https://github.com/hyper-5/frame-window-counter) by **C0nscious / hyper-5** (MIT license). The L\* calculation is a port of its calculator.
- The **CBF precision (alpha)** tick-splitting approach is based on [Click Between Frames](https://github.com/theyareonit/Click-Between-Frames) by **syzzi** (MIT license).
- Macro reading uses [GDReplayFormat](https://github.com/maxnut/GDReplayFormat) and [GDR-converter](https://github.com/maxnut/GDR-converter) by **maxnut**.
