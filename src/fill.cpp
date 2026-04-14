#include "fill.h"

#include <queue>
#include <stack>
#include <algorithm>
#include <numeric>
#include <unordered_map>

#define TESTING true

constexpr int CLOSED_SHAPE_GAPS_THRESHOLD = 2; // global threshold for max gap groups in a Fill to be considered valid

// ---------- Fill methods ----------

void Fill::addCell(size_t row, size_t col) {
    cells_.emplace_back(row, col);
}

void Fill::registerCell(size_t row, size_t col, double cellValue) {
    addCell(row, col);
    size_t val = static_cast<size_t>(cellValue);
    for (size_t p = 1; val > 0; p++, val >>= 1) {
        if (val & 1) {
            countCellAttributes(p, cellValue);
        }
    }
}

void Fill::addGapGroup(const std::vector<Position>& group) {
    gapGroups_.push_back(group);
}

void Fill::addDilationCell(size_t row, size_t col) {
    dilationCells_.emplace(row, col);
}

void Fill::countCellAttributes(size_t playerID, double weight) {
    playerCellCount_[playerID]++;
    playerWeightCount_[playerID] += weight;
}

void Fill::merge(const Fill& other) {
    cells_.insert(cells_.end(), other.cells_.begin(), other.cells_.end());
    gapGroups_.insert(gapGroups_.end(), other.gapGroups_.begin(), other.gapGroups_.end());
    dilationCells_.insert(other.dilationCells_.begin(), other.dilationCells_.end());
    for (const auto& kv : other.playerCellCount_) {
        playerCellCount_[kv.first] += kv.second;
    }
}

void Fill::removeInternalGapGroups(const std::vector<std::vector<int>>& cellsToFill,
                                   const std::set<int>& mergedFillIndices) {
    gapGroups_.erase(
        std::remove_if(gapGroups_.begin(), gapGroups_.end(),
            [&](const std::vector<Position>& gg) {
                return std::all_of(gg.begin(), gg.end(),
                    [&](const Position& gp) {
                        int other = cellsToFill[gp.first][gp.second];
                        return other >= 0 && mergedFillIndices.count(other);
                    });
            }),
        gapGroups_.end());
}

size_t Fill::getDominantPlayer() const {

    size_t maxCount = 0, playerWithMax = 0, totalPlayerInvestment = 0;
    double ownershipThreshold = 0.05, contestedOwnershipThreshold = 0.6; // e.g., 60%

    // Find player with max count
    //  might be better to use plLocalTerritories to calculate each players investment
    for (const auto& kv : playerCellCount_) {
        totalPlayerInvestment += kv.second;
        if (kv.second > maxCount) {
            maxCount = kv.second;
            playerWithMax = kv.first;
            if (TESTING) std::cout << "Player " << playerWithMax << " has max count: " << maxCount << "vs:" << cells_.size() << std::endl;
        }
    }

    // Check if maxCount is enough to claim ownership
    if (static_cast<double>(maxCount) / cells_.size() > ownershipThreshold &&
     static_cast<double>(maxCount) / totalPlayerInvestment > contestedOwnershipThreshold) {
        return playerWithMax;
    } else {
        return 0; // not enough to claim ownership
    }

}

const std::vector<Position>& Fill::getCells() const { return cells_; }
const std::vector<std::vector<Position>>& Fill::getGapGroups() const { return gapGroups_; }
const std::set<Position>& Fill::getDilationCells() const { return dilationCells_; }
const std::unordered_map<size_t, size_t>& Fill::getPlayerCount() const { return playerCellCount_; }
size_t Fill::numGapGroups() const { return gapGroups_.size(); }

// ---------- Union-Find (internal) ----------

namespace {

class UnionFind {
    std::vector<int> parent_, rank_;
public:
    explicit UnionFind(int n) : parent_(n), rank_(n, 0) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }
    int find(int x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];
            x = parent_[x];
        }
        return x;
    }
    void unite(int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank_[a] < rank_[b]) std::swap(a, b);
        parent_[b] = a;
        if (rank_[a] == rank_[b]) rank_[a]++;
    }
};

static constexpr int dx8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
static constexpr int dy8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
static constexpr int dx4[] = {-1, 0, 1, 0};
static constexpr int dy4[] = {0, 1, 0, -1};

} // anonymous namespace

// ---------- analyseFill ----------

