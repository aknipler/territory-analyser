#pragma once

// testing
#include "grid.h"

#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <cstddef>
#include <array>

using Position = std::pair<size_t, size_t>;

class Fill {
private:
    std::vector<Position> cells_;
    std::vector<std::vector<Position>> gapGroups_;
    std::set<Position> dilationCells_;
    std::vector<std::set<Position>> connectedGapSets_;
    std::unordered_map<size_t, size_t> playerCellCount_;
    std::unordered_map<size_t, double> playerWeightCount_;
    std::unordered_map<size_t, size_t> playerObstructionCount_; // how many bordering obstruction cells each player owns
    size_t closureOwner_ = 0; // player who rigourously encloses this fill (0 = none)
    bool walkableTerrain_;
    std::array<int, 4> bounds_{}; // minRow, maxRow, minCol, maxCol

public:
    Fill(int rows, int cols) : walkableTerrain_(true), bounds_{rows, 0, cols, 0} {}

    void addCell(size_t row, size_t col);
    void registerCell(size_t row, size_t col, double cellValue);
    void addGapGroup(const std::vector<Position>& group);
    void addDilationCell(size_t row, size_t col);
    void removeDilationCell(size_t row, size_t col);
    void addConnectedGapSet(const std::set<Position>& connectedGapSet);
    void countCellAttributes(size_t playerID, double weight);
    void recordAdjacentObstruction(size_t playerID);
    void setClosureOwner(size_t playerID);
    size_t getClosureOwner() const;
    void merge(const Fill& other);
    void removeInternalGapGroups(const std::vector<std::vector<int>>& cellsToFill,
                                 const std::set<int>& mergedFillIndices);
    void setWalkableBool(bool walkable);
    bool sameWalkableTerrain(bool cellTerrain) const;

    size_t getDominantPlayer() const;
    const std::vector<Position>& getCells() const;
    const std::vector<std::vector<Position>>& getGapGroups() const;
    const std::set<Position>& getDilationCells() const;
    const std::vector<std::set<Position>>& getConnectedGapSets() const;
    const std::unordered_map<size_t, size_t>& getPlayerCount() const;
    const std::array<int, 4>& getBounds() const;
    size_t numGapGroups() const;
};

struct FillResult {
    std::vector<Fill> fills;
    std::vector<std::vector<Position>> gaps;
    std::vector<std::vector<size_t>> fillBoard;
};

FillResult analyseFill(
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard = nullptr,
    const std::vector<bool>* defeatedPlayers = nullptr
);

