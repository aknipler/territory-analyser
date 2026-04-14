#pragma once

// testing
#include "grid.h"

#include <vector>
#include <set>
#include <unordered_map>
#include <utility>
#include <cstddef>

using Position = std::pair<size_t, size_t>;

class Fill {
private:
    std::vector<Position> cells_;
    std::vector<std::vector<Position>> gapGroups_;
    std::set<Position> dilationCells_;
    std::unordered_map<size_t, size_t> playerCellCount_;
    std::unordered_map<size_t, double> playerWeightCount_;

public:
    Fill() = default;

    void addCell(size_t row, size_t col);
    void registerCell(size_t row, size_t col, double cellValue);
    void addGapGroup(const std::vector<Position>& group);
    void addDilationCell(size_t row, size_t col);
    void countCellAttributes(size_t playerID, double weight);
    void merge(const Fill& other);
    void removeInternalGapGroups(const std::vector<std::vector<int>>& cellsToFill,
                                 const std::set<int>& mergedFillIndices);

    size_t getDominantPlayer() const;
    const std::vector<Position>& getCells() const;
    const std::vector<std::vector<Position>>& getGapGroups() const;
    const std::set<Position>& getDilationCells() const;
    const std::unordered_map<size_t, size_t>& getPlayerCount() const;
    size_t numGapGroups() const;
};

struct FillResult {
    std::vector<Fill> fills;
    std::vector<std::vector<Position>> gaps;
    std::vector<std::vector<size_t>> fillBoard;
};

FillResult analyseFill(
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    size_t numPlayers
);

