# Auto Frame Window

Measures the frame window of every input in a level, automatically.

1. Open the level, pause, press the **AFW** button.
2. Pick your bot macro (.gdr, .gdr.json or .gdr2).
3. Press **Start** and leave the game alone. It replays the level at max speed, nudging one input at a time earlier and later until you'd die.
4. When it's done, the level shows a colored marker with the window at every click, plus a top-right HUD counting 1f / 2f / 3f / 4f+ clicks. A `.fwc` for Frame Window Counter and a `.csv` are saved too.

Marker colors: red = 1 frame, orange = 2, yellow = 3, green = 4+. A `+` means the window hit the test cap and may be bigger.

Pause at any time to stop. Finished inputs are still saved.

No attempts, jumps or progress are saved to your level while it runs.
