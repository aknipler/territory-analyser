#include "fill.h"
#include "header.h"

#include <queue>
#include <stack>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>



// ---------- Fill methods ----------

void Fill::addCell(size_t row, size_t col) {
    cells_.emplace_back(row, col);
    const int r = static_cast<int>(row);
    const int c = static_cast<int>(col);
    if (r < bounds_[0]) bounds_[0] = r;
    if (r > bounds_[1]) bounds_[1] = r;
    if (c < bounds_[2]) bounds_[2] = c;
    if (c > bounds_[3]) bounds_[3] = c;
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

void Fill::removeDilationCell(size_t row, size_t col) {
    dilationCells_.erase({row, col});
}

void Fill::addConnectedGapSet(const std::set<Position>& connectedGapSet) {
    if (!connectedGapSet.empty()) {
        connectedGapSets_.push_back(connectedGapSet);
    }
}

void Fill::countCellAttributes(size_t playerID, double weight) {
    playerCellCount_[playerID]++;
    playerWeightCount_[playerID] += weight;
}

void Fill::recordAdjacentObstruction(size_t playerID) {
    playerObstructionCount_[playerID]++;
}

void Fill::setClosureOwner(size_t playerID) {
    closureOwner_ = playerID;
}

size_t Fill::getClosureOwner() const {
    return closureOwner_;
}

void Fill::merge(const Fill& other) {
    cells_.insert(cells_.end(), other.cells_.begin(), other.cells_.end());
    gapGroups_.insert(gapGroups_.end(), other.gapGroups_.begin(), other.gapGroups_.end());
    dilationCells_.insert(other.dilationCells_.begin(), other.dilationCells_.end());
    connectedGapSets_.insert(connectedGapSets_.end(), other.connectedGapSets_.begin(), other.connectedGapSets_.end());
    bounds_[0] = std::min(bounds_[0], other.bounds_[0]);
    bounds_[1] = std::max(bounds_[1], other.bounds_[1]);
    bounds_[2] = std::min(bounds_[2], other.bounds_[2]);
    bounds_[3] = std::max(bounds_[3], other.bounds_[3]);
    for (const auto& kv : other.playerCellCount_) {
        playerCellCount_[kv.first] += kv.second;
    }
    for (const auto& kv : other.playerObstructionCount_) {
        playerObstructionCount_[kv.first] += kv.second;
    }
    // Merge closure owner: keep if both agree, set to contested if differ
    if (closureOwner_ == 0) {
        closureOwner_ = other.closureOwner_;
    } else if (other.closureOwner_ != 0 && other.closureOwner_ != closureOwner_) {
        closureOwner_ = -1; // contested
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

void Fill::setWalkableBool(bool walkable) {
    walkableTerrain_ = walkable;
}

bool Fill::sameWalkableTerrain(bool cellTerrain) const {
    // compare cell terrain to fill terrain attribute
    return (walkableTerrain_ == cellTerrain);
}

size_t Fill::getDominantPlayer() const {
    const Config& config = AppConfig::get();

    size_t maxCount = 0, playerWithMax = 0, totalAdjustedInvestment = 0;
    const double ownershipThreshold = config.ownershipThreshold;
    const double contestedOwnershipThreshold = config.contestedOwnershipThreshold;
    const double isWalledMultiplier = config.isWalledMultiplier;

    size_t soleObstructionOwner = closureOwner_;

    // Find player with max count
    for (const auto& kv : playerCellCount_) {
        totalAdjustedInvestment += kv.second;
        if (kv.second > maxCount) {
            maxCount = kv.second;
            playerWithMax = kv.first;
            if (config.testingMode) {
                if (soleObstructionOwner != 0 && soleObstructionOwner >= 0 && soleObstructionOwner < 10) {
                    std::cout << "Player " << soleObstructionOwner << " rigourously encloses this fill (x" << isWalledMultiplier << " boost applied)" << std::endl;
                    std::cout << "Player " << playerWithMax << " has max adjusted count: " << maxCount << " vs cells: " << cells_.size() << std::endl;
                }
            }
        }
    }

    // if (config.testingMode)

    // Check if maxCount is enough to claim ownership,  (x2 for sole obstruction owner - this means a player has walled themselves in)
    if (static_cast<double>(maxCount) * (soleObstructionOwner != 0 && playerWithMax == soleObstructionOwner ? isWalledMultiplier : 1) / cells_.size() > ownershipThreshold &&
        static_cast<double>(maxCount) / totalAdjustedInvestment > contestedOwnershipThreshold) {
        return playerWithMax;
    } else {
        return 0; // not enough to claim ownership
    }

}

const std::vector<Position>& Fill::getCells() const { return cells_; }
const std::vector<std::vector<Position>>& Fill::getGapGroups() const { return gapGroups_; }
const std::set<Position>& Fill::getDilationCells() const { return dilationCells_; }
const std::vector<std::set<Position>>& Fill::getConnectedGapSets() const { return connectedGapSets_; }
const std::unordered_map<size_t, size_t>& Fill::getPlayerCount() const { return playerCellCount_; }
const std::array<int, 4>& Fill::getBounds() const { return bounds_; }
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

struct PositionHash {
    size_t operator()(const Position& p) const {
        const size_t h1 = std::hash<size_t>{}(p.first);
        const size_t h2 = std::hash<size_t>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

bool obstructionOwnedByPlayer(
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    int playerID,
    int row,
    int col)
{
    if (playerID < 0) return false;
    const size_t index = static_cast<size_t>(playerID);
    if (index >= playerObstructionBoards.size()) return false;
    return playerObstructionBoards[index][row][col];
}

bool touchesAnyNonObstructionNeighbor(
    int row,
    int col,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    int rows,
    int cols)
{
    for (int d = 0; d < 4; d++) {
        int nr = row + dx4[d];
        int nc = col + dy4[d];
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        if (!obstructionsBoard[nr][nc]) return true;
    }
    return false;
}

bool findPlayerObstructionThroughBranch(
    const Position& start,
    int playerID,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    int rows,
    int cols)
{
    // DFS through obstruction cells not owned by the target player.
    // Branch rule: if a visited cell touches open space, do not expand past it.
    std::stack<Position> stk;
    std::set<Position> visited;
    stk.push(start);
    visited.insert(start);

    while (!stk.empty()) {
        Position cur = stk.top();
        stk.pop();

        int r = static_cast<int>(cur.first);
        int c = static_cast<int>(cur.second);

        if (obstructionOwnedByPlayer(playerObstructionBoards, playerID, r, c)) {
            return true;
        }

        if (touchesAnyNonObstructionNeighbor(r, c, obstructionsBoard, rows, cols)) {
            continue;
        }

        for (int d = 0; d < 4; d++) {
            int nr = r + dx4[d];
            int nc = c + dy4[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            if (!obstructionsBoard[nr][nc]) continue;

            if (obstructionOwnedByPlayer(playerObstructionBoards, playerID, nr, nc)) {
                return true;
            }

            Position np{static_cast<size_t>(nr), static_cast<size_t>(nc)};
            if (!visited.count(np)) {
                visited.insert(np);
                stk.push(np);
            }
        }
    }

    return false;
}

} // anonymous namespace

// Returns the sole player whose per-player boundary (their obstruction cells adjacent to this
// fill + all gap cells neighboring this fill) forms an unbroken chain — either a cycle, or a
// path anchored to >= 2 distinct map edges.  Returns 0 if none or if contested.
static size_t checkClosureOwner(
    const Fill& fill,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    int rows, int cols, size_t numPlayers)
{
    const std::array<int, 4>& fillBounds = fill.getBounds();

    auto borderIndex = [&](int r, int c) -> int {
        if (rows == 1 && cols == 1) {
            return 0;
        }
        if (rows == 1) {
            return c;
        }
        if (cols == 1) {
            return r;
        }

        if (r == 0) {
            return c;
        }
        if (c == cols - 1) {
            return (cols - 1) + r;
        }
        if (r == rows - 1) {
            return (cols - 1) + (rows - 1) + (cols - 1 - c);
        }
        // c == 0
        return (cols - 1) + (rows - 1) + (cols - 1) + (rows - 1 - r);
    };

    const int perimeterLength =
        (rows == 1 && cols == 1) ? 1
        : (rows == 1) ? cols
        : (cols == 1) ? rows
        : (2 * (rows + cols) - 4);

    auto countDistinctBorderTouchRuns = [&](std::vector<int> touches) -> int {
        if (touches.empty()) {
            return 0;
        }

        std::sort(touches.begin(), touches.end());
        touches.erase(std::unique(touches.begin(), touches.end()), touches.end());

        constexpr int gapMergeTolerance = 0;
        int runs = 1;
        for (size_t i = 1; i < touches.size(); ++i) {
            if (touches[i] - touches[i - 1] > gapMergeTolerance + 1) {
                runs++;
            }
        }

        // Perimeter is cyclic for 2D boards; merge first/last runs if contiguous across seam.
        if (rows > 1 && cols > 1 && runs > 1) {
            int seamGap = touches.front() + perimeterLength - touches.back();
            if (seamGap <= gapMergeTolerance + 1) {
                runs--;
            }
        }

        return runs;
    };
    Config config = AppConfig::get();

    // Collect gap cells from all gap groups of this fill
    std::set<Position> gapSet;
    for (const auto& gg : fill.getGapGroups()) {
        for (const auto& gp : gg) {
            gapSet.insert(gp);
        }
    }

    size_t enclosedBy = 0;

    for (size_t pid = 1; pid <= numPlayers; ++pid) {
        if (pid >= playerObstructionBoards.size()) {
            continue;
        }
        const auto& pBoard = playerObstructionBoards[pid];

        // Build candidate boundary: player-p obstruction cells 4-adjacent to any fill cell
        // or unconverted dilation cell, + all gap cells.
        std::unordered_set<Position, PositionHash> boundary;

        auto checkNeighboursForBoundary = [&](int r, int c) {
            // AK: I wonder if this needs to be changed to be 8-neighbour in case of diagonal issues
            for (int d = 0; d < 4; d++) {
                int nr = r + dx4[d];
                int nc = c + dy4[d];
                if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                // if its an obstruction cell owned by this player or gaia, or if it's a walkability mismatch with the fill's terrain, it's part of the boundary
                if (pBoard[nr][nc] || playerObstructionBoards[0][nr][nc] || (WalkableTerrainBoard != nullptr && !fill.sameWalkableTerrain((*WalkableTerrainBoard)[nr][nc]))) {
                    boundary.insert({static_cast<size_t>(nr), static_cast<size_t>(nc)});
                    // if(config.testingMode == true) {
                    //     std::cout << "Boundary cell added (" << nr << "," << nc << ") player " << pid << std::endl;
                    // }
                }
            }
        };

        for (const auto& cell : fill.getCells()) {
            int r = static_cast<int>(cell.first);
            int c = static_cast<int>(cell.second);
            // if(config.testingMode == true) {
            //     std::cout << "Checking neighbors of fill cell (" << r << "," << c << ") for player " << pid << std::endl;
            // }
            checkNeighboursForBoundary(r, c);
        }

        // Also check connected-gap cells: these are the non-converted dilation cells
        // that remain connected to each specific gap component.
        for (const auto& gapConnectedSet : fill.getConnectedGapSets()) {
            for (const auto& gapConnectedCell : gapConnectedSet) {
                boundary.insert(gapConnectedCell);
                int r = static_cast<int>(gapConnectedCell.first);
                int c = static_cast<int>(gapConnectedCell.second);
                // if(config.testingMode == true) {
                //     std::cout << "Checking neighbors of connected gap cell (" << r << "," << c << ") for player " << pid << std::endl;
                // }
                checkNeighboursForBoundary(r, c);
            }
        }
        for (const auto& gp : gapSet) {
            // AK: am worried that this could end up grabbing obstructions outside the region of interest
            // Also, is this already fulfilled by gapConnectedSets?
            boundary.insert(gp);
            if(config.testingMode == true && false) {
                std::cout << "Gap cell (" << gp.first << "," << gp.second << ") added to boundary for player " << pid << std::endl;
            }

            // Gap cells can be the only cells that expose an owned obstruction or
            // terrain boundary to the candidate closure chain.
            checkNeighboursForBoundary(static_cast<int>(gp.first), static_cast<int>(gp.second));
        }

        if (boundary.empty()) continue;

        // BFS to find connected components; for each check closure condition
        std::unordered_set<Position, PositionHash> visited;
        bool playerCloses = false;

        for (const auto& start : boundary) {
            if (visited.count(start)) continue;

            std::vector<Position> component;
            std::queue<Position> bfsq;
            bfsq.push(start);
            visited.insert(start);
            if(config.testingMode == true && false) {
                std::cout << "First cell (" << start.first << "," << start.second << ") player " << pid << std::endl;
            }

            std::vector<int> borderTouches;
            const int estimatedBorderTouches = std::max(12, std::min(perimeterLength/4, 64));
            borderTouches.reserve(static_cast<size_t>(estimatedBorderTouches));
            std::array<int, 4> componentBounds = {rows, 0, cols, 0};

            while (!bfsq.empty()) {
                auto cur = bfsq.front(); bfsq.pop();
                component.push_back(cur);
                int r = static_cast<int>(cur.first);
                int c = static_cast<int>(cur.second);
                if (r < componentBounds[0]) componentBounds[0] = r;
                if (r > componentBounds[1]) componentBounds[1] = r;
                if (c < componentBounds[2]) componentBounds[2] = c;
                if (c > componentBounds[3]) componentBounds[3] = c;

                if (r == 0 || r == rows - 1 || c == 0 || c == cols - 1) {
                    borderTouches.push_back(borderIndex(r, c));
                }
                
                // if(config.testingMode == true) {
                //     std::cout << "Visited cell (" << r << "," << c << ") player " << pid << ". Border touches size " << borderTouches.size()  << std::endl;
                // }

                // this part needs to be 8-directional
                for (int d = 0; d < 8; d++) {
                    int nr = r + dx8[d];
                    int nc = c + dy8[d];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    Position np{static_cast<size_t>(nr), static_cast<size_t>(nc)};
                    if (boundary.count(np) && !visited.count(np)) {
                        visited.insert(np);
                        bfsq.push(np);
                        
                    }
                }
            }

            // Count undirected edges in the same 8-neighbor graph used for connectivity.
            int edgeCount = 0;
            for (const auto& cell : component) {
                const int r = static_cast<int>(cell.first);
                const int c = static_cast<int>(cell.second);

                for (int d = 0; d < 8; d++) {
                    int nr = r + dx8[d];
                    int nc = c + dy8[d];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;

                    Position np{static_cast<size_t>(nr), static_cast<size_t>(nc)};
                    if (!boundary.count(np)) continue;

                    // Count each undirected edge once via lexicographic ordering.
                    if (nr > r || (nr == r && nc > c)) {
                        edgeCount++;
                    }
                }
            }

            // Closure: cycle (E >= V) or chain anchored to 2+ distinct map edges
            bool hasCycle       = false; // AK Need to implement box checks now. This used to be (edgeCount >= static_cast<int>(component.size()));
            int distinctEdgeTouchLocations = countDistinctBorderTouchRuns(std::move(borderTouches));
            bool touchesTwoEdges = (distinctEdgeTouchLocations >= 2);

            if(config.testingMode == true && false) {
                std::cout << "*********** edge touches:" << distinctEdgeTouchLocations << std::endl;
                std::cout << "############## enclosure bounds: "
                          << componentBounds[0] << "," << componentBounds[1] << ","
                          << componentBounds[2] << "," << componentBounds[3] << std::endl;
                std::cout << "############## FILL bounds: "
                          << fillBounds[0] << "," << fillBounds[1] << ","
                          << fillBounds[2] << "," << fillBounds[3] << std::endl;
            }

            if (hasCycle || touchesTwoEdges) {
                const bool fillInsideComponentBounds =
                    fillBounds[0] >= componentBounds[0]
                    && fillBounds[1] <= componentBounds[1]
                    && fillBounds[2] >= componentBounds[2]
                    && fillBounds[3] <= componentBounds[3];

                if (fillInsideComponentBounds) {
                    playerCloses = true;
                }
            }
        }

        if (playerCloses) {
            if(config.testingMode == true) {
                std::cout << "*&*&*&*&*&*&*& prev enclosed by " << enclosedBy << "New pid: " << pid << std::endl;
            }
            if (enclosedBy == 0) {
                enclosedBy = pid;
            } else {
                return -1; // contested
            }
        }
    }

    return enclosedBy;
}

// ---------- analyseFill ----------

FillResult analyseFill(
    const std::vector<std::vector<double>>& plLocalTerritories, 
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<bool>* defeatedPlayers)
{
    const Config& config = AppConfig::get();

    FillResult result;
    if (plLocalTerritories.empty()) return result;

    const int rows = static_cast<int>(plLocalTerritories.size());
    const int cols = static_cast<int>(plLocalTerritories[0].size());


    // Build a local ownership board for safe bounds-checked ownership queries.
    std::vector<std::vector<std::vector<bool>>> localObstructionBoards = playerObstructionBoards;
    if (localObstructionBoards.empty()) {
        localObstructionBoards.resize(1, std::vector<std::vector<bool>>(rows, std::vector<bool>(cols, false)));
    }

    // Ensure all ownership layers match board dimensions.
    for (auto& board : localObstructionBoards) {
        if (static_cast<int>(board.size()) != rows) {
            board.resize(rows, std::vector<bool>(cols, false));
        }
        for (auto& row : board) {
            if (static_cast<int>(row.size()) != cols) {
                row.resize(cols, false);
            }
        }
    }

    // Merge all obstruction layers into one board for fill analysis.
    std::vector<std::vector<bool>> obstructionsBoard(rows, std::vector<bool>(cols, false));
    for (const auto& board : localObstructionBoards) {
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                obstructionsBoard[i][j] = obstructionsBoard[i][j] || board[i][j];
            }
        }
    }

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

    // Step 1b – Treat map edges as implicit walls: mark border cells as dilation
    //           walls so fills don't bleed to the boundary, unless an obstruction
    //           is already there.
    // We only add it the map edge dilation if there is a dilated wall within 1 distance
    for (int i = 0; i < rows; i++) {
        if (!obstructionsBoard[i][0] && dilatedWalls[i][1])         dilatedWalls[i][0]         = true;
        if (!obstructionsBoard[i][cols - 1] && dilatedWalls[i][cols - 2])  dilatedWalls[i][cols - 1]  = true;
    }
    for (int j = 0; j < cols; j++) {
        if (!obstructionsBoard[0][j] && dilatedWalls[1][j])         dilatedWalls[0][j]         = true;
        if (!obstructionsBoard[rows - 1][j] && dilatedWalls[rows - 2][j])  dilatedWalls[rows - 1][j]  = true;
    }

    // Step 1c - Mark terrain boundary cells as dilation walls on both sides of the
    //           water/land boundary. Uses 8-neighbor comparison so both the water cell
    //           and the adjacent land cell are captured (unlike findEdges which encodes
    //           the terrain value and leaves land-side boundary cells at 0).
    if (WalkableTerrainBoard != nullptr) {
        const auto& nwtb = *WalkableTerrainBoard;
        const int terrainRows = std::min(rows, static_cast<int>(nwtb.size()));
        for (int i = 0; i < terrainRows; i++) {
            const int terrainCols = std::min(cols, static_cast<int>(nwtb[i].size()));
            for (int j = 0; j < terrainCols; j++) {
                if (obstructionsBoard[i][j]) continue;
                bool onBoundary = false;
                for (int d = 0; d < 8; d++) {
                    int ni = i + dx8[d], nj = j + dy8[d];
                    if (ni < 0 || ni >= terrainRows || nj < 0 || nj >= terrainCols) continue;
                    if (nwtb[ni][nj] != nwtb[i][j]) { onBoundary = true; break; }
                }
                if (onBoundary) dilatedWalls[i][j] = true;
            }
        }
    }

    if(config.testingMode) {
        std::cout << "Obstructions Board:" << std::endl;
        printBoard(obstructionsBoard); // For config.testingMode
        std::cout << std::endl;

        std::cout << "Dilated Walls Board:" << std::endl;
        printBoard(dilatedWalls); // For config.testingMode
        std::cout << std::endl;
    }

    // Step 2 – BFS flood fill to create Fill regions
    std::vector<Fill> fills;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (obstructionsBoard[i][j] || dilatedWalls[i][j] || visited[i][j]) {
                continue;
            }

            Fill fill(rows, cols);
            int fillIdx = static_cast<int>(fills.size());
            std::queue<Position> q;
            q.push({static_cast<size_t>(i), static_cast<size_t>(j)});
            visited[i][j] = true;
            if (WalkableTerrainBoard != nullptr) {
                fill.setWalkableBool(static_cast<bool>((*WalkableTerrainBoard)[i][j]));
            } else {
                fill.setWalkableBool(true); // default to walkable if no terrain board provided
            }

            while (!q.empty()) {
                auto cur = q.front();
                q.pop();
                size_t r = cur.first, c = cur.second;

                if (defeatedPlayers != nullptr) {
                    // Register cell, skipping attribution for defeated players.
                    fill.addCell(r, c);
                    const double cellValue = plLocalTerritories[r][c];
                    size_t val = static_cast<size_t>(cellValue);
                    for (size_t p = 1; val > 0; p++, val >>= 1) {
                        if (val & 1) {
                            if (p < defeatedPlayers->size() && (*defeatedPlayers)[p]) continue;
                            fill.countCellAttributes(p, cellValue);
                        }
                    }
                } else {
                    fill.registerCell(r, c, plLocalTerritories[r][c]);
                }
                cellsToFill[r][c] = fillIdx;

                // Check 4-neighbors
                for (int d = 0; d < 4; d++) {
                    int nr = static_cast<int>(r) + dx4[d];
                    int nc = static_cast<int>(c) + dy4[d];

                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) { 
                        continue;
                    }
                    if (WalkableTerrainBoard != nullptr && !fill.sameWalkableTerrain((*WalkableTerrainBoard)[nr][nc])) {
                        continue; // block expansion into different terrain type
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
            std::set<Position> obstructionsSeen; // de-duplicate obstruction cells counted for this fill
            auto sameTerrainAt = [&](int row, int col) {
                return WalkableTerrainBoard == nullptr
                    || fill.sameWalkableTerrain((*WalkableTerrainBoard)[row][col]);
            };

            for (const auto& dilPos : fill.getDilationCells()) {
                if (dilVisited.count(dilPos)) {
                    continue;
                }

                if (!sameTerrainAt(static_cast<int>(dilPos.first), static_cast<int>(dilPos.second))) {
                    continue;
                }

                std::set<Position> componentDilCells;
                std::set<Position> gapCells;
                std::set<Position> componentObstructionsSeen;
                std::queue<Position> q;
                q.push(dilPos);
                dilVisited.insert(dilPos);

                while (!q.empty()) {
                    auto top = q.front();
                    q.pop();
                    componentDilCells.insert(top);

                    int dr = static_cast<int>(top.first);
                    int dc = static_cast<int>(top.second);

                    bool neighboursFill = false;
                    std::set<Position> cellsToAdd;

                    for (int d = 0; d < 4; d++) {
                        int ndr = dr + dx4[d];
                        int ndc = dc + dy4[d];
                        if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) continue;
                        if (!sameTerrainAt(ndr, ndc)) continue;

                        Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};

                        // Keep track of neighbours that are dilated walls
                        if (dilatedWalls[np.first][np.second] && !obstructionsBoard[np.first][np.second]
                            && !dilVisited.count(np)) {
                            cellsToAdd.insert(np);
                        }
                        // Keep track if the cell neighbours other fill: not wall, not dilation, not part of this Fill
                        else if (!obstructionsBoard[ndr][ndc] && !dilatedWalls[ndr][ndc]
                                 && cellsToFill[ndr][ndc] != fillIdx ) {
                            gapCells.insert(top);
                            neighboursFill = true;
                        }
                    }

                    if(!neighboursFill) {
                        for(const auto el : cellsToAdd) {
                            dilVisited.insert(el);
                            q.push(el);
                        }
                    }

                    // Collect bordering obstruction cells for this component only.
                    // Ownership is committed later only if this is not a dead-end branch.
                    for (int d = 0; d < 4; d++) {
                        int nor = dr + dx4[d];
                        int noc = dc + dy4[d];
                        if (nor < 0 || nor >= rows || noc < 0 || noc >= cols) continue;
                        if (!obstructionsBoard[nor][noc]) continue;
                        Position obsPos{static_cast<size_t>(nor), static_cast<size_t>(noc)};
                        componentObstructionsSeen.insert(obsPos);
                    }
                }

                if (gapCells.empty()) {

                    // Dead-end branch: convert all dilation cells to fill cells
                    dilToConvert.insert(dilToConvert.end(),
                        componentDilCells.begin(), componentDilCells.end());
                } else {
                    // Only non-dead-end branches contribute to obstruction ownership.
                    for (const auto& obsPos : componentObstructionsSeen) {
                        if (obstructionsSeen.count(obsPos)) {
                            continue;
                        }
                        obstructionsSeen.insert(obsPos);

                        for (size_t playerID = 1; playerID <= numPlayers; ++playerID) {
                            const int pid = static_cast<int>(playerID);

                            if (obstructionOwnedByPlayer(localObstructionBoards, pid,
                                                         static_cast<int>(obsPos.first),
                                                         static_cast<int>(obsPos.second))) {
                                fill.recordAdjacentObstruction(playerID);
                                continue;
                            }

                            if (findPlayerObstructionThroughBranch(obsPos, pid,
                                                                  localObstructionBoards,
                                                                  obstructionsBoard,
                                                                  rows,
                                                                  cols)) {
                                fill.recordAdjacentObstruction(playerID);
                            }
                        }
                    }

                    if(config.testingMode) {
                        
                        // create gap cells board for config.testingMode
                        std::vector<std::vector<int>> gapCellsBoard(rows, std::vector<int>(cols, 0));
                        std::cout << "Gap detected for fill " << fillIdx << " with " << gapCells.size() << " gap cells." << std::endl;
                        std::cout << "Gap cells: " << std::endl;
                        for (const auto& gc : gapCells) {
                            gapCellsBoard[gc.first][gc.second] = 1;
                        }
                        printBoard(gapCellsBoard);
                        std::cout << std::endl;
                    }
                    // Gap found: convert dilation cells that are adjacent to fill cells
                    std::set<Position> gapSet(gapCells.begin(), gapCells.end());
                    std::set<Position> toConvert;
                    
                    for (const auto& dCell : componentDilCells) {
                        // std::cout << "Component cells for loop: " << dCell.first << "," << dCell.second << " for fill " << fillIdx << std::endl;
                        if(gapCells.count(dCell)) {
                            // std::cout << "skipped" << std::endl;
                            continue; // don't convert actual gap cells
                        }
                        bool adjacentToFill = false;
                        for (int d = 0; d < 4; d++) {
                            int ndr = static_cast<int>(dCell.first) + dx4[d];
                            int ndc = static_cast<int>(dCell.second) + dy4[d];
                            if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) {
                                continue;
                            }
                            if (!sameTerrainAt(ndr, ndc)) {
                                continue;
                            }
                            Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};
                            if (cellsToFill[ndr][ndc] == fillIdx) {
                                adjacentToFill = true;
                            }
                        }
                        if (adjacentToFill) {
                            toConvert.insert(dCell);
                        }
                    }


                    // Build remaining dilation cells in this component (not already converting)
                    std::set<Position> remaining;
                    for (const auto& dCell : componentDilCells) {
                        if (!toConvert.count(dCell)) {
                            remaining.insert(dCell);
                        }
                    }

                    // Phase 2: among remaining cells, find those connected to a gap
                    // via 4-neighbor dilation connectivity.
                    std::set<Position> gapConnected;
                    std::stack<Position> s;

                    // Seed: remaining dilation cells directly adjacent to a gap cell
                    // AK: Instead of checking dCells if they neighbour a gap cell, check gap cells to see if they have dilation neighbours
                    // This should be a lot faster as there will be a lot less gaps than dilations
                    for (const auto& dCell : remaining) {
                        // std::cout << "Remaining cells for loop: " << dCell.first << "," << dCell.second << " for fill " << fillIdx << std::endl;
                        bool touchesGap = false;

                        for (int d = 0; d < 4; d++) {
                            int ndr = static_cast<int>(dCell.first) + dx4[d];
                            int ndc = static_cast<int>(dCell.second) + dy4[d];
                            if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) {
                                continue;
                            }
                            if (!sameTerrainAt(ndr, ndc)) {
                                continue;
                            }
                            Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};
                            if (gapSet.count(np)) {
                                touchesGap = true;
                                break;
                            }
                        }
                        // if (gapSet.count(dCell)) { delete above, is what olmate recommended
                        
                        if (touchesGap) {
                            gapConnected.insert(dCell);
                            s.push(dCell);
                        }
                    }
                    std::set<Position> thisFillGapCellsToAdd; // dilation cells that become gap cells after conversion, to seed phase 2
                    std::set<Position> thisFillGapCells; 
                    bool adjacentToFill = false;

                    // Flood through remaining dilation cells
                    while (!s.empty()) {
                        Position cur = s.top();
                        s.pop();
                        adjacentToFill = false;
                        // std::cout << "Remaining cells: " << cur.first << "," << cur.second << " for fill " << fillIdx << std::endl;

                        for (int d = 0; d < 4; d++) {
                            int ndr = static_cast<int>(cur.first) + dx4[d];
                            int ndc = static_cast<int>(cur.second) + dy4[d];
                            if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) {
                                continue;
                            }
                            if (!sameTerrainAt(ndr, ndc)) {
                                continue;
                            }

                            Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};
                            
                            if (cellsToFill[ndr][ndc] == fillIdx || toConvert.count(np)) {
                                thisFillGapCells.emplace(cur);
                                adjacentToFill = true;
                            }
                            if (remaining.count(np) && !gapConnected.count(np)) {
                                gapConnected.insert(np);
                                s.push(np);
                            }
                        }
                    
                        // if (adjacentToFill) {
                        //     toConvert.insert(cur);
                        //     for (const auto& el : thisFillGapCellsToAdd) {
                        //         if(!gapCells.count(el) && !toConvert.count(el)) {
                        //             thisFillGapCells.emplace(el);
                        //             std::cout << "Adding " << el.first << "," << el.second << " to thisFillGapCells for fill " << fillIdx << std::endl;
                        //         }
                        //     }
                        //     // remove the dcell from thisFillGapCells if it's there.
                        //     thisFillGapCells.erase(cur);
                        // }
                    }

                    // Remaining dilation cells NOT connected to any gap should convert to fill
                    for (const auto& dCell : remaining) {
                        if (!gapConnected.count(dCell)) {
                            toConvert.insert(dCell);
                        }
                    }

                    // Persist this connected component for later closure analysis.
                    fill.addConnectedGapSet(gapConnected);
                    
                    if(thisFillGapCells.size() > 0) {
                        gapCells = thisFillGapCells;
                    }
                    

                    dilToConvert.insert(dilToConvert.end(), toConvert.begin(), toConvert.end());
                    gapGroupsToAdd.emplace_back(gapCells.begin(), gapCells.end());
                }
            }

            // Apply conversions: dilation cells → fill cells
            for (const auto& pos : dilToConvert) {
                fill.registerCell(pos.first, pos.second, plLocalTerritories[pos.first][pos.second]);
                fill.removeDilationCell(pos.first, pos.second);
                cellsToFill[pos.first][pos.second] = fillIdx;
                dilatedWalls[pos.first][pos.second] = false;
                visited[pos.first][pos.second] = true;
            }

            // Add gap groups
            for (auto& gg : gapGroupsToAdd) {
                fill.addGapGroup(gg);
            }

            fills.push_back(std::move(fill));

        }
    }

    // Step 3b – Materialise each connected-gap set as its own Fill object.
    // The cells in a connectedGapSet are dilation cells that sit between two or
    // more fills.  By registering them as a real fill and updating cellsToFill,
    // the existing gap-group merge logic in Step 4 will naturally find both
    // neighbours and union them when they share the same dominant player.
    {
        const size_t initialFillCount = fills.size();
        for (size_t fi = 0; fi < initialFillCount; ++fi) {
            for (const auto& connectedSet : fills[fi].getConnectedGapSets()) {
                if (connectedSet.empty()) {
                    continue;
                }

                Fill bridgeFill(rows, cols);
                if (WalkableTerrainBoard != nullptr) {
                    // Inherit terrain type from the parent fill.
                    bridgeFill.setWalkableBool(fills[fi].sameWalkableTerrain(true));
                } else {
                    bridgeFill.setWalkableBool(true);
                }

                const int bridgeIdx = static_cast<int>(fills.size());
                for (const auto& cell : connectedSet) {
                    // Only register cells that haven't already been claimed by another fill.
                    // this way fills that share the same gapConnectedSets won't duplicate
                    // AK: issue here is that fill 0 has two sets and they are being put together rather than
                    // treated separately and bridging different fills.
                    if (cellsToFill[cell.first][cell.second] >= 0) {
                        continue;
                    }
                    bridgeFill.registerCell(cell.first, cell.second,
                                            plLocalTerritories[cell.first][cell.second]);
                    cellsToFill[cell.first][cell.second] = bridgeIdx;
                }

                if (!bridgeFill.getCells().empty()) {
                    fills.push_back(std::move(bridgeFill));
                }
            }
        }
    }

    // Step 4 – Merge Fills that share gap groups with the same dominant player
    {
        int n = static_cast<int>(fills.size());
        UnionFind uf(n);

        
        // Rigorous closure test: determine which player (if any) unambiguously
        // encloses this fill via an unbroken boundary chain.
        // Step 4a - Compute closure owner before merge decisions so
        // getDominantPlayer() uses current closure information.
        for (size_t fi = 0; fi < fills.size(); ++fi) {
            fills[fi].setClosureOwner(checkClosureOwner(
                fills[fi], localObstructionBoards, WalkableTerrainBoard, rows, cols, numPlayers));

            if (config.testingMode && fills[fi].getClosureOwner() != 0) {
                std::cout << "Fill " << fi << " rigourously enclosed by player " << fills[fi].getClosureOwner() << std::endl;
            }

            if(config.testingMode && fi >= 3) {
                std::cout << "Fill " << fi << " after gap detection:" << std::endl;
                std::cout << "Dilated walls after " << fi << " gap detection:" << std::endl;
                printBoard(dilatedWalls); // For config.testingMode
                std::cout << std::endl;

                // std::cout << "visited cells after gap detection:" << std::endl;
                // printBoard(visited); // For config.testingMode
                // std::cout << std::endl;

                std::cout << "cellsToFill (fill results) after gap detection:" << std::endl;
                printBoard(cellsToFill); // For config.testingMode
                std::cout << std::endl;
            }
        }

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
            if (f.numGapGroups() <= config.CLOSED_SHAPE_GAPS_THRESHOLD) {
                filtered.push_back(std::move(f));
            }
        }
        fills = std::move(filtered);
    }

    // Step 6 (Collect all remaining gap groups) and Step 7 (encode dominant player fills into a board)
    std::vector<std::vector<Position>> allGaps(numPlayers + 1); 
    result.fillBoard.assign(rows, std::vector<size_t>(cols, 0));

    for (size_t fi = 0; fi < fills.size(); fi++) {

        // get the dominant player of the fill
        size_t dp = fills[fi].getDominantPlayer();

        // save the gaps for later use
        if (dp < allGaps.size()) {
            for (const auto& gg : fills[fi].getGapGroups()) {
                allGaps[dp].insert(allGaps[dp].end(), gg.begin(), gg.end());
            }
        }

        if (dp == 0) {
            continue;
        }

        // encode the fill cells and save
        const size_t encoded_player_value = static_cast<size_t>(1) << (dp - 1);
        for (const auto& cell : fills[fi].getCells()) {
            result.fillBoard[cell.first][cell.second] = encoded_player_value;
        }
    }

    
    if (config.testingMode) {
        
        // merge the obstructions and dilations together for config.testingMode
        std::vector<std::vector<int>> obsDilBoard(rows, std::vector<int>(cols, 0));
        std::vector<std::vector<int>> obsDilFillBoard(rows, std::vector<int>(cols, 0));
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                for (int player=0;player<localObstructionBoards.size();player++) {
                    int value;
                    if(player == 0) {value = 9;}
                    if(player > 0) {value = player;}
                    obsDilBoard[i][j] += -value*localObstructionBoards[player][i][j];
                }
                obsDilBoard[i][j] -= 8*dilatedWalls[i][j]; 
                obsDilFillBoard[i][j] = (obsDilBoard[i][j] != 0) ? obsDilBoard[i][j] : result.fillBoard[i][j];
            }
        }
        
        std::cout << "Obstructions Board:" << std::endl;
        printBoard(obstructionsBoard); // For config.testingMode
        std::cout << std::endl;

        std::cout << "Obsdillfill Board:" << std::endl;
        printBoard(obsDilFillBoard); // For config.testingMode
        std::cout << std::endl;
        

        std::vector<std::vector<int>> testGapsBoardTeam(config.mapSize, std::vector<int>(config.mapSize, -1));
    const auto& gapBoardTeam = allGaps;
    for(int team = 0; team < gapBoardTeam.size(); ++team) {
        for(int cell = 0; cell < gapBoardTeam[team].size(); ++cell) {
            // Process each gap cell
            const auto& gap = gapBoardTeam[team][cell];
            testGapsBoardTeam[gap.first][gap.second] = team; // Mark the gap cell with the team number (or any other value you want)
        }
    }  
        std::cout << "gap board" << std::endl;
        printBoard(testGapsBoardTeam, 0);
        std::cout << std::endl;

        std::cout << "Player Local Territories Board:" << std::endl;
        printBoard(plLocalTerritories); // For config.testingMode
        std::cout << std::endl;
    }


    result.fills = std::move(fills);
    result.gaps = std::move(allGaps);
    return result;
}


