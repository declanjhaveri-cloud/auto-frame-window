# Auto Frame Window

Geode mod for Geometry Dash 2.2081 (Geode 5.10.1+). Measures the frame window of every input in a level automatically, using your bot macro.

## How it works

1. **Sync:** replays the macro untouched to learn how the bot counted frames (tries a few timings and picks the one that beats the level).
2. **Measure:** for each click it shifts that one input 1, 2, 3… frames earlier until you'd die, then later. `window = 1 + early + late` frames.
3. **Save:** writes `<level>_<time>.fwc` (import into Frame Window Counter) and a `.csv` to the mod's save folder.

Each test replays from the level start at max speed (no rendering slowdown, 4 physics ticks per call). Long levels with lots of clicks take a while. The status bar shows an ETA.

While it runs, it doesn't save attempts, jumps, progress or completions, and your clicks are ignored. Pause to stop; finished inputs are still saved.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Max frames to test | 8 | Max shift each way. Windows at the cap are flagged `capped` in the csv. |
| Frames to survive after input | 240 | 1 second. Raise it if an input's mistake kills you later than that. |
| Measure releases too | off | For ship/wave/swing holds. About 2× longer. |
| Frame counting | Auto | Leave on Auto unless syncing fails. |
| Speed (ms per frame) | 14 | Higher = faster, choppier. |

## Supported macros

`.gdr` (json/msgpack), `.gdr.json`, `.gdr2`. Recorded at 240 TPS (others are converted but may be off). No start positions.

## Build

Push this folder to a GitHub repo. The included workflow (`.github/workflows/build.yml`) builds it with Geode's official action. Download the `auto_frame_window` artifact from the Actions run; inside is `declan.auto-frame-window.geode`. Drop it in `Geometry Dash/geode/mods`.

## Limits (v0.1)

- Every test restarts from the level start (checkpoint-based speedup is a possible next step).
- Random triggers aren't seeded, so levels that rely on them may not sync.
- Macros relying on xdBot "frame fixes" may not sync.