FillResult analyseFill(
    const std::vector<std::vector<double>>& plLocalTerritories, 
    const std::vector<std::vector<bool>>& obstructionsBoard,
    size_t numPlayers)
{
    FillResult result;
    if (plLocalTerritories.empty()) return result;

    const int rows = static_cast<int>(plLocalTerritories.size());
    const int cols = static_cast<int>(plLocalTerritories[0].size());

    // Step 0 – Initialization
    std::vector<std::vector<bool>> dilatedWalls(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<int>>  cellsToFill(rows, std::vector<int>(cols, -1));
    std::unordered_map<size_t, double> playerWeightCount;
    for (size_t p = 1; p <= numPlayers; p++) {
        playerWeightCount[p] = 0.0;
    }

    // Step 1 – Dilate walls: mark 8-directional neighbors of each wall cell
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (!obstructionsBoard[i][j]) { 
                continue; 
            } else {
                for (int d = 0; d < 8; d++) {
                    int ni = i + dx8[d], nj = j + dy8[d];
                    if (ni >= 0 && ni < rows && nj >= 0 && nj < cols && !obstructionsBoard[ni][nj])
                        dilatedWalls[ni][nj] = true;
                }
            }
        }
    }


    // Step 2 – BFS flood fill to create Fill regions
    std::vector<Fill> fills;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (obstructionsBoard[i][j] || dilatedWalls[i][j] || visited[i][j]) {
                continue;
            }

            Fill fill;
            int fillIdx = static_cast<int>(fills.size());
            std::queue<Position> q;
            q.push({static_cast<size_t>(i), static_cast<size_t>(j)});
            visited[i][j] = true;

            while (!q.empty()) {
                auto cur = q.front();
                q.pop();
                size_t r = cur.first, c = cur.second;

                fill.registerCell(r, c, plLocalTerritories[r][c]);
                cellsToFill[r][c] = fillIdx;

                // Check 4-neighbors
                for (int d = 0; d < 4; d++) {
                    int nr = static_cast<int>(r) + dx4[d];
                    int nc = static_cast<int>(c) + dy4[d];

                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) { 
                        continue;
                    }

                    // Record adjacent dilation cells (excluding original obstructions)
                    if (dilatedWalls[nr][nc] && !obstructionsBoard[nr][nc])
                        fill.addDilationCell(static_cast<size_t>(nr), static_cast<size_t>(nc));

                    // Expand BFS into non-obstruction, non-dilation, unvisited cells
                    if (!obstructionsBoard[nr][nc] && !dilatedWalls[nr][nc] && !visited[nr][nc]) {
                        visited[nr][nc] = true;
                        q.push({static_cast<size_t>(nr), static_cast<size_t>(nc)});
                    }
                }
            }

            // Step 3 – Gap detection: explore dilation boundaries of this Fill
            std::set<Position> dilVisited;
            std::vector<Position> dilToConvert;
            std::vector<std::vector<Position>> gapGroupsToAdd;

            for (const auto& dilPos : fill.getDilationCells()) {
                if (dilVisited.count(dilPos)) {
                    continue;
                }

                std::vector<Position> componentDilCells;
                std::vector<Position> gapCells;
                std::set<Position> gapSeen;
                std::stack<Position> stk;
                stk.push(dilPos);
                dilVisited.insert(dilPos);

                while (!stk.empty()) {
                    auto top = stk.top();
                    stk.pop();
                    componentDilCells.push_back(top);

                    int dr = static_cast<int>(top.first);
                    int dc = static_cast<int>(top.second);

                    for (int d = 0; d < 4; d++) {
                        int ndr = dr + dx4[d];
                        int ndc = dc + dy4[d];
                        if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) continue;

                        Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};

                        // Continue through connected dilation cells
                        if (dilatedWalls[ndr][ndc] && !obstructionsBoard[ndr][ndc]
                            && !dilVisited.count(np)) {
                            dilVisited.insert(np);
                            stk.push(np);
                        }
                        // Gap cell: not wall, not dilation, not part of this Fill
                        else if (!obstructionsBoard[ndr][ndc] && !dilatedWalls[ndr][ndc]
                                 && cellsToFill[ndr][ndc] != fillIdx
                                 && !gapSeen.count(np)) {
                            gapSeen.insert(np);
                            gapCells.push_back(np);
                        }
                    }
                }

                if (gapCells.empty()) {
                    // Dead-end branch: convert all dilation cells to fill cells
                    dilToConvert.insert(dilToConvert.end(),
                        componentDilCells.begin(), componentDilCells.end());
                } else {
                    // Gap found: convert dilation cells that are adjacent to fill cells
                    std::set<Position> gapSet(gapCells.begin(), gapCells.end());
                    for (const auto& dCell : componentDilCells) {
                        bool adjacentToFill = false;
                        for (int d = 0; d < 4; d++) {
                            int ndr = static_cast<int>(dCell.first) + dx4[d];
                            int ndc = static_cast<int>(dCell.second) + dy4[d];
                            if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) continue;
                            std::cout << "Checking gap adjacency for dilation cell (" << dCell.first << "," << dCell.second << ") against gap cell (" << ndr << "," << ndc << ") with fillIdx " << cellsToFill[ndr][ndc] << std::endl; // For testing
                            printBoard(cellsToFill); // For testing
                            if (cellsToFill[ndr][ndc] == fillIdx)
                                adjacentToFill = true;
                        }
                        if (adjacentToFill) {
                            dilToConvert.push_back(dCell);
                        }
                    }
                    gapGroupsToAdd.push_back(std::move(gapCells));
                }
            }

            // Apply conversions: dilation cells → fill cells
            for (const auto& pos : dilToConvert) {
                fill.registerCell(pos.first, pos.second, plLocalTerritories[pos.first][pos.second]);
                cellsToFill[pos.first][pos.second] = fillIdx;
                dilatedWalls[pos.first][pos.second] = false;
            }

            // Add gap groups
            for (auto& gg : gapGroupsToAdd) {
                fill.addGapGroup(gg);
            }

            fills.push_back(std::move(fill));
        }
    }

    // Step 4 – Merge Fills that share gap groups with the same dominant player
    {
        int n = static_cast<int>(fills.size());
        UnionFind uf(n);

        for (int fi = 0; fi < n; fi++) {
            for (const auto& gg : fills[fi].getGapGroups()) {
                for (const auto& gp : gg) {
                    int other = cellsToFill[gp.first][gp.second];
                    if (other >= 0 && other != fi
                        && fills[fi].getDominantPlayer() != 0
                        && fills[fi].getDominantPlayer() == fills[other].getDominantPlayer()) {
                        uf.unite(fi, other);
                    }
                }
            }
        }

        // Remove gap groups that are now internal to merged groups
        std::unordered_map<int, std::set<int>> rootMembers;
        for (int fi = 0; fi < n; fi++) {
            rootMembers[uf.find(fi)].insert(fi);
        }
        for (int fi = 0; fi < n; fi++) {
            int root = uf.find(fi);
            if (rootMembers[root].size() > 1) {
                fills[fi].removeInternalGapGroups(cellsToFill, rootMembers[root]);
            }
        }

        // Rebuild fills by union-find groups
        std::unordered_map<int, int> rootToIdx;
        std::vector<Fill> merged;
        for (int fi = 0; fi < n; fi++) {
            int root = uf.find(fi);
            auto it = rootToIdx.find(root);
            if (it != rootToIdx.end()) {
                merged[it->second].merge(fills[fi]);
            } else {
                rootToIdx[root] = static_cast<int>(merged.size());
                merged.push_back(std::move(fills[fi]));
            }
        }
        fills = std::move(merged);
    }

    // Step 5 – Gap constraint: discard fills with more than 2 gap groups
    {
        std::vector<Fill> filtered;
        filtered.reserve(fills.size());
        for (auto& f : fills) {
            if (f.numGapGroups() <= CLOSED_SHAPE_GAPS_THRESHOLD) {
                filtered.push_back(std::move(f));
            }
        }
        fills = std::move(filtered);
    }

    // Step 6 – Collect all remaining gap groups
    std::vector<std::vector<Position>> allGaps;
    for (const auto& f : fills) {
        for (const auto& gg : f.getGapGroups())
            allGaps.push_back(gg);
    }

    // Step 7 - Build fill board: encode dominant player per cell (powers-of-2)
    result.fillBoard.assign(rows, std::vector<size_t>(cols, 0));
    for (size_t fi = 0; fi < fills.size(); fi++) {

        size_t dp = fills[fi].getDominantPlayer();

        if (dp == 0) { 
            continue; 
        }

        size_t encoded_player_value = static_cast<size_t>(1) << (dp - 1);

        for (const auto& cell : fills[fi].getCells())
            result.fillBoard[cell.first][cell.second] = encoded_player_value;
    }

    
    if (TESTING) {
        
        std::cout << "Obstructions + Dilations Board:" << std::endl;
        // merge the obstructions and dilations together for testing
        std::vector<std::vector<int>> obsDilBoard(rows, std::vector<int>(cols, 0));
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                obsDilBoard[i][j] = obstructionsBoard[i][j] + dilatedWalls[i][j];
            }
        }
        printBoard(obsDilBoard); // For testing
        std::cout << std::endl;
        
        std::cout << "Fill Board:" << std::endl;
        printBoard(result.fillBoard); // For testing
        std::cout << std::endl;

        std::cout << "Player Local Territories Board:" << std::endl;
        printBoard(plLocalTerritories); // For testing
        std::cout << std::endl;
    }


    result.fills = std::move(fills);
    result.gaps = std::move(allGaps);
    return result;
}


