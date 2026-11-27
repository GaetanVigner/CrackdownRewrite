# Any360 against ReXGlue, 2026-11-26

The README's benchmark table, raw. Six launches a variant, alternating (ReXGlue first), 130 s each, scripted route
from the title screen into the city, uncapped. Crackdown on Any360 81b3a4b, Release; Ryzen 7 7800X3D (all 16 logical
processors: the script's 0-7 mask is reset by ReXGlue's runtime at startup, on both variants), RTX 5070 Ti, Windows 11.

- `runs.txt`: the launch script's output ("=== <variant> (<log>)", private memory 2 s before the end, last lines).
- `logs/`: each launch's frame lines (`Crackdown: guest frames` every 10 s, the game's own swaps, the same code on
  both runtimes; Any360's own renderer lines too), errors and the runtime choice. Filtered from the full logs.
- `summary.txt`: `python aggregate.py runs.txt logs --max-fps 600 --skip 1`: per launch the steady periods' means (the
  first period dropped as warm-up, the one with the lowest 1% low reported apart as the dip, title screen and
  loading periods above 600 FPS left out), per variant the medians, and a Mann-Whitney rank test (z) against ReXGlue.
- `bench.ps1`: the launch script (machine lock, a 0-7 mask that did not hold, variants' arguments).

ReXGlue's two errors a launch are its own file system's (`ResolvePath(cache:\...xuifrontend) failed - device not
found`), present in every ReXGlue launch and harmless.
