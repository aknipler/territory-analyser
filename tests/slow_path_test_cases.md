# Test cases: incremental slow path (slow add + slow remove)

Companion to `slow_path_plan.md`. These define "done" for the incremental slow path. The assertion
in every integration case is the oracle `validateAgainstFullRecompute` returning **`0/0/0/0`**
(fillBoard, gap-cell sets, partition, per-fill props all equal to a from-scratch `initialiseFill`).

**Harness (proposed):** a `tests/test_slow_path.cpp` driver (new CMake target `slow-path-test`,
mirroring `gap-seam-test`). For each case it: sets a Config via `AppConfig::set` (small map,
`validateIncrementalFills=true`), builds a `TerritoryAnalyser` from a tiny board file under
`examples/slow/<case>.txt`, applies the operation with `analyser.updateObstruction(...)`, and asserts
the oracle passed AND the slow path (not the degenerate `initialiseFill` fallback) was taken. Cases
pass **today** (slow == full recompute) and must keep passing once the incremental path lands — write
them first (TDD / regression guard).

Board legend: `.` open · `#` obstruction (player in the op) · `~` non-walkable water · digits = the
owning player's territory region label (informational). Coordinates are `(x,y)`, x = row, y = col,
matching the codebase. Footprints are `[x,x+w) × [y,y+h)`.

---

## A. Core topology cases

### T1 — slow ADD closes a shape (split 1 → 2)  ★ primary add case
A single open region with a U-shaped wall that is one cell from closed. Add the closing obstruction.
```
before:                 after add (5,2):
. . . . . . .           . . . . . . .
. # # # # # .           . # # # # # .
. #       # .           . # . . . # .      <- interior pocket now sealed
. #       # .           . # . . . # .
. # # . # # .           . # # # # # .      <- (5,2) added closes the gap
. . . . . . .           . . . . . . .
```
- Expect: **slow** path; the one fill splits into interior pocket + outer region (≥2 fills, possibly
  +1 bridge fill on the now-internal seam). Oracle `0/0/0/0`.

### T2 — slow REMOVE opens a shape (merge 2 → 1)  ★ primary remove case
Exact inverse of T1: start sealed (two fills: interior + outer), remove `(5,2)`.
- Expect: **slow** path; interior + outer fills **merge** into one. Oracle `0/0/0/0`. This is the
  case the old scoped reflood got wrong (visited-blocked the merge).

### T3 — slow ADD creates a gap but does NOT close (topology unchanged)
Wall with a wide opening; add one obstruction that narrows but does not seal it.
- Expect: **slow** path (neighbour within Cheb-2) but **same fill count**; gap groups change only.
  Oracle `0/0/0/0`. Guards against spurious splits.

### T4 — slow REMOVE creates a gap but does NOT open  ★ the (17,42) regression
Reproduce the example1 connected-house removal: `remove(17,42,"House",4)`. Gap cells appear at
~`(17,40),(18,39),(21,41)` along the interior wall; **no merge** (region stays one fill, still
player-4 owned per the external-gap/closure work).
- Expect: **slow** path; fill count unchanged; new gap group(s) recorded interior-side, beyond the
  footprint. Player-4 region remains `dominant=4`. Oracle `0/0/0/0`. (Pin the exact gap cells — this
  is the case that exposed the old gap-beyond-bbox bug.)

### T5 — slow ADD wall-thickening (no topology change)
Add an obstruction flush against an existing wall (thicken it), no enclosure or gap change.
- Expect: **slow** path, identical partition + gaps (only the footprint cell leaves its fill). Oracle
  `0/0/0/0`.

### T6 — slow REMOVE frees footprint cells, no merge
Remove an obstruction sitting inside a single fill's interior (surrounded by that one fill); freed
footprint cells are reclaimed into the surrounding fill.
- Expect: **slow** path; one fill grows by the footprint cells; no merge/split. Oracle `0/0/0/0`.

---

## B. Structural-faithfulness cases

