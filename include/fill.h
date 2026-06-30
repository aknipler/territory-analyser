#pragma once

// testing
#include "grid.h"

#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <cstddef>
#include <array>

#include <queue>
#include <stack>
#include <algorithm>
#include <numeric>

using Position = std::pair<size_t, size_t>;

// Hash function for Position (pair<size_t, size_t>)
struct PositionHash {
    size_t operator()(const Position& p) const noexcept {
        size_t h1 = std::hash<size_t>{}(p.first);
        size_t h2 = std::hash<size_t>{}(p.second);
        return h1 ^ (h2 * 2654435761ULL);  // Knuth multiplicative hash for better distribution
    }
};

// Hash function for signed int pairs (used for map-edge and boundary cells)
struct SignedPairHash {
    size_t operator()(const std::pair<int,int>& p) const noexcept {
        size_t h1 = std::hash<int>{}(p.first);
        size_t h2 = std::hash<int>{}(p.second);
        return h1 ^ (h2 * 2654435761ULL);
    }
};

// Outcome of a fill's rigorous closure analysis: the set of players whose walls
// (plus free walls — map edges and terrain boundaries) geometrically enclose the
// fill. Closure is PURELY GEOMETRIC — no ownership gate — so it cannot be
// self-reinforced by dominance and does not change when a fill's owner changes.
// getDominantPlayer() boosts every encloser, so the dominant-among-enclosers wins
// (and a player who walled themselves in can overcome an enemy bleeding influence
// into the ring). An empty set means no player encloses the fill; a multi-element
// set is fine (e.g. nested walls) — boosting all of them lets dominance decide.
struct ClosureResult {
    std::set<size_t> enclosers;

    bool encloses(size_t playerID) const { return enclosers.count(playerID) > 0; }
    bool empty() const { return enclosers.empty(); }
};

// A connected component of fill cells that are not 4-adjacent to any dilation wall
// (i.e. more than one tile removed from any obstruction). Used to accelerate
// incremental flood fills: once any boundaryCells cell is reached during BFS, the
// whole group can be absorbed immediately and BFS only needs to continue from the
// boundary cells rather than exploring every interior cell individually.
struct InteriorGroup {
    std::vector<Position> cells;          // all cells in this interior connected component
    std::vector<Position> boundaryCells;  // subset: cells 4-adjacent to a dilation wall
};

class Fill {
private:
    int indx;
    std::vector<Position> cells_;
    std::vector<std::vector<Position>> gapGroups_;
    std::unordered_set<Position, PositionHash> dilationCells_;
    std::vector<std::set<Position>> connectedGapSets_;
    std::unordered_map<size_t, size_t> playerCellCount_;
    std::unordered_map<size_t, double> playerWeightCount_;
    std::vector<double> preUpdateCellValues_; // territory value per cell at last registration/update
    ClosureResult closure_; // result of the rigorous enclosure analysis for this fill
    bool walkableTerrain_;
    std::array<int, 4> bounds_{}; // minX, maxX, minY, maxY
    std::unordered_set<std::pair<int, int>, SignedPairHash> mapEdgeCells_; // cells touching map boundary (may have negative coords for outside boundary)
    std::unordered_set<Position, PositionHash> terrainBoundaryCells_; // in-bounds cells of the *opposite* terrain that border this fill (free walls, like map edges)
    std::unordered_map<size_t, std::set<std::pair<int, int>>> obstructionNeighbours_;
    int groupId_ = -1; // index of the merge-group this fill belongs to; fills sharing a groupId are treated as one region without being destructively merged
    std::vector<Position> boundaryCells_; // fill cells 4-adjacent to a dilation wall, recorded during floodFill (the group's boundary)
    int externalGapGroupCount_ = -1; // cached count of EXTERNAL gap groups (gaps not into a same-owner fill); -1 = unset -> gapReductionFactor falls back to all gapGroups_

    /** @brief Gap interpolation weight in [0,1]: 1.0 = no external gaps, decays as external gaps
     *         increase. Uses the cached external-gap count (setExternalGapGroupCount); feeds
     *         walledBoostFactor(). */
    double gapReductionFactor() const;

