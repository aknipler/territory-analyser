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
    size_t closureOwner_ = 0; // player who rigourously encloses this fill (0 = none)
    bool walkableTerrain_;
    std::array<int, 4> bounds_{}; // minX, maxX, minY, maxY
    std::unordered_set<std::pair<int, int>, SignedPairHash> mapEdgeCells_; // cells touching map boundary (may have negative coords for outside boundary)
    std::unordered_map<size_t, std::set<std::pair<int, int>>> obstructionNeighbours_;


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
    void setClosureOwner(size_t playerID);
    size_t getClosureOwner() const;

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
};

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
    const std::vector<size_t>& dsuGroupBounds,     // {minX, maxX, minY, maxY} of the touching building cluster
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard = nullptr,
    const std::vector<bool>* defeatedPlayers = nullptr
);