### T7 — bridge fill touched
Set up a near-closed shape whose connected-gap set is materialised as a **bridge fill** by
`fillConnectedGapSets`. Then (a) ADD near the bridge and (b) REMOVE near the bridge.
- Expect: the post-change fills vector reproduces `initialiseFill`'s structure **including the bridge
  fill** (partition invariant, not just fillBoard). Oracle `0/0/0/0`. Guards risk R5.

### T8 — change touches ≥3 fills at once
A wall junction where removing one obstruction connects three regions, or adding one splits a region
bordering two others.
- Expect: affected-set `A` contains all touched fills; merge/split resolves correctly across all.
  Oracle `0/0/0/0`.

### T9 — nested enclosure (fill inside a fill)
Outer ring fully encloses an inner ring which encloses a pocket. ADD to close the inner ring / REMOVE
to open it, leaving the outer intact.
- Expect: only the inner topology changes; outer fill untouched; grouping/closure of the inner
  recomputed. Oracle `0/0/0/0`.

### T10 — map-edge closure
A wall that, together with the **map edge**, encloses a region (`closedWithMapEdge`). ADD the segment
that anchors the second edge-touch (closes) / REMOVE it (opens).
- Expect: edge-closure transition handled; the region gains/loses its enclosure and dominant.
  Oracle `0/0/0/0`.

### T11 — defeated player's obstruction removed
With player P defeated (territory already zeroed), `remove` one of P's obstructions on the slow path.
- Expect: `defeatedPlayers` threaded through the reflood exactly as `completeBoardFill` does; no
  spurious territory. Oracle `0/0/0/0`. Guards risk R3 (run on both player and team boards; team
  passes `defeatedPlayers=nullptr`).

### T12 — slow change flips the interior dominant
A slow add/remove that both changes topology AND shifts the enclosed fill's dominant player (e.g.
adds a ranged obstruction whose influence flips ownership of the newly-sealed pocket).
- Expect: split/merge AND dominant re-resolved in the global tail. Oracle `0/0/0/0`. Exercises the
  slow-path ∩ flip interaction.

### T13 — large-fill merge (performance / fallback, Phase-1 limitation R4)
Remove the wall between a small sealed pocket and the **large open field**, forcing a merge that
reclears+refloods the big fill.
- Expect: Oracle `0/0/0/0` (correctness). Additionally **measure** time; if the incremental path is
  not faster than `initialiseFill` for this case, confirm the `|cleared cells| > threshold` guard
  falls back to full recompute (still `0/0/0/0`).

---

## C. Unit tests (pure helpers, via the `gap-seam-test`-style harness)

### T-U1 — `clearFills` index remap
Given a fills vector and a `fillAssignmentBoard`, clear a subset and assert: cleared cells → `-1`;
surviving fills compacted; every surviving cell's `fillAssignmentBoard` index still points to the
same `Fill`. Include: clear-none, clear-all, clear-middle, clear-first/last. Guards risk R2.

### T-U2 — `selectAffectedFills` window
On a synthetic `fillAssignmentBoard`, assert the window scan around a footprint returns exactly the
fill indices within Chebyshev-`K`, including both fills flanking a one-cell wall (the merge precursor)
and excluding fills `K+1` away. Parameterise `K`.

---

## D. Cross-cutting assertions (apply to every integration case)

1. **Oracle `0/0/0/0`** on BOTH the player board and the team board.
2. **Idempotence:** applying the op then its inverse returns to the original fill partition
   (e.g. T1 add then T2 remove == start), each step oracle-clean.
3. **Path taken:** the slow incremental branch ran (not the `initialiseFill` fallback) — assert via a
   path-tag/counter added during implementation.
4. **No erase-of-unrelated-fills:** fills whose bbox is outside the window are byte-identical
   (same cells, gaps, dominant) before vs after — catches over-clearing regressions.

---

## E. Seeds / fixtures

- Reuse `examples/example1.txt` for T4 (the canonical connected-house case) — it already has the
  player-4 enclosure and the `remove(17,42,"House",4)` step in `main.cpp`.
- T1/T2/T3/T5/T6 get tiny hand-built boards under `examples/slow/` (≤12² for fast oracle recompute).
- T7–T13 get purpose-built boards; keep each ≤24² so the full-recompute oracle stays sub-millisecond.
