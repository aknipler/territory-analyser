# Plan: incremental slow path for `updateFill` (slow add + slow remove)

**Status:** design only — do not implement until greenlit. Companion: `medium_path_handover.md`
(superseded for the merge/split asymmetry but still the reference for gap-traversal detail and the
water-closure dependency). Verification authority: `validateAgainstFullRecompute` (the oracle).

---

## 1. Goal & current state

`updateFill` dispatches on `isFastPath` (`!hasNearbyObstructions`, i.e. another obstruction within
Chebyshev-2 of the footprint):

- **Fast path** (isolated footprint, topology can't change): local membership/count update; on a
  dominant flip, a global "tail" (`mergeFills → removeShapesByGapThreshold → encode`) over the
  existing fills, no reflood. Oracle-faithful.
- **Slow path** (neighbour within Cheb-2 → topology may change): currently **`return
  initialiseFill(...)`** — a full board recompute. Correct but ~30 ms on the 168² map.

Goal: replace the full recompute with an **incremental** slow path for both `add` and `remove`,
holding the oracle to `0/0/0/0` (fillBoard, gap-cell sets, partition, per-fill props).

A previous scoped attempt exists, **disabled**, in `updateFill` (the dead block after the slow-path
`return`). It clears fills overlapping the DSU-group bbox and refloods that bbox. It is kept for
reference but diverges from a full recompute — §3 explains why; this plan supersedes it.

---

## 2. The pipeline a faithful result must reproduce

`initialiseFill` (src/fill.cpp:2380) is the ground truth. Per recompute:

1. `initialise` → per-player obstruction boards, merged `obstructionsBoard`, `cellsToFill` (=-1).
2. `dilateObstructionsAndTerrain` → `dilatedWalls`.
3. `completeBoardFill` → flood every open (non-wall, non-dilation) cell into `Fill`s; per-fill
   `gapAnalysisAndDilationConversion` records gap groups + converts unreachable dilation to cells.
4. `fillConnectedGapSets` → materialise each connected-gap set as a **bridge `Fill`** (this is why a
   "gapGroups-only" shortcut fails the *partition* invariant — bridge fills are real fills).
5. `mergeFills` → per-fill `checkClosureOwner` (now guarded by `closureCouldMatter`), external-gap
   counts (`setExternalGapGroupCount`), union-find grouping by dominant.
6. `removeShapesByGapThreshold` → per-fill external-gap closure gate.
7. `encodeFillsAndGapsForOutput` → `fillBoard`, gap sets; `collectInteriorGroups`.

**Key locality fact:** steps 5–7 are *global but cheap* and depend only on each fill's own cells,
gap groups, and obstruction-neighbours. The expensive part is step 3 (the board-wide flood). So the
incremental slow path only needs to **reproduce the exact cells + gap groups of the fills the change
touched**, then re-run steps 4–7 over the whole (mostly-unchanged) fills vector — exactly what the
fast-path flip tail already does.

---

## 3. Why the old scoped reflood diverges (root causes to avoid)

1. **Affected set chosen by DSU-group bbox**, not by what the change actually touches. A bbox can
   miss a fill that borders the changed wall (under-clear → stale cells/gaps) or clip a fill that
   spans in/out of the bbox.
2. **Reflood blocked at every surviving fill** (`visited` = all non-cleared). Correct for splits,
   **wrong for merges**: when a removed wall connects fill A (cleared) to fill B (not cleared), the
   reflood stops at B's cells, so A and B stay separate — a full recompute makes them one fill.
3. **Surviving fills keep stale gap groups** that referenced cells which the change turned into
   obstruction / another fill.
4. **Gaps extend beyond the bbox** along the wall (the removed-house case: gap cells at (17,40),
   (18,39), (21,41) — interior-side, ~3 tiles along the wall), so a bbox-bounded gap pass misses
   them.

The fix is a correctly-chosen **affected-fill set** that is *closed* under the merge/split the
operation can cause, cleared and reprocessed so cells + gaps come out identical to a full recompute.

---

## 4. Recommended design — unified "clear affected fills → reprocess → global tail"

Both add and remove use one mechanism; the merge (remove) vs split (add) behaviour **emerges** from
the reflood rather than being special-cased.

### 4.1 Select the affected-fill set `A`

Walls change only inside the footprint, so only fills bordering the footprint can split, merge, or
have their gaps change. Build `A` by scanning `fillAssignmentBoard` in a window:

```
window = footprint expanded by K cells (Chebyshev), K = dilationRadius(1) + maxGapChain(2) + 1 = 4
A = { every fill index found in fillAssignmentBoard within the window } ∪ { the fill containing each
      footprint cell, for "add" }  (∪ one hop of gap-connected neighbours — see 4.4 / risk R1)
```

- **Remove:** both fills on either side of the removed wall border the footprint → both land in `A`
  → clearing both lets one reflood claim them as a single merged fill (root cause #2 fixed).
- **Add:** the fill being split contains/borders the footprint → it lands in `A` → reflooding the
  cleared region around the new wall yields ≥2 fills.

`K` is a safety radius; **the oracle decides if it is big enough** — if any case shows a partition
or gap diff, widen `K` or add the gap-connected hop (R1). Over-inclusion is always *correct* (just
slower), so start generous.

### 4.2 Clear `A`

For each fill in `A`: set its cells to `-1` in `fillAssignmentBoard` and `0` in `fillBoard`. Remove
those fills from the `fills` vector and **remap surviving indices** in `fillAssignmentBoard` (the old
code's Step 6 does this correctly — reuse it). Build `visited = (fillAssignmentBoard != -1)` so the
reflood is bounded by the *surviving* fills and by walls/dilation (from the post-change
`masterObstructionBoard`, which already reflects the add/remove).

### 4.3 Reflood the cleared cells

Rebuild `obstrBoard`/`dilatedWalls` from the post-change `masterObstructionBoard` (add → footprint
is now wall; remove → footprint is now open and floodable). Then, for every cleared cell that is
unvisited / non-wall / non-dilation, seed a `floodFill` + `gapAnalysisAndDilationConversion`
(src/fill.cpp:1815) exactly as `completeBoardFill` does. Iterate cells of the cleared fills (not a
bbox) so freed footprint cells and along-wall gap cells are all covered (root cause #4 fixed).
Merges and splits both fall out of this naturally.

### 4.4 Global tail (identical to `initialiseFill` steps 4–7)

Run, over the **whole** fills vector, in order:
`fillConnectedGapSets → mergeFills → removeShapesByGapThreshold → encodeFillsAndGapsForOutput →
collectInteriorGroups`. Safe because surviving fills have empty `connectedGapSets_` (already consumed
in their original recompute) and unchanged gaps/neighbours; closure/grouping/threshold are recomputed
globally so any **non-local** ripple (a far fill that becomes enclosed/contested by the wall change)
is handled here — these steps are cheap relative to a board-wide flood.

### 4.5 Add vs remove — the only real differences

| | Remove (open) | Add (close) |
|---|---|---|
| Footprint cells | become **floodable** → reclaimed by the reflood | become **wall** → excluded from reflood; were previously fill/dilation, so clearing the containing fill is required |
| Topology outcome | ≥2 fills in `A` may merge into 1 | 1 fill in `A` may split into ≥2 |
| Affected-set seed | reverse-lookup `obstructionNeighbours_` for the removed obstruction's cells is exact; the window scan is the simple superset | window scan around the new footprint (no reverse-lookup — obstruction didn't exist pre-add) |

---

## 5. Correctness argument

The result is faithful iff the cleared-and-refloofed fills come out with exactly the cells + gap
groups a full `initialiseFill` would give them, and every *un*cleared fill is genuinely unchanged.

- **Uncleared fills unchanged:** their cells border only walls that did not change (else they'd be in
  the window), so their flood region and gap groups are identical to a full recompute. ✓ (relies on
  `K` being a true separator — R1.)
- **Cleared fills faithful:** they are reflooded by the *same* `floodFill` + `gapAnalysis` routines,
  on the *same* post-change boards, bounded by the *same* surrounding walls and surviving fills as a
  full recompute would be. Merges/splits emerge identically. ✓
- **Non-local closure/grouping/threshold:** recomputed globally in 4.4. ✓

This is the same faithfulness contract the fast-path flip tail already meets; the only new surface is
the scoped reflood (4.1–4.3), which the oracle pins cell-exactly.

---

## 6. Phasing

- **Phase 1 — correctness (this plan):** unified clear+reflood + global tail, oracle `0/0/0/0` on all
  §test cases. Accept that a remove which merges a *large* fill (e.g. the open field) reclears+refloods
  that fill (≈ full-recompute cost for that case) — correct but not yet optimal.
- **Phase 2 — performance (optional, later):** avoid reflooding large fills.
  - *Remove/merge:* union the cleared fills with `Fill::merge` (src/fill.cpp:194) — concatenate cells
    + obstruction-neighbours + gaps — then recompute only the seam's gaps. No flood.
  - *Add/split:* bounded **interior** flood (handover §4 option-b): seed from the interior side of the
    closing wall, bounded by wall+dilation, to carve the smaller pocket; the outer fill = original
    minus the pocket via `evictCellsInFootprint`-style delta. Floods only the small side.
  Each Phase-2 optimisation is gated behind the same oracle; ship Phase 1 first.

Phase 2 needs reliable **close/open detection** (and eventually **water** closure — the island-border
graph, handover §7). Phase 1 does **not** need close/open detection — it reprocesses regardless, so it
is the safe first milestone.

---

## 7. Risks / open questions

- **R1 — separator radius `K`.** If a single change can connect two fills whose only contact is at
  the footprint but one extends far, the window still catches both (both touch the footprint), so `K`
  around the footprint suffices in principle. The unknown is gap reach along walls; mitigation: start
  with `K=4` **and** add a one-hop gap-connected expansion of `A`; let the oracle shrink it.
- **R2 — index remap bugs.** Compacting `fills` while keeping `fillAssignmentBoard` consistent is the
  classic footgun (it bit `removeShapesByGapThreshold` before). Reuse the existing Step-6 remap and
  cover it with a unit test (§ test cases T-U1).
- **R3 — defeated players / Gaia.** Reflood must thread `defeatedPlayers` exactly as
  `completeBoardFill` does; team boards pass `nullptr`. Cover with a defeated-player case.
- **R4 — performance of large-fill merges** (Phase-1 limitation, see §6) — measure; if it regresses
  vs full recompute for that case, fall back to `initialiseFill` when `|cleared cells| > threshold`.
- **R5 — bridge fills.** Cleared bridge fills (from `fillConnectedGapSets`) must not be treated as
  ordinary fills when selecting `A`; they have empty `obstructionNeighbours_`. The window scan finds
  them via `fillAssignmentBoard`; clearing+rebuilding via `fillConnectedGapSets` in 4.4 re-creates
  them. Verify with a bridge-fill case (T7).

---

## 8. Implementation checklist (when greenlit)

1. Extract a helper `selectAffectedFills(footprint, window, fillAssignmentBoard, fills) → set<int>`
   (pure, unit-testable — T-U2).
2. Extract the clear+remap into `clearFills(indices, fills, fillAssignmentBoard, fillBoard)` (reuse
   old Step-6; unit-testable — T-U1).
3. Reflood the cleared cells (reuse `floodFill` + `gapAnalysisAndDilationConversion`).
4. Call the existing global tail (already a clean sequence).
5. Replace the slow-path `return initialiseFill(...)` with the above; keep a guarded fallback to
   `initialiseFill` for degenerate inputs and (R4) oversized clears.
6. Run the full §test-cases suite with `validateIncrementalFills=true` → all `0/0/0/0`.

---

## 9. Test strategy (detail in `tests/slow_path_test_cases.md`)

The oracle is the assertion. Two layers:
- **Integration (primary):** for each scenario, build a small board, apply the slow add/remove with
  `validateIncrementalFills=true`, assert `validateAgainstFullRecompute` returns true (`0/0/0/0`).
  These pass **today** (slow == full recompute) and must keep passing once the incremental path lands
  — i.e. they are written first as regression guards (TDD).
- **Unit:** pure helpers (`selectAffectedFills`, `clearFills` index remap) via the existing
  `gap-seam-test` harness pattern.
- Each integration case also asserts the **expected path was taken** (slow, not full fallback) once a
  lightweight counter/return-flag is added, so the suite proves the incremental code actually ran.
