# Handover: the "medium path" for updateFill (DROPPED — may revisit)

**Status (2026-06-22):** design only, **no code exists** in `updateFill()`. We are dropping it and
proceeding with **fast + slow paths only**, held to **full structural faithfulness** (the oracle
`validateAgainstFullRecompute` must report `fillBoard / gap-cell / partition / fill-prop` all = 0).
This document is the single source for picking the medium path back up later.

---

## 1. Concept — the 3-tier model

Today `updateFill` has two paths, dispatched by `hasNearbyBuildings` (Chebyshev-2):
- **fast** — isolated footprint, topology can't change → local attribute/membership update.
- **slow** — anything else → region reprocess (currently buggy; being rebuilt).

The medium path was a *third* tier sitting between them, for **add/remove that creates a gap but
does NOT close/open a shape** (topology unchanged, only gap bookkeeping needed):

| | Fast | Medium | Slow |
|---|---|---|---|
| **Add** | no obstruction within Cheb-2 → just update territory attrs | creates a gap near a building but doesn't **close** a shape → traverse/record the gap | **closes** a shape → split a fill into ≥2 → reflood |
| **Remove** | as above | creates a gap but doesn't **open** a shape → traverse/record the gap | **opens** a shape so ≥2 fills meet → merge fills |

Key asymmetry: closing **splits** (needs new fills → reflood); opening **merges** (union existing
fills, cheaper than reflood).

The medium path's value: avoid the full reflood for the common "extend a wall / poke a hole that
doesn't change enclosure" case, which currently falls into the expensive (and buggy) slow path.

---

## 2. Remove-gap algorithm (the design we landed on)

On remove, find which freed/nearby cells become **gap cells**, then record each into every fill it
borders.

**Find the fills touching the building — reverse-lookup (preferred over a ring scan):** query each
fill's stored `obstructionNeighbours_` for the removed building's footprint cells. Those fills
listed the building as a bordering obstruction, so this is exact and already accounts for the
dilation gap between fill and wall. (A geometric ring scan must be **Chebyshev-2**, not 1 — fill
cells sit one dilation cell past the footprint, so a Cheb-1 scan mostly hits dilation `-1`, which is
why the current fast-path `surroundingFillIdx` scan is flaky.)

**Rules for a gap-cell candidate (per touching fill):**
1. 4-neighbour of one of that fill's cells.
2. 8-neighbour of an obstruction.
3. Chebyshev-chain (8-neighbour) reaches **another** obstruction within **max chain length 2** (or
   it directly is a second obstruction) → confirms it's a sealed opening between two walls, not open
   space. Chain length 2 = the maximum sealed-opening width.

**One fill vs two fills touching:**
- One fill → may create a gap or not; straightforward local analysis.
- Two fills → **gap or merge**. Merge iff the two fills become **4-connected through un-dilated,
  non-obstruction cells** after removal; otherwise it's a gap.

---

## 3. Add-gap case

Symmetric to remove: an add that lengthens a wall / creates a notch but doesn't **close** a shape.
"Record the inside-gap even if it's currently internal, in case this `connectedObstructions` soon
closes a shape" — see §4. Detection of "doesn't close" relies on the DSU closure flags (§7).