    /** @brief Effective encloser multiplier used in getDominantPlayer: 1 + (isWalledMultiplier - 1)
     *         * gapReductionFactor(), in [1, isWalledMultiplier]. Single source of truth for the
     *         walled boost, shared by getDominantPlayer and closureCouldMatter. */
    double walledBoostFactor() const;


public:
    Fill(int width, int height) : walkableTerrain_(true), bounds_{width, 0, height, 0} {}

    void addCell(size_t x, size_t y);
    /** @brief Adds cell and increments per-player attribute counts from its bitmask value. */
    void registerCell(size_t x, size_t y, double cellValue);
    void addGapGroup(const std::vector<Position>& group);
    void addDilationCell(size_t x, size_t y);
    void removeDilationCell(size_t x, size_t y);
    void addConnectedGapSet(const std::set<Position>& connectedGapSet);
    void removeConnectedGapSet(const std::set<Position>& connectedSet);
    void addFillObstructionNeighbour(size_t playerID, int x, int y);
    void countCellAttributes(size_t playerID, double weight);
    void setClosure(const ClosureResult& closure);
    const ClosureResult& getClosure() const;
    void addTerrainBoundaryCell(size_t x, size_t y);
    const std::unordered_set<Position, PositionHash>& getTerrainBoundaryCells() const;
    void setGroupId(int groupId);
    int getGroupId() const;

    /** @brief Sets the boundary cells (fill cells 4-adjacent to a dilation wall), recorded for
     *         free during floodFill. The group's cells are cells_; its boundary is these. No
     *         separate re-search/subdivision — one interior group per fill. */
    void setBoundaryCells(std::vector<Position>&& boundary);
    const std::vector<Position>& getBoundaryCells() const;

    /** @brief Appends all cell data from other (cells, gaps, dilation, bounds, etc.) and resolves closure owner. */
    void merge(const Fill& other);

    /** @brief Drops gap groups whose cells all belong to fills in mergedFillIndices. */
    void removeInternalGapGroups(const std::vector<std::vector<int>>& cellsToFill,
                                 const std::set<int>& mergedFillIndices);
    void setWalkableBool(bool walkable);
    bool sameWalkableTerrain(bool cellTerrain) const;
    void reserveCells(size_t n);
    
    /** @brief Promotes a batch of dilation cells to fill cells (updates bounds, attributes, and tracking arrays). */
    void batchConvertDilationToFill(
        const std::vector<Position>& positions,
        const std::vector<std::vector<double>>& plLocalTerritories,
        std::vector<std::vector<int>>& cellsToFill,
        std::vector<std::vector<bool>>& dilatedWalls,
        std::vector<std::vector<bool>>& visited,
        int fillIdx);
    /** @brief Bulk-initialises fill after BFS: moves cell list into cells_, updates bounds/attributes,
     *         and flushes dilation and map-edge cell buffers. */
    void batchRegisterCellsAndMapEdgeCells(
        bool hasDefeatedPlayers,
        std::vector<Position>&& cellsToRegister,
        const std::vector<std::vector<double>>& plLocalTerritories,
        const std::vector<std::pair<int, int>>& mapEdgeCellsToAdd,
        const std::vector<Position>& dilationCellsToAdd,
        const std::vector<bool>* defeatedPlayers,
        std::vector<std::vector<int>>& cellsToFill,
        int fillIdx);
    /** @brief Removes footprint cells, delta-updates counts for influence-area cells using the cached
     *         pre-update values, and leaves outside-influence cells' counts untouched. */
    void evictCellsInFootprint(size_t x, size_t y, size_t w, size_t h,
                                size_t influenceExtent,
                                const std::vector<std::vector<double>>& plLocalTerritories);


    /** @brief Returns the player that owns this fill based on threshold checks, or 0 if none qualifies. */
    size_t getDominantPlayer() const;

    /** @brief Cheap soundness guard: true iff computing this fill's closure could change
     *         getDominantPlayer()'s result (so checkClosureOwner is worth running). Returns false
     *         only when the owner is provably identical with or without closure. */
    bool closureCouldMatter() const;

