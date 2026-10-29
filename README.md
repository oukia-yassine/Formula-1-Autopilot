# y3racer – A Formula 1 Autopilot in C

`y3racer` is an autonomous driver for the **Grand Prix F1** programming challenge (ENSICAEN, semester 6, 2025–2026). It reads a race track from standard input, then drives a car around it by choosing an acceleration vector each turn, aiming to reach the finish line as fast as possible without crashing or running out of fuel.

The driver is written in strict ANSI C and compiles without warnings with `-ansi -pedantic -Wall -Wextra`, as required by the challenge rules.

---

## Table of contents

- [The challenge in brief](#the-challenge-in-brief)
- [Repository structure](#repository-structure)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [How the driver works](#how-the-driver-works)
- [Debugging](#debugging)
- [Known limitations and ideas](#known-limitations-and-ideas)
- [Credits](#credits)

---

## The challenge in brief

A race takes place on a rectangular grid made of four terrain types:

| Symbol | Terrain | Effect |
|:---:|---|---|
| `#` | Track | Normal driving |
| `~` | Sand | Speed limit drops to 1 and costs 1 extra fuel per turn |
| `.` | Off-track | Forbidden (the car is stopped) |
| `=` | Finish | Reach it to finish the race |
| `1`–`3` | Start | Starting positions of the three drivers |

Each turn the driver:

1. receives its own position and the positions of the two opponents (`x y  x y  x y`);
2. answers with an acceleration `ax ay` within **1 second**.

Rules that shape the strategy:

- Acceleration components are in `{-1, 0, 1}` (5 one-time boosts allow `{-2..2}`).
- The speed norm is capped at 5 (1 in sand).
- The new position is `position + speed`, and **every intermediate cell on the straight line** must be free (no cutting corners through off-track cells or cars).
- Fuel use per turn: `ax² + ay² + ⌊√(3(vx² + vy²) / 2)⌋`, plus 1 in sand. At zero fuel the car stops.
- Any violation immobilises the car and resets its speed to zero.

The track, protocol and scoring are described in the official subject (`instruction_2026.pdf`). The race manager ("GDC") is a Qt5 application distributed separately.

## Repository structure

```
y3racer/
├── Makefile              # Builds and installs the driver (ANSI, -Wall -Wextra -pedantic)
├── y3racer.c             # The driver: map parsing, heatmap, beam search, main loop
└── follow_line/          # Line-traversal helper provided with the challenge
    ├── follow_line.c
    ├── follow_line.h
    ├── Makefile
    └── main.c            # Example usage (not linked into the driver)
```

## Requirements

- Linux (the competition runs on Ubuntu 22.04)
- `gcc` and `make`
- To actually race: the Grand Prix race manager (GDC), which needs Qt 5 (`qmake`, Qt development packages)

## Build and run

The driver is meant to live next to the race manager. Place this folder inside the GDC directory (alongside `drivers/` and `tracks/`):

```
GrandPrix2026_4.0.4/
├── GrandPrix            # built with qmake && make
├── drivers/
├── tracks/
└── y3racer/             # this repository
```

Then:

```bash
cd y3racer
make              # compile ./y3racer
make install      # compile and copy the binary to ../drivers/
make try          # install, then launch the race manager (../GrandPrix)
```

In the GDC window, pick a track and select `y3racer` in one of the driver slots.

Other targets:

```bash
make SANITIZE=on install   # build with AddressSanitizer (debugging crashes)
make clean                 # remove object files and backups
make distclean             # also remove the binary
```

> **Before submitting to the competition:** rebuild without `SANITIZE`, and remove stderr logging and `-g`, since both slow the driver down. Do not include executables in your archive.

## How the driver works

### 1. Parsing the map

At start-up the first line gives `width height fuel`, followed by `height` lines of terrain. The map is stored as a dynamically allocated array of strings (`Map`).

### 2. Distance heatmap (BFS from the finish line)

`generate_heatmap` runs a **multi-source breadth-first search** starting from every `=` cell and expanding through all non-off-track cells in 8 directions. The result is an `int` grid where each value is the minimum number of king-moves to the finish. It is computed **once** and reused every turn, giving a cheap and reliable "how far am I from the goal?" estimate on any track, however twisty.

### 3. Move validation

`is_move_valid` reproduces the race manager's rules before a move is ever sent:

- the target cell is inside the map;
- every cell on the segment (walked with the provided `initLine` / `nextPoint`) is not off-track;
- speed limit: norm² ≤ 25 normally, ≤ 1 if the move touches sand.

This keeps the car from crashing and lets the search discard illegal branches.

### 4. Beam search over future moves

`get_best_action` explores future trajectories with a **beam search**:

- **Depth:** 6 turns ahead.
- **Branching:** the 9 standard accelerations (`ax, ay ∈ {-1,0,1}`).
- **Beam width:** the 15 best states are kept after each level.
- **State:** position, velocity, remaining fuel, and the *first* acceleration that led to it.
- **Fuel model:** the official consumption formula plus the sand penalty; branches that run out of fuel are dropped.
- **Ranking:** states are sorted by `heatmap − 3 × fuel`, favouring states close to the finish while preserving fuel.
- **Decision:** the first acceleration of the trajectory that reached the lowest heatmap value is returned.

Because the search reasons about velocity, it learns to brake before corners and to carry speed on straights, which a purely greedy "go toward the finish" approach cannot do.

### 5. Main loop

For each turn the driver reads the three positions, **derives its current velocity from the position change** (so crashes and stops reported by the GDC are handled naturally), runs the search, and prints `ax ay` on stdout (flushed immediately, as required).

### Complexity

Per turn the work is bounded by `6 depths × 15 states × 9 accelerations` move validations, each walking at most a handful of cells, plus a small sort of ≤135 candidates per depth. This runs well under the 1-second limit.

## Debugging

The GDC captures stderr into `stderr_driver_N.txt` and shows it live through the **StdErr** button. `y3racer` logs the map size, starting fuel, and, each round, its position, velocity and fuel estimate.

For segmentation faults, build with `make SANITIZE=on install` (this requires `libasan` matching your `gcc` version) and read the stack trace in the StdErr window.

## Known limitations and ideas

Being upfront about what the current version does *not* do:

- **Fuel tracking:** the remaining fuel in the driver's state (`pilot.gas`) is initialised from the map header but is not decreased between turns, so long-term fuel planning is optimistic after the first moves. Updating it with the real consumption each round is the most valuable fix.
- **Boosts unused:** the five boosts (`BOOSTS_AT_START`) are counted but never requested; the search only explores the 9 normal accelerations.
- **Opponents ignored:** opponent positions are parsed but not used, so the search can plan through a cell an opponent occupies and would be stopped by a collision.
- **Heuristic is geometric:** the heatmap measures distance in cells, not turns; it ignores speed limits and sand costs. Weighting sand cells in the BFS would be a simple improvement.
- **Fixed parameters:** depth (6), beam width (15) and fuel weight (3) are constants, not tuned per track.
- **Sorting:** candidates are sorted with a simple O(n²) exchange sort; fine at this size, but a `qsort` would be cleaner.

## Credits

- Race manager (GDC): Julien Gosme, then Gilles Lebrun, Sébastien Fourey and Éric Ziad-Forest.
- Challenge subject and `follow_line` helper: Tristan Benoit & Éric Ziad-Forest, ENSICAEN.
- Driver `y3racer`: *add your team name / authors here.*