Fast-vs-medium early-out (so wall-thickening doesn't pay gap-traversal cost): **windowed local
flood** — reflood a small window (footprint + dilation + ~2 cells) before/after, compare walkable
connected-component count; unchanged → route to fast. Cheaper pre-filter: **dilation-pocket scan**
(any walkable cell newly pinched on ≥3 sides / a 1-cell channel).

---

## 4. Option (b): incremental split-on-close (the prize)

Slow flood does **not** compute new gaps — it floods and uses pre-found gaps only as flood-stoppers.
So new gaps from a wall being built aren't caught. The medium path would **accumulate gap/boundary
info per `connectedObstructions` group** as walls go up, so that when the group finally **closes**, we
do NOT full-reflood — instead:
1. Seed a flood from a cell on the **interior** side of the closing wall (the accumulated boundary
   holds inside-adjacent cells), **bounded by the wall+dilation** → exactly the interior fill.
2. Outside fill = original fill with interior cells **delta-evicted** (`evictCellsInFootprint`-style).
3. Recompute gaps/closure locally for both halves.

Rigorous because the interior flood is bounded by the real wall, so it assigns exactly the cells a
from-scratch `initialiseFill` would. Hinges on **reliable close-detection** (§7) and a correct
interior seed (nested/multiple pockets need one seed per pocket).

---

## 5. Representation requirement (do not get this wrong)

Gap cells = **bordering dilation cells** recorded in `gapGroups_`, with `fillBoard = 0`. They must
**NOT** be added to `cells_` / become owned. The current slow-path bug is precisely that it promotes
these to owned fill cells (oracle: incremental `fillBoard=8` vs fresh `0`). Match what
`initialiseFill` produces. NOTE: the strengthened oracle checks the **fills vector partition** too,
so to truly pass we must reproduce `initialiseFill`'s structure **including bridge fills**
(`fillConnectedGapSets`) — the "gapGroup-only, no bridge" shortcut passes only the *weak* oracle.

---

## 6. Known shortcomings / open issues (with oracle evidence)

- **Gap cells extend BEYOND the footprint.** For removed house `17,42` (footprint `17–18,42–43`),
  the real gap cells were `(17,40),(18,39),(21,41)` — interior-side and ~3 tiles along the wall. A
  footprint-seeded, footprint-bounded search (design rule 1.1) finds none of them. The search must
  cover footprint + dilation radius + along-wall.
- **Freed cells partition three ways** {newly-claimed fill, new dilation, gap}, not just "gap". The
  algorithm only finds gap cells; something must also reclaim freed→fill and re-derive local dilation.
- **Medium/slow discrimination needs reliable close/open detection** incl. **water** closure. DSU
  gives building-cycle + map-edge closure today, NOT terrain. Prereq: island-border graph (see §7).
- Must produce the **same fill structure** as `initialiseFill` (partition + bridge fills) — see §5.

---

## 7. Dependency: water/terrain closure (island-border graph) — deferred

Compute a **connected-component labelling of non-walkable cells** ONCE in `initWalkableTerrain()`
(terrain is static), e.g. `waterComponentBoard` (cell→component id, walkable=-1). Then **unify with
the existing map-edge machinery**: map exterior = one boundary component, each lake = another.
Generalize `mapEdgeBuildingIds`→`boundaryTouches` and `checkMapEdgeClosure`→`checkBoundaryClosure`
(closed if the wall anchors ≥2 distinct boundary touches, or forms a building cycle). This is the
gating prerequisite for medium/slow discrimination AND option (b).

---

## 8. Efficiency

Avoids the full ~30ms reflood; bounded to the footprint's neighbourhood + the touching fills' local
gap analysis. Reverse-lookup is O(touching-fills' obstructionNeighbours_). The early-out keeps
wall-thickening at O(window). Option (b)'s close handling is one bounded interior flood, not a
board-wide reflood.

---

## 9. How to verify

`validateAgainstFullRecompute` (config `validateIncrementalFills`) is the oracle — it diffs every
incremental result against a from-scratch `initialiseFill` on `fillBoard`, gap-cell sets, fill
partition, and per-fill props. "Done" = all four zero on the medium path's cases. It already pins
the slow-remove gap bug to exact cells; reuse it the same way for the medium path.

---

## 10. Pick-up checklist

1. Build the island-border graph (§7) first — close-detection gates everything.
2. Add the fast-vs-medium early-out (§3).
3. Implement remove-gap (§2) + add-gap (§3), emitting gapGroups + bridge fills (§5).
4. Implement option (b) split-on-close (§4) if you want to avoid reflood on closure.
5. Hold it all to the oracle (§9), full structural faithfulness.
