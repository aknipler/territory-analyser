# HANDOFF — AoE2 DE screenshot → territory-analyser test case extraction

## Goal
Convert the screenshot `1000029911.png` (2559x1439, AoE2 DE with a visual mod that renders
buildings as solid player-coloured isometric diamond plates with white glyphs) into test data
for https://github.com/aknipler/territory-analyser (cloned at `/home/claude/territory-analyser`
in the original session; re-clone if absent).

### Deliverables
1. A text file in the style of `examples/example1.txt`: **one line per object**:
   `analyser.updateBuilding(x,y,"BuildingName",player,team,"add");`
   covering all player buildings, palisade walls, and gaia objects (trees, gold, stone, cliffs).
2. A separate **168x168 0/1 grid dump** of water tiles (for nonWalkableTerrain automation).
3. A **glyph contact sheet** image of clustered unknown glyphs for the user to label
   (only 8 glyphs are confirmed so far, see legend below).

## User-confirmed conventions (do not re-ask)
- Game: Age of Empires 2 DE. Use standard AoE2 DE building names; extending buildingsDict is fine.
- Glyph legend so far: axe=Lumber Camp, crossed swords=Barracks, house glyph=House,
  graduation cap=University, hammer+anvil=Blacksmith, catapult=Siege Workshop,
  weighing scales=Market, pickaxe=Mining Camp. Everything else → contact sheet for user.
- Coordinates: map diamond LEFT corner = (0,0); (N-1,0) = BOTTOM screen corner; +y toward TOP corner.
- Code ground truth (src/territory_analyser.cpp `updateObstruction`): footprint = rows [x, x+bh),
  cols [y, y+bw). Under the anchors above, a building's origin tile = the **screen-LEFT corner of
  its diamond plate** (user *believed* it'd be the screen-top corner — flag this nuance in the final
  report; it is irrelevant for square footprints).
- Players: p1=blue (NW base), p2=red (NE), p3=green (SW), p4=yellow/olive (SE). 2v2:
  p1&p3 = team 1, p2&p4 = team 2. Gaia = player 0, team 0.
- Palisade walls only (no stone walls on this map); gates = wall segments (1 wall line per tile).
- Ignore: farms, fish, ALL units.
- Trees: one `updateBuilding(...,"Tree",0,0,"add")` per forest-hex tile. Gold Mine / Stone Mine
  per pile tile. Cliffs as "Cliff" per tile.
- Aim for EXACT tile placement (hence all the elevation machinery below).

