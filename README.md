# Territory Analyser

A real-time territory analysis library for RTS games and board games, designed with Age of Empires 2 in mind but intentionally game-agnostic.

## Setup

1. Install OpenCV 4.x. Follow the instructions at https://docs.opencv.org/4.x/df/d65/tutorial_table_of_content_introduction.html

2. Build `obstructionsDict.json`. See **How It Works → Obstructions Dictionary** below.

3. Provide a walkable-terrain board (`examples/AAAImageWater.txt`). This is a grid of `0`/`1` characters marking non-walkable (water) and walkable cells. Without it the analyser defaults to all-walkable.

4. Edit `settings.json` to match your configuration.

5. Build with CMake (see **Build** below) and run.


## Build

The project uses CMake and requires GCC/G++ 17+.

```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel
./Territory-Analyser
```

### Common Setup Issues

**GCC version too old** — on Linux: `sudo apt update && sudo apt upgrade gcc g++`, then `gcc -v`. If still wrong, check for symlink issues across multiple installed versions.

**VS Code users**
- In `C/C++: Edit Configurations (JSON)` add `"/usr/local/include/opencv4"` to `includePath`.
- Set `cppstandard` to `c++17` in Preferences → Settings.
- Add to `.vscode/tasks.json` (cppbuild args):
```json
"-std=c++17",
"${workspaceFolder}/*.cpp",
"-I/usr/local/include/opencv4",
"-L/usr/local/lib",
"-lopencv_imgcodecs",
"-lopencv_core",
"-lpthread",
"-lopencv_highgui",
"-lopencv_imgproc"
```
- Use CMakeBuild (bottom-left of VS Code, not the top-right run button).


## Usage

### Typical Workflow

```cpp
// 1. Initialise
TerritoryAnalyser analyser(mapSize, numPlayers, numTeams, teamAssignments, threshold, "state.txt");

// 2. Add/remove obstructions as the game state changes
analyser.updateObstruction(x, y, "Castle", player, "add");
analyser.updateObstruction(x, y, "Castle", player, "remove");

// 3. Trigger a render update
analyser.updateRender("player");  // or "team"

// 4. Get the output mat
cv::Mat result = analyser.getFinalTerritoryMap("player");
```

Repeat steps 2–4 as game state evolves. For the `"flash"` contested-territory method, call `updateContestedFlash()` every frame.

**Initial state file format** — one command per line:
```
analyser.updateObstruction(x, y, "House", 1, "add");
```
Lines starting with `#` are treated as comments.


## How It Works

### Raw Territory

Each obstruction contributes influence to the cells around it, using attributes from `obstructionsDict.json` (radius, weight, soft-edge, width, height). Influence is additive: placing two obstructions near each other grows their combined territory.

The master board uses power-of-two bitmask encoding per player so multi-owner (contested) cells are detectable in a single integer comparison.

**Soft edges** — if two obstructions are just outside each other's hard radii, their soft-edge influence still overlaps, giving a small territory bonus for proximity.

### Contested Territory

Three methods, set in `settings.json → contestedTerritoryMethod`:
- `"growth"` — each contested cell is awarded to the owner with the best distance-weighted score.
- `"flash"` — contested cells pulse in alpha (call `updateContestedFlash()` every frame).
- `"staticColour"` — contested cells are set to a fixed colour.

### Fill

Fill uses obstructions to detect closed shapes and assign interior territory. Pipeline:

1. **Wall map** — merge all per-player obstruction boards (including Gaia at key 0) into one boolean wall grid.
2. **Dilation** — dilate walls and map edges by one cell so nearly-closed shapes are treated as closed.
3. **Flood fill** — flood over all open (non-wall, non-dilated) cells to find candidate fill regions.
4. **Gap detection** — for each fill, find gap cells (undilated openings in the surrounding wall ring) and group them into connected gap sets.
5. **Closure analysis** (`ClosureResult`) — for each fill, determine the set of players whose obstructions geometrically enclose it. A fill is "enclosed" by player P if P's obstructions (via the DSU connected-obstruction graph) form a closed ring around the fill region.
6. **Dominant player** — among the enclosing players, the dominant is the one with the highest adjusted count: every encloser's raw territory-cell count inside the fill is boosted by `isWalledMultiplier`. The winner must also exceed `ownershipThreshold` of the un-boosted total.
7. **Gap threshold** — fills with more than `CLOSED_SHAPE_GAPS_THRESHOLD` *external* gap cells (gaps connecting to a different fill's territory or unclaimed space) have their closure cleared. They keep their cells and group membership but contribute no walled boost to dominant-player calculation. **Fills are never erased** — this keeps the fill-assignment board consistent across incremental updates.
8. **Output** — a fill board (per-cell dominant-player encoding), per-player gap-cell sets, and fill metadata.

### Incremental Updates (`updateObstruction`)

After each obstruction add/remove the analyser takes one of three paths:

- **Fast path (no topology change)** — the obstruction footprint has no neighbours within Chebyshev distance 2. Only footprint cells are evicted from or registered into the fill board; no reflood.
- **Fast path (dominant flip)** — same isolation criterion but the obstruction's influence shifts the dominant player of one or more fills. A global tail runs: `mergeFills → removeShapesByGapThreshold → encodeFillsAndGapsForOutput` over existing fills, no reflood. This is idempotent because fills are never erased.
- **Slow path (topology change)** — a neighbour exists within Chebyshev-2. Falls back to a full `initialiseFill` recompute (faithful baseline).

### DSU (Connected Obstructions)

A union-find structure tracks which obstructions are directly adjacent (footprint gap ≤ 0). Each connected component stores: member set, bounding box, `closed` flag (cycle detected), and `closedWithMapEdge` flag (two separate map-boundary contact points). These flags drive the closure analysis in step 5 above.

### Colour Pass

Layers rendered in priority order (lowest overrides highest):
1. Fill territory (interior of closed/nearly-closed shapes)
2. Raw territory (influence from obstructions)
3. Territory edges
4. Neutral / Gaia objects (always on top)

### Incremental Update Oracle

Set `validateIncrementalFills: true` in `settings.json` to enable a correctness oracle. After each incremental `updateObstruction`, the oracle runs a fresh `initialiseFill` and diffs the result against the incremental result on four invariants: fill-board ownership, per-player gap-cell sets, fill partition (which cells belong to which fill), and per-fill properties (dominant player, closure, gap cells). A `[VALIDATE label] OK — 0/0/0/0` line confirms the incremental path is faithful. This flag carries a significant performance cost; leave it off in production.


## Obstructions Dictionary (`obstructionsDict.json`)

All obstruction types must be defined here. Attributes:

| Field | Meaning |
|---|---|
| `radius` | Influence radius (cells) |
| `weight` | Influence strength inside radius |
| `softEdge` | Number of extra cells beyond radius where influence tapers to zero |
| `softEdgeDivFactor` | Taper start value = `ownershipThreshold / softEdgeDivFactor` |
| `width` | Footprint width (cells) |
| `height` | Footprint height (cells) |

The file also contains a `rangedObstructions` list. Ranged obstructions use Euclidean (radial) distance for influence; non-ranged obstructions use an 8-neighbour Dijkstra from the footprint, respecting walls and non-walkable terrain.

**Tips for building your own dictionary**
- Ranged obstructions (towers, castles, etc.): relate radius to the object's attack range.
- Military-production obstructions (barracks, stables): use a moderate fixed radius.
- Economic/civilian obstructions (houses, walls): small radius, low weight.
- Exceptions (docks, harbours): mark as `nonWalkableTerrainObstructions` in `settings.json` so their Dijkstra influence propagates over water.