    /** @brief Player with the most raw cells in this fill (0 if none). Boost-independent basis for
     *         classifying internal vs external gaps. */
    size_t rawLeadingPlayer() const;

    /** @brief Caches the count of EXTERNAL gap groups (computed with cross-fill context in
     *         mergeFills) so gapReductionFactor() penalises only real openings, not internal seams. */
    void setExternalGapGroupCount(size_t count);
    const std::vector<Position>& getCells() const;
    const std::vector<std::vector<Position>>& getGapGroups() const;
    const std::unordered_set<Position, PositionHash>& getDilationCells() const;
    const std::unordered_map<size_t, std::set<std::pair<int, int>>>& getFillObstructionNeighbours() const;
    const std::vector<std::set<Position>>& getConnectedGapSets() const;
    const std::unordered_map<size_t, size_t>& getPlayerCount() const;
    const std::array<int, 4>& getBounds() const;
    size_t numGapGroups() const;
    void addMapEdgeCell(int x, int y);
    const std::unordered_set<std::pair<int, int>, SignedPairHash>& getMapEdgeCells() const;
};

struct FillResult {
    std::vector<Fill> fills;
    std::vector<std::vector<Position>> gaps;
    std::vector<std::vector<size_t>> fillBoard;
    std::vector<std::vector<int>> fillAssignmentBoard; // cell → fill index (-1 = unclaimed/obstruction)
    // One self-contained interior group per fill (cells + boundary). Self-contained (owns its cells)
    // so it can persist in TerritoryAnalyser independently of the transient per-update fills.
    std::vector<InteriorGroup> interiorGroups;
};

/**
 * @brief Counts the EXTERNAL gap groups of a single fill for the gap-threshold closure gate. A gap
 *        group is INTERNAL (a same-owner seam, not counted) iff myDominant is non-zero and every one
 *        of its cells resolves via cellsToFill to a fill whose dominant player equals myDominant;
 *        otherwise the gap group is external. Per-fill and keyed on the dominant PLAYER, so a
 *        connected same-owner region may be partially owned. dominantByFill holds each fill's owner
 *        (0 = none). Exposed for unit testing (see tests/test_gap_seam.cpp).
 */
size_t countExternalGapGroups(const Fill& fill,
                              size_t myDominant,
                              const std::vector<std::vector<int>>& cellsToFill,
                              const std::vector<size_t>& dominantByFill);

FillResult initialiseFill(
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<int>>& masterPlayerObstructionBoard,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard = nullptr,
    const std::vector<bool>* defeatedPlayers = nullptr
);

FillResult updateFill(
    size_t footprintX, size_t footprintY, size_t footprintW, size_t footprintH,
    size_t influenceExtent,
    bool isFastPath,
    const std::string& mod,
    std::vector<Fill> fills,
    std::vector<std::vector<int>> fillAssignmentBoard,
    std::vector<std::vector<size_t>> fillBoard,
    std::vector<std::vector<Position>> gaps,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<int>>& masterObstructionBoard,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<size_t>& dsuGroupBounds,     // {minX, maxX, minY, maxY} of the touching obstruction cluster
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard = nullptr,
    const std::vector<bool>* defeatedPlayers = nullptr,
    const std::string& stepLabel = ""              // debug: tag (e.g. "player"/"team") for updateFillSteps dumps
);

/**
 * @brief Debug consistency net. Re-runs initialiseFill() from scratch on the SAME post-update
 *        inputs and diffs the order-independent invariants — per-cell fillBoard ownership and the
 *        per-player gap-cell SETS — against the given incremental result. Logs every divergence
 *        with coordinates. Returns true if consistent.
 *
 *        This is the ground-truth oracle for the incremental fast/slow paths: anything they get
 *        wrong (e.g. a missing gap after a removal) shows up here as a mismatch vs. the full
 *        recompute. Gated by config.validateIncrementalFills; it does a full recompute so it
 *        roughly doubles the cost of each incremental op — testing only.
 */
bool validateAgainstFullRecompute(
    const FillResult& incremental,
    const std::string& label,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<int>>& masterObstructionBoard,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard = nullptr,
    const std::vector<bool>* defeatedPlayers = nullptr
);