## Critical discovery — MAP SIZE
The user said 220x220 but the screenshot is **168x168 (Medium)**. Evidence: plate widths are
exactly k*14.97px for kxk footprints; isolated 1x1 wall plates ≈ 14px; lattice phasor fit gives
tile width 14.966px and 2510px map span / 14.966 = 167.7 ≈ 168. **Already flagged to the user**
in chat (they haven't responded to it yet); produce output at 168 and mention it again in the
final summary.

## Geometry (all calibrated, trust these numbers)
Main map diamond corners: top (1281,70), bottom (1281,1313), left (26,697), right (2536,697).
Lattice (r = x = row toward bottom corner, c = y = col toward top corner; tile-corner units):
```
sx = BX + AX*(r+c)          AX = 7.4832  (phasor fit, concentration 0.81)
sy = BY + AY*(r-c) - shift  AY = AX/2 = 3.7416
```
- x-phase: phx ≈ 1.54 (from `solve_phase(sxT, AX)` over marker top corners), anchor n0 = 3,
  i.e. `S = round((sxT - phx)/AX) - n0` gives S = r+c with left corner = 0. Verify S ∈ [0,336].
- BY0 = 697.5 used as the vertical reference: `d_float = (syT - BY0)/AY`.
- **Marker top corner** (use it — the two top edges have no 3D rim): for a kxk plate at origin
  (x0,y0): top corner = tile corner (x0, y0+k). So S_top = x0+y0+k, d_top = x0-y0-k,
  x0 = (S+d)/2, y0 = (S-d)/2 - k. Parity: d ≡ S (mod 2) always.
- Corner fitting: per component use u = sx+2*sy, v = sx-2*sy; 1st/99th percentiles;
  top corner = ((u_min+v_max)/2, (u_min-v_max)/4). Accuracy: x std 0.08 tiles, y std 0.12.
- Plates are drawn with ~1.5px inset per side (so a kxk plate measures ≈ k*14.97 - 3 px wide);
  footprint k = round(width/(2*AX) + 0.2).

## Elevation problem & solution (the heart of this pipeline)
Terrain elevation shifts everything UP-screen: corrupts d=(r-c), never s=(r+c). Observed elevation
offsets: even modes 0/+2/+4/+6 in d-units plus ~18 odd-parity markers. Resolution method
(implemented & verified in `solve_final.py`):
1. Per marker, enumerate candidate d values with correct parity within a window below/above d_float.
2. Score each candidate by player-colour presence on the **minimap** (elevation-free, top-down):
   sample each footprint tile centre; score = fraction of tiles hitting the player's minimap mask.
3. Ties (dense bases saturate the minimap) → choose candidate whose elevation proxy (d - d_float)
   is closest to the median of nearby confident markers.
4. Greedy non-overlap pass on a 168x168 occupancy grid; conflicts take their next-best candidate.
Results on the 302 clean single plates: mean minimap score 0.928, 7 markers <0.5, 1 unresolved
overlap, 25 conflict-resolved, 236 tie-smoothed. Verified visually: `check_minimap.png`.

## Minimap calibration
Region rows 1125:1434, cols 1941:2559 of the full image. Transform (tile-corner units):
```
mm_sx = MBX + M_AX*(r+c)    MBX = 1942.0   M_AX = 1.8363
mm_sy = MBY + M_AY*(r-c)    MBY = 1283.0   M_AY = 0.9167
```
(MBX/MBY grid-searched to maximise total marker match.) Minimap player colours (BGR, tol<90):
p1 (241,42,27)+(169,48,36); p2 (28,28,236)+(1,1,183); p3 (18,221,15)+(0,165,0);
p4 (12,229,229)+(0,202,202). Restrict masks to the minimap region (they leak onto the main map
otherwise — that bug was hit once already).

## Main-map colours & masking gotchas
Player plate colours (BGR, tol<70): p1 (204,0,0), p2 (0,0,204), p3 (16,132,3), p4 (24,172,172).
- **GOLD CONTAMINATION**: gold piles ((15,172,210),(30,223,246),(34,144,190)) sit within tol 70 of
  the p4 olive. Fix in `pipeline2.player_mask`: require d_p4 < d_gold - 5 for every gold colour.
- Glyphs = white pixels (min channel > 170) inside plates; fill holes before component analysis.
- Marker components are found per colour with `fill_holes` + connectedComponents; min area 25-40.

## Object taxonomy discovered in the screenshot
- **Buildings**: solid kxk plates (k=1..4) with a white glyph. k=1 plates with tiny glyphs exist —
  almost certainly Outposts (glyph looks like a "T"/tower).
- **Palisade walls**: plain 1x1 plates, no glyph, form chains that merge into single components
  (decompose via correlation with a 1x1 diamond kernel, peak ≥0.70, diamond-shaped suppression
  radius 0.85 tile — rectangular suppression kills true neighbours, already fixed).
- **Foundations (under construction)**: dotted diamond OUTLINE in player colour + a nearby small
  flag plate bearing the player number as a Roman numeral (II, IV observed). At least: one red 4x4
  foundation at bbox (1453,754,61,30); several olive 2x2/3x3; possibly blue thin dotted chains =
  **unbuilt palisade-wall foundation dots**. ASK THE USER how to treat foundations (include as the
  building? skip?) — add an example crop to the contact sheet.
- **Letter glyphs**: some plates carry letters ("M", "W", "T", numerals). "M" plates sit amid farms →
  likely Mill. Don't guess silently — put them on the contact sheet.
- A small plate sits ON a pond (bottom-right area) — possibly a Dock or fish-related marker; check.
- Gaia (NOT YET EXTRACTED): trees = dark hexes (interior ≈ (26,26,26), merge-prone; same elevation
  machinery applies — minimap forests are dark green); gold = bright yellow piles; stone = grey
  piles; cliffs = grey/brown rocky lines (minimap shows brown dots).
- Water: blue ponds; minimap light-blue can cross-check. Output = 168x168 0/1 grid. Water lies flat
  per pond → solve each pond's d-offset by minimap shape matching, then take exact tiles from the
  main-map shape.

## File inventory (`/home/claude/ta_work/`, bundled alongside this handoff)
Scripts (python3, opencv/numpy/scipy available):
- `extract.py` — core module: `load()` (image + map mask), `fill_holes`, `marker_components`,
  `diamond_fit` (u/v percentile corner fit), `solve_phase`. Constants IMG_PATH, N=168, AX,
  PLAYER_COLORS. **Import this everywhere.**
- `solve_final.py` — WORKING marker solver (minimap scoring, tie smoothing, non-overlap, and
  `render_check` → `check_minimap.png`). Operates on `markers_fit.json` + `good_mask.npy`.
- `pipeline2.py` — CURRENT unified classifier (`classify()`): per-component routing into
  building / wall / foundation / wall_dot, glyph clustering with dedupe, merged-chain
  decomposition. Output `plates2.json`.
- `pipeline.py`, `decompose.py`, `solve.py` — superseded iterations; keep for reference only.

Data:
- `markers_fit.json` (315 fitted components: p, bbox, area, corners, width, kf, k),
  `good_mask.npy` (302 clean single plates), `marker_solution.json` (302 SOLVED tile positions:
  i,p,k,S,base,cands,d,x0,y0,score,margin,nties,smoothed/resolved flags — this is good data),
  `plates2.json` (latest classify() output), `plates.json` (v1), `mapmask.npy`, `mm_calib.npy`,
  `glyph_areas.npy`, `comps.json`, `markers_raw.json`, tree_pts/tree_areas (early test, redo).
- `lab_p*.npy` NOT bundled (60MB, regenerable via `marker_components`).
- Diagnostics: `check_minimap.png` (solved footprints over minimap — the main verification view),
  `bad_*.png`, `suspect_walls.png`, `suspect_foundations.png`, `crop_*.png`, `ng4_*.png`.

## Latest pipeline2 counts & KNOWN ISSUES (fix first)
Counts: buildings p1/p2/p3/p4 = 25/89/85/85; walls = 102/33/7/5; k histogram:
2x2=183, 3x3=62, 4x4=36, 1x1 walls=147, 1x1 buildings=3.
1. **Foundation regression in pipeline2**: the ring test (`farea > 2.2*raw`) never fires because
   `fill_holes` runs on the un-dilated comp — dotted rings don't close, so foundations fall through
   to the merged branch and shed spurious "walls" (p2's 33 walls are suspect). Fix: close/dilate the
   comp before filling (then un-dilate), or test ring-ness on the dilated component.
2. p2/p3/p4 "walls" need auditing — montage earlier showed mis-decomposed big plates and outposts;
   re-montage after fix.
3. 1 unresolved overlap + 7 low-score (<0.5) markers in `marker_solution.json` — inspect manually.
4. `solve_final.py` consumes `markers_fit.json`; it must be adapted to consume `plates2.json`
   (top-corner convention: entries with `from_center=True` already converted sy by -TILE*k/4; the
   wall/building entries from decomposition give centers — recheck that conversion: center→top is
   sy_top = csy - AY*k... currently uses TILE*k/4 = AX*k/2 = 2*AY*k/2 = AY*k ✓ verify by render).

## Remaining TODO (in order)
1. Fix pipeline2 foundation ring test; re-audit walls; merge `marker_solution.json`-style solving
   over ALL plates2 entries (one unified solve + non-overlap + render check).
2. Glyph extraction & clustering from all solved building plates → labelled contact sheet
   (cluster by normalized binary glyph crops, e.g. resize to 24x24, agglomerative on IoU).
   Include: the known-8 (auto-label), letters, the pond plate, a foundation example + numeral flag.
   **Send to user for labelling; ask about foundations & the map-size discrepancy.**
3. Gaia extraction: trees (dark hexes via correlation like walls; elevation via minimap dark-green
   forest mask + neighbour smoothing), gold piles, stone piles, cliffs (per-tile "Cliff").
4. Water: per-pond shape → minimap d-anchor → 168x168 grid dump.
5. Final assembly: AoE2 footprint table (TC 4x4, Castle 4x4, House/Mill/camps 2x2, most military
   3x3, Outpost 1x1, Monastery 3x3, Market 3x3, University 3x3, Siege Workshop 3x3/4x4 — verify
   against measured k!), emit `updateBuilding` lines (players then gaia), water grid file,
   plus a summary of caveats (168 vs 220; origin-corner nuance; foundations decision).
   Present via present_files.

## Verification habits that caught real bugs
- Render solved footprints onto the minimap (`render_check`) after every solve change.
- Montage any new detection class before trusting counts (`suspect_*.png` pattern).
- Check per-player counts for sanity (p1 was 29 before wall decomposition vs 88-93 for others).
