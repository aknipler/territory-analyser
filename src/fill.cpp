#include "fill.h"
#include "header.h"




// ---------- Fill methods ----------

void Fill::addCell(size_t x, size_t y) {
    cells_.emplace_back(x, y);
    const int xi = static_cast<int>(x);
    const int yi = static_cast<int>(y);
    if (xi < bounds_[0]) bounds_[0] = xi;
    if (xi > bounds_[1]) bounds_[1] = xi;
    if (yi < bounds_[2]) bounds_[2] = yi;
    if (yi > bounds_[3]) bounds_[3] = yi;
}

void Fill::evictCellsInFootprint(size_t x, size_t y, size_t w, size_t h,
                                 size_t influenceExtent,
                                 const std::vector<std::vector<double>>& plLocalTerritories) {
    // Influence bounding box: cells outside this region cannot have changed territory values.
    const size_t infX    = (x > influenceExtent) ? x - influenceExtent : 0;
    const size_t infY    = (y > influenceExtent) ? y - influenceExtent : 0;
    const size_t infXEnd = x + w + influenceExtent;
    const size_t infYEnd = y + h + influenceExtent;
    const int boardW = static_cast<int>(plLocalTerritories.size());

    size_t write = 0;
    int minX = std::numeric_limits<int>::max(), maxX = std::numeric_limits<int>::min();
    int minY = std::numeric_limits<int>::max(), maxY = std::numeric_limits<int>::min();

    for (size_t i = 0; i < cells_.size(); ++i) {
        const size_t cx = cells_[i].first;
        const size_t cy = cells_[i].second;

        if (cx >= x && cx < x + w && cy >= y && cy < y + h) {
            // Footprint cell — subtract its cached contribution and discard.
            const double oldVal = preUpdateCellValues_[i];
            size_t bits = static_cast<size_t>(oldVal);
            for (size_t p = 1; bits > 0; ++p, bits >>= 1) {
                if (!(bits & 1)) continue;
                auto cit = playerCellCount_.find(p);
                if (cit != playerCellCount_.end() && cit->second > 0) --cit->second;
                auto wit = playerWeightCount_.find(p);
                if (wit != playerWeightCount_.end()) wit->second -= oldVal;
            }
            continue; // evicted
        }

        // Cell survives — carry forward its (possibly updated) cached value.
        double cachedVal = preUpdateCellValues_[i];

        if (cx >= infX && cx < infXEnd && cy >= infY && cy < infYEnd) {
            // Inside influence area: territory value may have changed — apply delta.
            const int xi = static_cast<int>(cx), yi = static_cast<int>(cy);
            double newVal = cachedVal;
            if (xi < boardW) {
                const int boardH = static_cast<int>(plLocalTerritories[xi].size());
                if (yi < boardH) newVal = plLocalTerritories[xi][yi];
            }
            if (newVal != cachedVal) {
                size_t oldBits = static_cast<size_t>(cachedVal);
                for (size_t p = 1; oldBits > 0; ++p, oldBits >>= 1) {
                    if (!(oldBits & 1)) continue;
                    auto cit = playerCellCount_.find(p);
                    if (cit != playerCellCount_.end() && cit->second > 0) --cit->second;
                    auto wit = playerWeightCount_.find(p);
                    if (wit != playerWeightCount_.end()) wit->second -= cachedVal;
                }
                size_t newBits = static_cast<size_t>(newVal);
                for (size_t p = 1; newBits > 0; ++p, newBits >>= 1) {
                    if (newBits & 1) {
                        playerCellCount_[p]++;
                        playerWeightCount_[p] += newVal;
                    }
                }
                cachedVal = newVal;
            }
        }
        // Outside influence area: counts are still valid, no change needed.

        cells_[write] = cells_[i];
        preUpdateCellValues_[write] = cachedVal;
        const int xi = static_cast<int>(cx), yi = static_cast<int>(cy);
        if (xi < minX) minX = xi; if (xi > maxX) maxX = xi;
        if (yi < minY) minY = yi; if (yi > maxY) maxY = yi;
        ++write;
    }

    cells_.resize(write);
    preUpdateCellValues_.resize(write);

    if (cells_.empty()) { bounds_ = {}; return; }
    bounds_ = {minX, maxX, minY, maxY};
}

/**
 * @brief Adds the cell to cells_ (via addCell) and increments per-player attribute counts
 *        derived from the cell's bitmask ownership value: each set bit corresponds to a player id.
 */
void Fill::registerCell(size_t x, size_t y, double cellValue) {
    addCell(x, y);
    preUpdateCellValues_.push_back(cellValue);
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

void Fill::addDilationCell(size_t x, size_t y) {
    dilationCells_.emplace(x, y);
}

void Fill::removeDilationCell(size_t x, size_t y) {
    dilationCells_.erase({x, y});
}

void Fill::addConnectedGapSet(const std::set<Position>& connectedGapSet) {
    if (!connectedGapSet.empty()) {
        connectedGapSets_.push_back(connectedGapSet);
    }
}

void Fill::removeConnectedGapSet(const std::set<Position>& connectedSet) {
    for (auto it = connectedGapSets_.begin(); it != connectedGapSets_.end(); ++it) {
        if (*it == connectedSet) {
            connectedGapSets_.erase(it);
            break;
        }
    }
}

void Fill::addFillObstructionNeighbour(size_t playerID, int x, int y) {
    obstructionNeighbours_[playerID].insert({x, y});
}
const std::unordered_map<size_t, std::set<std::pair<int, int>>>& Fill::getFillObstructionNeighbours() const {
    return obstructionNeighbours_;
}

void Fill::countCellAttributes(size_t playerID, double weight) {
    playerCellCount_[playerID]++;
    playerWeightCount_[playerID] += weight;
}

void Fill::setClosureOwner(size_t playerID) {
    closureOwner_ = playerID;
}

size_t Fill::getClosureOwner() const {
    return closureOwner_;
}

// ---------- Fill::merge ----------

/**
 * @brief Appends all cell data from other into this fill: cells, gap groups, dilation cells,
 *        connected-gap sets, map-edge cells, obstruction neighbours, and bounds.
 *        Closure owner is resolved: kept if both fills agree, set to -1 (contested) if they differ.
 */
void Fill::merge(const Fill& other) {
    cells_.insert(cells_.end(), other.cells_.begin(), other.cells_.end());
    preUpdateCellValues_.insert(preUpdateCellValues_.end(),
                                other.preUpdateCellValues_.begin(), other.preUpdateCellValues_.end());
    gapGroups_.insert(gapGroups_.end(), other.gapGroups_.begin(), other.gapGroups_.end());
    dilationCells_.insert(other.dilationCells_.begin(), other.dilationCells_.end());
    connectedGapSets_.insert(connectedGapSets_.end(), other.connectedGapSets_.begin(), other.connectedGapSets_.end());
    mapEdgeCells_.insert(other.mapEdgeCells_.begin(), other.mapEdgeCells_.end());
    for (const auto& kv : other.obstructionNeighbours_) {
        obstructionNeighbours_[kv.first].insert(kv.second.begin(), kv.second.end());
    }
    bounds_[0] = std::min(bounds_[0], other.bounds_[0]);
    bounds_[1] = std::max(bounds_[1], other.bounds_[1]);
    bounds_[2] = std::min(bounds_[2], other.bounds_[2]);
    bounds_[3] = std::max(bounds_[3], other.bounds_[3]);
    for (const auto& kv : other.playerCellCount_) {
        playerCellCount_[kv.first] += kv.second;
    }
    // Merge closure owner: keep if both agree, recalculate from merged fill if contested
    if (closureOwner_ == 0) {
        closureOwner_ = other.closureOwner_;
    } else if (other.closureOwner_ != 0 && other.closureOwner_ != closureOwner_) {
        closureOwner_ = 0; // neutralise before recalculation so neither side gets the walled bonus
        closureOwner_ = getDominantPlayer();
    }
}

/**
 * @brief Drops any gap group whose every cell is owned by one of the fills in mergedFillIndices.
 *        Called after a union-find merge so that gaps between now-merged fills are not double-counted.
 */
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

// ---------- Fill::batchRegisterCellsAndMapEdgeCells ----------

/**
 * @brief Bulk-initialises a fill after BFS: moves the accumulated cell list into cells_, performs a
 *        single-pass bounds update and player attribute count, then flushes the dilation and
 *        map-edge cell buffers.  The hasDefeatedPlayers flag enables skipping defeated-player bits
 *        during attribute counting.
 */
void Fill::batchRegisterCellsAndMapEdgeCells(
    bool hasDefeatedPlayers,
    std::vector<Position>&& cellsToRegister,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::pair<int, int>>& mapEdgeCellsToAdd,
    const std::vector<Position>& dilationCellsToAdd,
    const std::vector<bool>* defeatedPlayers,
    std::vector<std::vector<int>>& cellsToFill,
    int fillIdx)
{
    // Move the BFS-accumulated vector directly into cells_ — zero copy.
    cells_ = std::move(cellsToRegister);
    preUpdateCellValues_.clear();
    preUpdateCellValues_.reserve(cells_.size());

    // Single pass: update bounds and accumulate player attributes.
    int minX = bounds_[0], maxX = bounds_[1];
    int minY = bounds_[2], maxY = bounds_[3];

    if (hasDefeatedPlayers) {
        for (const auto& pos : cells_) {
            const int xi = static_cast<int>(pos.first);
            const int yi = static_cast<int>(pos.second);
            if (xi < minX) minX = xi;
            if (xi > maxX) maxX = xi;
            if (yi < minY) minY = yi;
            if (yi > maxY) maxY = yi;

            const double cellValue = plLocalTerritories[pos.first][pos.second];
            preUpdateCellValues_.push_back(cellValue);
            size_t val = static_cast<size_t>(cellValue);
            for (size_t p = 1; val > 0; p++, val >>= 1) {
                if (val & 1) {
                    if (p < defeatedPlayers->size() && (*defeatedPlayers)[p]) continue;
                    countCellAttributes(p, cellValue);
                }
            }
            cellsToFill[pos.first][pos.second] = fillIdx;

        }
    } else {
        for (const auto& pos : cells_) {
            const int xi = static_cast<int>(pos.first);
            const int yi = static_cast<int>(pos.second);
            if (xi < minX) minX = xi;
            if (xi > maxX) maxX = xi;
            if (yi < minY) minY = yi;
            if (yi > maxY) maxY = yi;

            const double cellValue = plLocalTerritories[pos.first][pos.second];
            preUpdateCellValues_.push_back(cellValue);
            size_t val = static_cast<size_t>(cellValue);
            for (size_t p = 1; val > 0; p++, val >>= 1) {
                if (val & 1) {
                    countCellAttributes(p, cellValue);
                }
            }
            cellsToFill[pos.first][pos.second] = fillIdx;
        }
    }

    bounds_[0] = minX; bounds_[1] = maxX;
    bounds_[2] = minY; bounds_[3] = maxY;

    // Flush dilation cells collected during BFS.
    dilationCells_.reserve(dilationCells_.size() + dilationCellsToAdd.size());
    for (const auto& pos : dilationCellsToAdd) {
        dilationCells_.emplace(pos.first, pos.second);
    }

    // Reserve before bulk-inserting to avoid mid-loop rehashing.
    mapEdgeCells_.reserve(mapEdgeCells_.size() + mapEdgeCellsToAdd.size());
    for (const auto& cell : mapEdgeCellsToAdd) {
        mapEdgeCells_.emplace(cell.first, cell.second);
    }
}

// ---------- Fill::batchConvertDilationToFill ----------

/**
 * @brief Promotes a batch of dilation cells to full fill cells: appends them to cells_, updates
 *        bounds and player attributes, and removes them from dilationCells_, cellsToFill,
 *        dilatedWalls, and visited tracking arrays.
 */
void Fill::batchConvertDilationToFill(
    const std::vector<Position>& positions,
    const std::vector<std::vector<double>>& plLocalTerritories,
    std::vector<std::vector<int>>& cellsToFill,
    std::vector<std::vector<bool>>& dilatedWalls,
    std::vector<std::vector<bool>>& visited,
    int fillIdx) {
    
    // Reserve space to avoid reallocations during emplace_back
    cells_.reserve(cells_.size() + positions.size());
    preUpdateCellValues_.reserve(preUpdateCellValues_.size() + positions.size());
    
    // Track bounds for batch update
    int minX = bounds_[0], maxX = bounds_[1];
    int minY = bounds_[2], maxY = bounds_[3];
    
    for (const auto& pos : positions) {
        size_t x = pos.first;
        size_t y = pos.second;
        double cellValue = plLocalTerritories[x][y];
        
        // Add cell to cells_ vector
        cells_.emplace_back(x, y);
        preUpdateCellValues_.emplace_back(cellValue);
        
        // Track bounds for batch update
        int xi = static_cast<int>(x);
        int yi = static_cast<int>(y);
        if (xi < minX) minX = xi;
        if (xi > maxX) maxX = xi;
        if (yi < minY) minY = yi;
        if (yi > maxY) maxY = yi;
        
        // Count player attributes from cell value
        size_t val = static_cast<size_t>(cellValue);
        for (size_t p = 1; val > 0; p++, val >>= 1) {
            if (val & 1) {
                countCellAttributes(p, cellValue);
            }
        }
        
        // Remove from dilation set and update tracking arrays
        dilationCells_.erase(pos);
        cellsToFill[x][y] = fillIdx;
        dilatedWalls[x][y] = false;
        visited[x][y] = true;
    }
    
    // Batch update bounds once
    bounds_[0] = minX;
    bounds_[1] = maxX;
    bounds_[2] = minY;
    bounds_[3] = maxY;
}

// ---------- Fill::getDominantPlayer ----------

/**
 * @brief Returns the player id that owns this fill, or 0 if no player clears both the
 *        ownershipThreshold (cell fraction) and contestedOwnershipThreshold (share of investment).
 *        A sole-enclosure bonus (isWalledMultiplier) is applied when closureOwner_ matches the
 *        candidate player.
 */
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
                    std::cout << "Player " << playerWithMax << " has max adjusted count: " << maxCount << " vs cells count: " << cells_.size() << " i.e. " << std::fixed << std::setprecision(1) << (static_cast<double>(maxCount) / cells_.size() * 100.0) << "%" << std::endl;
                    std::cout << "Boosted ownership percentage: " << std::fixed << std::setprecision(1) << (static_cast<double>(maxCount) / cells_.size() * 100.0 * (soleObstructionOwner != 0 && playerWithMax == soleObstructionOwner ? isWalledMultiplier : 1)) << "%" << std::endl;
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
const std::unordered_set<Position, PositionHash>& Fill::getDilationCells() const { return dilationCells_; }
const std::vector<std::set<Position>>& Fill::getConnectedGapSets() const { return connectedGapSets_; }
const std::unordered_map<size_t, size_t>& Fill::getPlayerCount() const { return playerCellCount_; }
const std::array<int, 4>& Fill::getBounds() const { return bounds_; }
size_t Fill::numGapGroups() const { return gapGroups_.size(); }

void Fill::addMapEdgeCell(int x, int y) {
    mapEdgeCells_.emplace(x, y);
}

const std::unordered_set<std::pair<int, int>, SignedPairHash>& Fill::getMapEdgeCells() const {
    return mapEdgeCells_;
}

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

bool boundsOverlap(const std::array<int, 4>& a, const std::array<int, 4>& b) {
    return a[0] <= b[1] && b[0] <= a[1] && a[2] <= b[3] && b[2] <= a[3];
}

bool boundsContain(const std::array<int, 4>& outer, const std::array<int, 4>& inner) {
    return inner[0] >= outer[0]
        && inner[1] <= outer[1]
        && inner[2] >= outer[2]
        && inner[3] <= outer[3];
}

std::array<int, 4> mergeBounds(const std::array<int, 4>& a, const std::array<int, 4>& b) {
    return {
        std::min(a[0], b[0]),
        std::max(a[1], b[1]),
        std::min(a[2], b[2]),
        std::max(a[3], b[3])
    };
}

int boundsArea(const std::array<int, 4>& b) {
    return (b[1] - b[0] + 1) * (b[3] - b[2] + 1);
}

/**
 * @brief Returns true if sets a and b share at least one cell within overlapBounds, indicating
 *        that the two boundary components connect or intersect inside their bounding-box overlap.
 */
bool boundaryIntersectsOrConnects(
    const std::unordered_set<std::pair<int, int>, SignedPairHash>& a,
    const std::unordered_set<std::pair<int, int>, SignedPairHash>& b,
    const std::array<int, 4>& overlapBounds)
{
    for (const auto& cell : a) {
        const int x = cell.first;
        const int y = cell.second;
        if (x < overlapBounds[0] || x > overlapBounds[1]
            || y < overlapBounds[2] || y > overlapBounds[3]) {
            continue;
        }

        if (b.count(cell)) {
            return true;
        }
    }
    return false;
}

bool obstructionOwnedByPlayer(
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    int playerID,
    int x,
    int y)
{
    if (playerID < 0) return false;
    const size_t index = static_cast<size_t>(playerID);
    if (index >= playerObstructionBoards.size()) return false;
    return playerObstructionBoards[index][x][y];
}

bool touchesAnyNonObstructionNeighbor(
    int x,
    int y,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    int width,
    int height)
{
    for (int d = 0; d < 4; d++) {
        int nextX = x + dx4[d];
        int nextY = y + dy4[d];
        if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
        if (!obstructionsBoard[nextX][nextY]) return true;
    }
    return false;
}


struct BranchResult {
    bool touchesOtherFill;
    std::set<std::pair<int, int>> obstructionsFound;
};

/**
 * @brief DFS through obstruction cells not owned by playerID, starting from start.
 *        Reports whether the branch reaches open (non-obstruction) space, and collects
 *        any obstruction cells owned by playerID that are reachable along the branch.
 */
BranchResult findPlayerObstructionThroughBranch(
    const std::pair<int,int>& start,
    std::set<std::pair<int,int>>& visited,
    std::unordered_set<Position, PositionHash>& boundary,
    int playerID,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    int width,
    int height)
{
    
    // DFS through obstruction cells not owned by the target player.
    // Branch rule: if a visited cell touches open space or IS target player obstruction, do not expand past it.
    bool touchesOtherFill = false;
    std::stack<std::pair<int,int>> stk;
    std::set<std::pair<int, int>> obstructionsFound;
    std::set<std::pair<int,int>> stkToAdd; 
    stk.push(start);
    visited.insert(start);

    while (!stk.empty()) {
        std::pair<int,int> cur = stk.top();
        stk.pop();
        stkToAdd.clear();

        int x = cur.first;
        int y = cur.second;

        if (!touchesOtherFill && touchesAnyNonObstructionNeighbor(x, y, obstructionsBoard, width, height)) {
            touchesOtherFill = true;
        }

        for (int d = 0; d < 4; d++) {
            int nextX = x + dx4[d];
            int nextY = y + dy4[d];
            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
            if (!obstructionsBoard[nextX][nextY]) continue;

            if (obstructionOwnedByPlayer(playerObstructionBoards, playerID, nextX, nextY)) {
                obstructionsFound.insert({nextX, nextY});
            }

            std::pair np{nextX, nextY};
            if (!visited.count(np)) {
                visited.insert(np);
                stkToAdd.insert(np);
            }
        }
        for (const auto& el : stkToAdd) {
            stk.push(el);
        }
    }

    for (const auto& foundObstr : obstructionsFound) {
        boundary.insert(foundObstr);
    }

    return BranchResult{touchesOtherFill, obstructionsFound};
}

} // anonymous namespace

// Returns the sole player whose per-player boundary (their obstruction cells adjacent to this
// fill + all gap cells neighboring this fill + map edge touches) forms a cycle (E >= V).
// Returns 0 if none or if contested.
// Note: Closure detection now relies on cycle detection; paths anchored to map edges are
// implicitly handled via mapEdgeCells in the boundary graph.
static size_t checkClosureOwner(
    const Fill& fill,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    const std::vector<std::vector<bool>>&  obstructionsBoard,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    int width, int height, size_t numPlayers,
    const std::vector<Fill>* allFills,
    const std::vector<size_t>* preMergeDominantOwners,
    int thisFillIndex)
{

    // Early exit optimization: if only one player and Gaia obstruct the fill,
    // then that player trivially encloses it
    const auto& obstNeighbours = fill.getFillObstructionNeighbours();
    {
        size_t candidatePlayer = 0;
        bool onlyOnePlayer = true;
        int totalObs = 0;
        for (const auto& playerEntry : obstNeighbours) {
            for (const auto& cell : playerEntry.second) {
                int x = cell.first;
                int y = cell.second;
                // std::cout << "Obstacles for player: " << playerEntry.first << "located at: " << x << "," << y << std::endl;
            }
            if (playerEntry.first == 0) {
                totalObs += playerEntry.second.size();
            }
            if (playerEntry.first != 0) {  // Not Gaia
                if (candidatePlayer == 0) {
                    candidatePlayer = playerEntry.first;  // Found first non-Gaia player
                    totalObs += playerEntry.second.size();
                } else if (playerEntry.first != candidatePlayer) {
                    onlyOnePlayer = false;  // Found a second non-Gaia player
                    break;
                }
            }
        }
        if (onlyOnePlayer && candidatePlayer > 0 && totalObs > 9) {
            // Only one player (and possibly Gaia) obstruct this fill
            std::cout << "Early exit closure detection: only player " << candidatePlayer << " obstructs this fill, so they are the sole enclosure candidate." << std::endl;
            return candidatePlayer;
        }
    }


    auto cCOpt1_start = std::chrono::high_resolution_clock::now();

    const std::array<int, 4>& fillBounds = fill.getBounds();

    Config config = AppConfig::get();

    // Collect gap cells from all gap groups of this fill
    std::set<Position> gapSet;
    for (const auto& gg : fill.getGapGroups()) {
        for (const auto& gp : gg) {
            gapSet.insert(gp);
        }
    }

    size_t enclosedBy = 0;
    
    // auto pid_start = std::chrono::high_resolution_clock::now();
    // auto init_duration = std::chrono::duration_cast<std::chrono::microseconds>(pid_start - cCOpt1_start).count();
    // std::cout << "TIMING: Initialization took " << init_duration << " microseconds." << std::endl;


    for (size_t pid = 1; pid <= numPlayers; ++pid) {
        
        // auto pid_start = std::chrono::high_resolution_clock::now();
        // If 
        if (obstNeighbours.find(pid) == obstNeighbours.end()) {
            continue;
        }

        if (pid >= playerObstructionBoards.size()) {
            continue;
        }
        const auto& pBoard = playerObstructionBoards[pid];

        struct BoundaryComponent {
            std::unordered_set<std::pair<int, int>, SignedPairHash> cells;  // Both in-bounds and map edge cells
            std::array<int, 4> bounds;
            bool closes = false;
        };

        // Build candidate boundary: player-p obstruction cells 4-adjacent to any fill cell
        // or unconverted dilation cell, + all gap cells.
        std::unordered_set<Position, PositionHash> boundary;

        // auto checkNeighboursForBoundary = [&](int x, int y) {
        //     // AK: I wonder if this needs to be changed to be 8-neighbour in case of diagonal issues
        //     for (int d = 0; d < 4; d++) {
        //         int nextX = x + dx4[d];
        //         int nextY = y + dy4[d];
        //         if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
        //         // if its an obstruction cell owned by this player or gaia, or if it's a walkability mismatch with the fill's terrain, it's part of the boundary
        //         if (pBoard[nextX][nextY] || playerObstructionBoards[0][nextX][nextY] || (WalkableTerrainBoard != nullptr && !fill.sameWalkableTerrain((*WalkableTerrainBoard)[nextX][nextY]))) {
        //             boundary.insert({static_cast<size_t>(nextX), static_cast<size_t>(nextY)});
        //             // if(config.testingMode == true) {
        //             //     std::cout << "Boundary cell added (" << nextX << "," << nextY << ") player " << pid << std::endl;
        //             // }
        //         }
        //     }
        // };





        std::set<std::pair<int,int>> visitedObstrThrBranch;
        for (const auto& playerEntry : fill.getFillObstructionNeighbours()) {
            if (playerEntry.first != pid && playerEntry.first != 0) {
                for (const auto& coord : playerEntry.second) {
                    // Collect this players obstructions through enemy obstructions
                    if(!visitedObstrThrBranch.count(coord) ) {
                        // elements are added to boundary in the function
                        findPlayerObstructionThroughBranch(coord, visitedObstrThrBranch, boundary, pid, playerObstructionBoards, obstructionsBoard, width, height);
                    }
                }
                // Only add obstructions owned by the player of interest (pid)
                continue;
            }
            for (const auto& cell : playerEntry.second) {
                int x = cell.first;
                int y = cell.second;
                boundary.insert({static_cast<size_t>(x), static_cast<size_t>(y)});
            }
        }

        // Also check connected-gap cells: these are the non-converted dilation cells
        // that remain connected to each specific gap component.
        // for (const auto& gapConnectedSet : fill.getConnectedGapSets()) {
        //     for (const auto& gapConnectedCell : gapConnectedSet) {
        //         boundary.insert(gapConnectedCell);
        //         int x = static_cast<int>(gapConnectedCell.first);
        //         int y = static_cast<int>(gapConnectedCell.second);
        //         // if(config.testingMode == true) {
        //         //     std::cout << "Checking neighbors of connected gap cell (" << r << "," << c << ") for player " << pid << std::endl;
        //         // }
        //         checkNeighboursForBoundary(x, y);
        //     }
        // }
        
        // auto intermediate_clock = std::chrono::high_resolution_clock::now();
        // auto intermediate_duration = std::chrono::duration_cast<std::chrono::microseconds>(intermediate_clock - pid_start).count();
        // std::cout << "TIMING: Intermediate check for p " << pid << " took " << intermediate_duration << " microseconds with boundary size " << boundary.size() << "." << std::endl;


        for (const auto& gp : gapSet) {
            // AK: am worried that this could end up grabbing obstructions outside the region of interest
            // Also, is this already fulfilled by gapConnectedSets?
            boundary.insert(gp);
            if(config.testingMode == true && false) {
                std::cout << "Gap cell (" << gp.first << "," << gp.second << ") added to boundary for player " << pid << std::endl;
            }

            // Gap cells can be the only cells that expose an owned obstruction or
            // terrain boundary to the candidate closure chain.
            // checkNeighboursForBoundary(static_cast<int>(gp.first), static_cast<int>(gp.second));
        }

        
        // auto gappy_end = std::chrono::high_resolution_clock::now();
        // auto gappy_duration = std::chrono::duration_cast<std::chrono::microseconds>(gappy_end - pid_start).count();
        // std::cout << "TIMING: Gap check for p " << pid << " took " << gappy_duration << " microseconds with boundary size " << boundary.size() << "." << std::endl;

        
        // Add map edge cells that this fill touches
        const auto& mapEdgeCellsSet = fill.getMapEdgeCells();

        // mapEdgeCellSet can be empty, but boundary cannot be.
        if (boundary.empty()) continue;

        // BFS to find connected components; for each check closure condition
        std::unordered_set<std::pair<int, int>, SignedPairHash> visited;
        visited.reserve(boundary.size() + mapEdgeCellsSet.size());
        bool playerCloses = false;
        std::vector<BoundaryComponent> closureCandidates;

        // Helper to check if a cell is in the boundary (either in-bounds or map edge)
        auto isBoundaryCell = [&](int x, int y) -> bool {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                return boundary.count({static_cast<size_t>(x), static_cast<size_t>(y)}) > 0;
            } else {
                return mapEdgeCellsSet.count({x, y}) > 0;
            }
        };
        
        for (const auto& start : boundary) {
            std::pair<int, int> startPair{static_cast<int>(start.first), static_cast<int>(start.second)};
            if (visited.count(startPair)) continue;

            std::unordered_set<std::pair<int, int>, SignedPairHash> component;
            std::vector<std::pair<int, int>> bfsq; // flat queue: contiguous, cache-friendly
            bfsq.reserve(boundary.size() + mapEdgeCellsSet.size()); // reserve max possible size to avoid reallocations
            size_t bfsHead = 0;
            bfsq.push_back(startPair);
            visited.insert(startPair);

            if(config.testingMode == true && false) {
                std::cout << "First cell (" << start.first << "," << start.second << ") player " << pid << std::endl;
            }

            std::array<int, 4> componentBounds = {width, 0, height, 0};
            int edgeCount = 0;

            while (bfsHead < bfsq.size()) {
                auto cur = bfsq[bfsHead++];
                int x = cur.first;
                int y = cur.second;

                // Add all cells (in-bounds and out-of-bounds) to component
                component.insert(cur);
                
                // Update bounds for all cells (both in-bounds and map edge)
                if (x < componentBounds[0]) componentBounds[0] = x;
                if (x > componentBounds[1]) componentBounds[1] = x;
                if (y < componentBounds[2]) componentBounds[2] = y;
                if (y > componentBounds[3]) componentBounds[3] = y;

                // Process all 8-neighbors: explore unvisited boundary cells and count edges
                for (int d = 0; d < 8; d++) {
                    int nextX = x + dx8[d];
                    int nextY = y + dy8[d];
                    
                    std::pair<int, int> npPair{nextX, nextY};
                    
                    // Explore unvisited boundary cells
                    if (isBoundaryCell(nextX, nextY)) {
                        // Count edge if neighbor is lexicographically greater (ensures each edge counted once)
                        if (nextX > x || (nextX == x && nextY > y)) {
                            edgeCount++;
                        }
                        // Add to queue if not yet visited
                        if (!visited.count(npPair)) {
                            visited.insert(npPair);
                            bfsq.push_back(npPair);
                        }
                    }
                }
            }

            // Closure: cycle (E >= V)
            bool hasCycle = (edgeCount >= static_cast<int>(component.size()));
            if (hasCycle) {
                closureCandidates.push_back(BoundaryComponent{
                    std::move(component),
                    componentBounds,
                    true
                });
            }
        }

        // auto cCOpt1_end = std::chrono::high_resolution_clock::now();
        // auto cCOpt1_duration = std::chrono::duration_cast<std::chrono::microseconds>(cCOpt1_end - cCOpt1_start).count();
        // std::cout << "TIMING: Closure candidate search for player " << pid << " took " << cCOpt1_duration << " microseconds with " << closureCandidates.size() << " candidates." << std::endl;


        if (!closureCandidates.empty()) {
            std::vector<size_t> order(closureCandidates.size(), 0);
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
                return boundsArea(closureCandidates[lhs].bounds) < boundsArea(closureCandidates[rhs].bounds);
            });

            std::vector<int> parent(static_cast<int>(closureCandidates.size()), 0);
            std::iota(parent.begin(), parent.end(), 0);

            auto findRoot = [&](int x) {
                while (parent[x] != x) {
                    parent[x] = parent[parent[x]];
                    x = parent[x];
                }
                return x;
            };

            auto uniteRoots = [&](int a, int b) {
                a = findRoot(a);
                b = findRoot(b);
                if (a != b) {
                    parent[b] = a;
                }
            };

            // Bounding-box idea 2.0: start from smallest boxes, then merge only
            // when overlap exists and boundaries truly intersect/connect.
            for (size_t oi = 0; oi < order.size(); ++oi) {
                for (size_t oj = oi + 1; oj < order.size(); ++oj) {
                    const int i = static_cast<int>(order[oi]);
                    const int j = static_cast<int>(order[oj]);

                    if (!boundsOverlap(closureCandidates[i].bounds, closureCandidates[j].bounds)) {
                        continue;
                    }

                    const std::array<int, 4> overlapBounds = {
                        std::max(closureCandidates[i].bounds[0], closureCandidates[j].bounds[0]),
                        std::min(closureCandidates[i].bounds[1], closureCandidates[j].bounds[1]),
                        std::max(closureCandidates[i].bounds[2], closureCandidates[j].bounds[2]),
                        std::min(closureCandidates[i].bounds[3], closureCandidates[j].bounds[3])
                    };

                    if (overlapBounds[0] > overlapBounds[1] || overlapBounds[2] > overlapBounds[3]) {
                        continue;
                    }

                    if (boundaryIntersectsOrConnects(closureCandidates[i].cells,
                                                     closureCandidates[j].cells,
                                                     overlapBounds)) {
                        uniteRoots(i, j);
                    }
                }
            }

            std::unordered_map<int, std::array<int, 4>> mergedBoundsByRoot;
            for (size_t idx = 0; idx < closureCandidates.size(); ++idx) {
                const int root = findRoot(static_cast<int>(idx));
                auto it = mergedBoundsByRoot.find(root);
                if (it == mergedBoundsByRoot.end()) {
                    mergedBoundsByRoot[root] = closureCandidates[idx].bounds;
                } else {
                    it->second = mergeBounds(it->second, closureCandidates[idx].bounds);
                }
            }

            for (const auto& kv : mergedBoundsByRoot) {
                const auto& mergedBounds = kv.second;
                if (!boundsContain(mergedBounds, fillBounds)) {
                    continue;
                }

                bool ownerConsistent = true;
                bool sawInsideFill = false;

                if (allFills != nullptr && preMergeDominantOwners != nullptr
                    && allFills->size() == preMergeDominantOwners->size()) {
                    for (size_t otherIdx = 0; otherIdx < allFills->size(); ++otherIdx) {
                        if (static_cast<int>(otherIdx) == thisFillIndex) {
                            continue;
                        }
                        if (!boundsContain(mergedBounds, (*allFills)[otherIdx].getBounds())) {
                            continue;
                        }

                        sawInsideFill = true;
                        size_t insideOwner = (*preMergeDominantOwners)[otherIdx];
                        if (insideOwner != 0 && insideOwner != pid) {
                            ownerConsistent = false;
                            break;
                        }
                    }
                }

                // Require this fill itself to be compatible with the wall owner.
                size_t thisOwner = 0;
                if (preMergeDominantOwners != nullptr
                    && thisFillIndex >= 0
                    && static_cast<size_t>(thisFillIndex) < preMergeDominantOwners->size()) {
                    thisOwner = (*preMergeDominantOwners)[static_cast<size_t>(thisFillIndex)];
                }

                if (thisOwner != 0 && thisOwner != pid) {
                    ownerConsistent = false;
                }

                // Accept either: enclosing this fill only, or enclosing a group where
                // all non-contested enclosed fills share the same owner as the wall.
                if (ownerConsistent && (sawInsideFill || thisOwner == pid || thisOwner == 0)) {
                    playerCloses = true;
                    break;
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

// ---------- dilateObstructionsAndTerrain ----------

/**
 * @brief Builds a dilated-walls board by marking the 8-neighbours of every obstruction cell
 *        (step 1).  Border cells adjacent to an existing dilation are also marked (step 1b),
 *        preventing fills from bleeding to the map boundary.  Both sides of any water/land
 *        terrain boundary are additionally marked as dilation walls (step 1c).
 */
static std::vector<std::vector<bool>> dilateObstructionsAndTerrain(
    const std::vector<std::vector<bool>>& obstructionsBoard,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    int width,
    int height)
{
    std::vector<std::vector<bool>> dilatedWalls(width, std::vector<bool>(height, false));

    // Step 1 – Dilate walls: mark 8-directional neighbors of each wall cell
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (!obstructionsBoard[x][y]) {
                continue; 
            } else {
                for (int d = 0; d < 8; d++) {
                    int nextX = x + dx8[d];
                    int nextY = y + dy8[d];
                    if (nextX >= 0 && nextX < width && nextY >= 0 && nextY < height && !obstructionsBoard[nextX][nextY]) {
                        dilatedWalls[nextX][nextY] = true;
                    }
                }
            }
        }
    }

    // Step 1b – Treat map edges as implicit walls: mark border cells as dilation
    //           walls so fills don't bleed to the boundary, unless an obstruction
    //           is already there.
    // We only add it the map edge dilation if there is a dilated wall within 1 distance
    for (int y = 0; y < height; ++y) {
        if (width > 1 && !obstructionsBoard[0][y] && dilatedWalls[1][y]) dilatedWalls[0][y] = true;
        if (width > 1 && !obstructionsBoard[width - 1][y] && dilatedWalls[width - 2][y]) dilatedWalls[width - 1][y] = true;
    }
    for (int x = 0; x < width; ++x) {
        if (height > 1 && !obstructionsBoard[x][0] && dilatedWalls[x][1]) dilatedWalls[x][0] = true;
        if (height > 1 && !obstructionsBoard[x][height - 1] && dilatedWalls[x][height - 2]) dilatedWalls[x][height - 1] = true;
    }

    // Step 1c - Mark terrain boundary cells as dilation walls on both sides of the
    //           water/land boundary. Uses 8-neighbor comparison so both the water cell
    //           and the adjacent land cell are captured (unlike findEdges which encodes
    //           the terrain value and leaves land-side boundary cells at 0).
    if (WalkableTerrainBoard != nullptr) {
        const auto& nwtb = *WalkableTerrainBoard;
        const int terrainWidth = std::min(width, static_cast<int>(nwtb.size()));
        const int terrainHeight = terrainWidth > 0 ? std::min(height, static_cast<int>(nwtb[0].size())) : 0;
        for (int x = 0; x < terrainWidth; ++x) {
            for (int y = 0; y < terrainHeight; ++y) {
                if (y >= static_cast<int>(nwtb[x].size())) {
                    continue;
                }
                if (obstructionsBoard[x][y]) continue;
                bool onBoundary = false;
                for (int d = 0; d < 8; d++) {
                    int nextX = x + dx8[d];
                    int nextY = y + dy8[d];
                    if (nextX < 0 || nextX >= terrainWidth || nextY < 0 || nextY >= terrainHeight) continue;
                    if (nextY >= static_cast<int>(nwtb[nextX].size())) {
                        continue;
                    }
                    if (nwtb[nextX][nextY] != nwtb[x][y]) { onBoundary = true; break; }
                }
                if (onBoundary) dilatedWalls[x][y] = true;
            }
        }
    }

    return dilatedWalls;
}

// ---------- fillConnectedGapSets ----------

/**
 * @brief Materialises each connectedGapSet on existing fills as a new bridge Fill object
 *        (step 3b).  Cells already claimed by another fill are skipped; resulting bridge fills
 *        are appended to fills so the merge step can union them with their neighbours.
 */
static void fillConnectedGapSets(
    std::vector<Fill>& fills,
    std::vector<std::vector<int>>& cellsToFill,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    int width,
    int height)
{
    // Step 3b
    // The cells in a connectedGapSet are dilation cells that sit between two or
    // more fills.  By registering them as a real fill and updating cellsToFill,
    // the existing gap-group merge logic in Step 4 will naturally find both
    // neighbours and union them when they share the same dominant player.
    const size_t initialFillCount = fills.size();
    for (size_t fi = 0; fi < initialFillCount; ++fi) {

        const std::vector<std::set<Position>> gapSets = fills[fi].getConnectedGapSets();
        for (const auto& connectedSet : gapSets) {
            if (connectedSet.empty()) {
                continue;
            }

            Fill bridgeFill(width, height);
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

            
            for (size_t ri = 0; ri < initialFillCount; ++ri) {
                fills[ri].removeConnectedGapSet(connectedSet);
            }
            if (!bridgeFill.getCells().empty()) {
                std::cout << "fill pushed back: " << fills.size() << std::endl;
                fills.push_back(std::move(bridgeFill));
            }
        }
    }
}

// ---------- mergeFills ----------

/**
 * @brief Computes closure ownership for each fill (step 4a), then unions fills that share a
 *        gap group with the same dominant player.  Internal gap groups are pruned, fills are
 *        rebuilt from union-find groups, and fills with fewer than 4 cells are discarded.
 */
static void mergeFills(
    std::vector<Fill>& fills,
    const std::vector<std::vector<int>>& cellsToFill,
    const std::vector<std::vector<std::vector<bool>>>& localObstructionBoards,
    const std::vector<std::vector<bool>>&  obstructionsBoard,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<std::vector<bool>>& dilatedWalls,
    int width,
    int height,
    size_t numPlayers)
{

    const Config& config = AppConfig::get();

    int n = static_cast<int>(fills.size());
    UnionFind uf(n);

    // Baseline dominant owners before closure boosts are applied.
    std::vector<size_t> preMergeDominantOwners(fills.size(), 0);
    for (size_t fi = 0; fi < fills.size(); ++fi) {
        preMergeDominantOwners[fi] = fills[fi].getDominantPlayer();
    }

    auto mFStart = std::chrono::high_resolution_clock::now();

    // Rigorous closure test: determine which player (if any) unambiguously
    // encloses this fill via an unbroken boundary chain.
    // Step 4a - Compute closure owner before merge decisions so
    // getDominantPlayer() uses current closure information.
    for (size_t fi = 0; fi < fills.size(); ++fi) {
        fills[fi].setClosureOwner(checkClosureOwner(
            fills[fi],
            localObstructionBoards,
            obstructionsBoard,
            WalkableTerrainBoard,
            width,
            height,
            numPlayers,
            &fills,
            &preMergeDominantOwners,
            static_cast<int>(fi)));

        // if (config.testingMode && fills[fi].getClosureOwner() != 0) {
            std::cout << "Fill " << fi << " rigourously enclosed by player " << fills[fi].getClosureOwner() << std::endl;
        // }

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
    
    auto mFEnd = std::chrono::high_resolution_clock::now();
    auto mFTime = std::chrono::duration_cast<std::chrono::milliseconds>(mFEnd - mFStart); 
    std::cout << "TIMING: merge fill first half time taken: " << mFTime.count() << "ms" << std::endl;

    mFStart = std::chrono::high_resolution_clock::now();

    for (int fi = 0; fi < n; fi++) {
        for (const auto& gg : fills[fi].getGapGroups()) {
            for (const auto& gp : gg) {
                int other = cellsToFill[gp.first][gp.second];
                if (other >= 0 && other < n && other != fi
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
    
    // Filter out fills with less than 6 cells
    fills.erase(std::remove_if(fills.begin(), fills.end(), 
        [](const Fill& f) { return f.getCells().size() < 4; }), 
        fills.end());
    
    // mFEnd = std::chrono::high_resolution_clock::now();
    // mFTime = std::chrono::duration_cast<std::chrono::milliseconds>(mFEnd - mFStart); 
    // std::cout << "merge fill second half time taken: " << mFTime.count() << "ms" << std::endl;
}

// ---------- removeShapesByGapThreshold ----------

/**
 * @brief Discards fills whose gap-group count exceeds CLOSED_SHAPE_GAPS_THRESHOLD, retaining
 *        only fills that represent reasonably enclosed shapes (step 5).
 */
static void removeShapesByGapThreshold(std::vector<Fill>& fills)
{
    const Config& config = AppConfig::get();

    std::vector<Fill> filtered;
    filtered.reserve(fills.size());
    int fIdx = 0; 
    for (auto& f : fills) {
        // std::cout << "Fill " << fIdx << " has " << f.numGapGroups() << " gap groups." << std::endl;
        if (f.numGapGroups() <= config.CLOSED_SHAPE_GAPS_THRESHOLD) {
            // std::cout << "Keeping fill " << fIdx << " with " << f.numGapGroups() << " gap groups." << std::endl;
            filtered.push_back(std::move(f));
        }
        fIdx++;
    }
    fills = std::move(filtered);
}

// ---------- keepOnlyGapConnected ----------

/**
 * @brief From the dilation component in remaining, keeps only cells reachable from a gap cell
 *        via 4-neighbour dilation connectivity (same terrain type).  Unreachable cells are moved
 *        into toConvert so they become fill cells.  Also records which fill cells neighbour the
 *        gap-connected region in thisFillGapCellsOut, used to refine the gap group.
 */
static std::unordered_set<Position, PositionHash> keepOnlyGapConnected(
    const std::unordered_set<Position, PositionHash>& remaining,
    const std::unordered_set<Position, PositionHash>& gapSet,
    std::unordered_set<Position, PositionHash>& toConvert,
    const std::vector<std::vector<int>>& cellsToFill,
    int fillIdx,
    const Fill& fill,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    int width,
    int height,
    std::unordered_set<Position, PositionHash>* thisFillGapCellsOut)
{
    auto sameTerrainAt = [&](int x, int y) {
        return WalkableTerrainBoard == nullptr
            || fill.sameWalkableTerrain((*WalkableTerrainBoard)[x][y]);
    };

    // Phase 2: among remaining cells, keep only cells connected to a gap
    // via 4-neighbor dilation connectivity.
    std::unordered_set<Position, PositionHash> gapConnected;
    std::vector<Position> s; // use vector as stack (LIFO via push_back/pop_back)

    // Seed: remaining dilation cells directly adjacent to a gap cell.
    for (const auto& dCell : remaining) {
        bool touchesGap = false;

        for (int d = 0; d < 4; d++) {
            int nextX = static_cast<int>(dCell.first) + dx4[d];
            int nextY = static_cast<int>(dCell.second) + dy4[d];
            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) {
                continue;
            }
            if (!sameTerrainAt(nextX, nextY)) {
                continue;
            }
            Position np{static_cast<size_t>(nextX), static_cast<size_t>(nextY)};
            if (gapSet.count(np)) {
                touchesGap = true;
                break;
            }
        }

        if (touchesGap) {
            gapConnected.insert(dCell);
            s.push_back(dCell);
        }
    }

    std::unordered_set<Position, PositionHash> thisFillGapCells;

    // Flood through remaining dilation cells.
    while (!s.empty()) {
        Position cur = s.back();
        s.pop_back();

        for (int d = 0; d < 4; d++) {
            int nextX = static_cast<int>(cur.first) + dx4[d];
            int nextY = static_cast<int>(cur.second) + dy4[d];
            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) {
                continue;
            }
            if (!sameTerrainAt(nextX, nextY)) {
                continue;
            }

            Position np{static_cast<size_t>(nextX), static_cast<size_t>(nextY)};

            if (cellsToFill[nextX][nextY] == fillIdx || toConvert.count(np)) {
                thisFillGapCells.emplace(cur);
            }
            if (remaining.count(np) && !gapConnected.count(np)) {
                gapConnected.insert(np);
                s.push_back(np);
            }
        }
    }

    // Remaining dilation cells not connected to any gap should convert to fill.
    for (const auto& dCell : remaining) {
        if (!gapConnected.count(dCell)) {
            toConvert.insert(dCell);
            // AK: Keep track of the obstruction it neighbours though? Not implemented here yet.
        }
    }

    if (thisFillGapCellsOut != nullptr) {
        *thisFillGapCellsOut = std::move(thisFillGapCells);
    }

    return gapConnected;
}

// ---------- encodeFillsAndGapsForOutput ----------

/**
 * @brief Iterates the final fill list, encodes each fill's dominant player into result.fillBoard
 *        as a power-of-two bitmask, and collects gap-group cells per dominant player into allGaps.
 *        Fills with no dominant player (dp == 0) contribute to allGaps[0] but are not encoded.
 */
static std::vector<std::vector<Position>> encodeFillsAndGapsForOutput(
    const std::vector<Fill>& fills,
    size_t numPlayers,
    int width,
    int height,
    FillResult& result)
{
    std::vector<std::vector<Position>> allGaps(numPlayers + 1);
    result.fillBoard.assign(width, std::vector<size_t>(height, 0));
    std::vector<std::vector<size_t>> outputForTesting(width, std::vector<size_t>(height, 0));

    size_t fillIdx = 0;
    for (const auto& fill : fills) {
        fillIdx++;
        // get the dominant player of the fill
        size_t dp = fill.getDominantPlayer();

        // save the gaps for later use
        if (dp < allGaps.size()) {
            for (const auto& gg : fill.getGapGroups()) {
                allGaps[dp].insert(allGaps[dp].end(), gg.begin(), gg.end());
            }
        }

        if (dp == 0) {
            continue;
        }

        // encode the fill cells and save
        const size_t encoded_player_value = static_cast<size_t>(1) << (dp - 1);
        for (const auto& cell : fill.getCells()) {
            // outputForTesting[cell.first][cell.second] = fillIdx;
            result.fillBoard[cell.first][cell.second] = encoded_player_value;
        }
    }
    // printBoard(outputForTesting); // For config.testingMode
    
    // DEBUG: Print fillBoard to see encoded player values
    // std::cout << "DEBUG: fillBoard (encoded player values):" << std::endl;
    // printBoard(outputForTesting);

    return allGaps;
}

// ---------- buildDilationComponent ----------

/**
 * @brief BFS over the connected dilation wall component starting from dilPos.
 *        Tracks cells that neighbour a different fill (gapCells) and collects bordering
 *        obstruction cells per owner id into fill's obstructionNeighbours.
 *        Expansion stops when a dilation cell neighbours a different fill (gap boundary).
 */
static void buildDilationComponent(
    const Position& dilPos,
    std::unordered_set<Position, PositionHash>& dilVisited,
    int width,
    int height,
    const std::vector<std::vector<bool>>& dilatedWalls,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    const std::vector<std::vector<int>>& masterPlayerObstructionBoard,
    const std::vector<std::vector<int>>& cellsToFill,
    int fillIdx,
    Fill& fill,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    std::unordered_set<Position, PositionHash>& componentDilCells,
    std::unordered_set<Position, PositionHash>& gapCells)
{
    auto sameTerrainAt = [&](int x, int y) {
        return WalkableTerrainBoard == nullptr
            || fill.sameWalkableTerrain((*WalkableTerrainBoard)[x][y]);
    };

    // Flat vector queue: contiguous allocation, cache-friendly
    std::vector<Position> q;
    q.push_back(dilPos);
    size_t qHead = 0;
    dilVisited.insert(dilPos);

    while (qHead < q.size()) {
        auto top = q[qHead++];
        componentDilCells.insert(top);

        int cellX = static_cast<int>(top.first);
        int cellY = static_cast<int>(top.second);

        bool neighboursFill = false;
        std::vector<Position> cellsToAdd;

        for (int d = 0; d < 4; d++) {
            int nextX = cellX + dx4[d];
            int nextY = cellY + dy4[d];
            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
            if (!sameTerrainAt(nextX, nextY)) continue;

            Position np{static_cast<size_t>(nextX), static_cast<size_t>(nextY)};

            // Keep track of neighbours that are dilated walls
            if (dilatedWalls[np.first][np.second] && !obstructionsBoard[np.first][np.second]
                && !dilVisited.count(np)) {
                cellsToAdd.push_back(np);
            }
            // Keep track if the cell neighbours other fill: not wall, not dilation, not part of this Fill
            else if (!obstructionsBoard[nextX][nextY] && !dilatedWalls[nextX][nextY]
                     && cellsToFill[nextX][nextY] != fillIdx ) {
                gapCells.insert(top);
                neighboursFill = true;
            }
        }

        if(!neighboursFill) {
            for(const auto& el : cellsToAdd) {
                dilVisited.insert(el);
                q.push_back(el);
            }
        }

        // Collect bordering obstruction cells.
        // This is used for optimisation in checkClosureOwner.
        for (int d = 0; d < 4; d++) {
            int nextX = cellX + dx4[d];
            int nextY = cellY + dy4[d];
            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) continue;
            if (obstructionsBoard[nextX][nextY]) {
                // Decode which players own this obstruction from the bitmask
                const int masterObstruction = masterPlayerObstructionBoard[nextX][nextY];
                if (masterObstruction == 0) {
                    // Gaia obstruction
                    fill.addFillObstructionNeighbour(0, nextX, nextY);
                } else if (masterObstruction > 0) {
                    // Extract all player IDs from bitmask: bit position 0 = player 1, bit 1 = player 2, etc.
                    int mask = masterObstruction;
                    // AK: I feel like this can be optimised
                    for (size_t bitPos = 0; bitPos < 32 && mask > 0; ++bitPos) {
                        if (mask & 1) {
                            size_t playerID = bitPos + 1;
                            fill.addFillObstructionNeighbour(playerID, nextX, nextY);
                        }
                        mask >>= 1;
                    }
                }
                // masterObstruction == -1 means no obstruction, which shouldn't happen if obstructionsBoard[nextX][nextY] is true
            }
        }
    }
}

// ---------- gapAnalysisAndDilationConversion ----------

/**
 * @brief For each dilation component attached to fill, determines whether it borders another
 *        fill (gap) or is a dead-end (convert to fill cells).  Dead-end dilation cells are
 *        promoted to fill cells via batchConvertDilationToFill; gap-bordering cells are
 *        partitioned into gap groups and connected-gap sets for later closure analysis.
 */
static void gapAnalysisAndDilationConversion(
    Fill& fill,
    int fillIdx,
    int width,
    int height,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    const std::vector<std::vector<int>>& masterPlayerObstructionBoard,
    std::vector<std::vector<bool>>& dilatedWalls,
    std::vector<std::vector<int>>& cellsToFill,
    std::vector<std::vector<bool>>& visited,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard)
{
    const Config& config = AppConfig::get();

    // Cache the fill's walkable terrain type to avoid repeated dereferences
    const bool hasTerrainBoard = WalkableTerrainBoard != nullptr;
    size_t fillTerrainType = hasTerrainBoard ? 0 : 1; // dummy value if no terrain board
    if (hasTerrainBoard) {
        // We can't easily get the fill's terrain from the Fill class, 
        // so we'll check on first dilation cell. For now, use the lambda approach.
    }

    // Step 3 – Gap detection: explore dilation boundaries of this Fill
    std::unordered_set<Position, PositionHash> dilVisited;
    std::vector<Position> dilToConvert;
    std::unordered_set<Position, PositionHash> obstructionsSeen; // de-duplicate obstruction cells counted for this fill
    auto sameTerrainAt = [&](int x, int y) {
        return !hasTerrainBoard || fill.sameWalkableTerrain((*WalkableTerrainBoard)[x][y]);
    };

    for (const auto& dilPos : fill.getDilationCells()) {
        if (dilVisited.count(dilPos)) {
            continue;
        }

        if (!sameTerrainAt(static_cast<int>(dilPos.first), static_cast<int>(dilPos.second))) {
            continue;
        }

        std::unordered_set<Position, PositionHash> componentDilCells;
        std::unordered_set<Position, PositionHash> gapCells;
        buildDilationComponent(
            dilPos, dilVisited,
            width, height,
            dilatedWalls, obstructionsBoard, masterPlayerObstructionBoard, cellsToFill, fillIdx,
            fill, WalkableTerrainBoard,
            componentDilCells, gapCells);

        if (gapCells.empty()) {
            // Dead-end branch: convert all dilation cells to fill cells
            dilToConvert.insert(dilToConvert.end(),
                componentDilCells.begin(), componentDilCells.end());
                // AK: Keep track of the building though? Not implemented here yet.
        } else {
            if(config.testingMode) {
                
                // create gap cells board for config.testingMode
                std::vector<std::vector<int>> gapCellsBoard(width, std::vector<int>(height, 0));
                std::cout << "Gap detected for fill " << fillIdx << " with " << gapCells.size() << " gap cells." << std::endl;
                std::cout << "Gap cells: " << std::endl;
                for (const auto& gc : gapCells) {
                    gapCellsBoard[gc.first][gc.second] = 1;
                }
                printBoard(gapCellsBoard);
                std::cout << std::endl;
            }
            // Gap found: partition componentDilCells into toConvert / remaining in a
            // single pass rather than two separate loops.
            std::unordered_set<Position, PositionHash> toConvert;
            std::unordered_set<Position, PositionHash> remaining;
            const auto& fillDilationCells = fill.getDilationCells();

            for (const auto& dCell : componentDilCells) {
                if (!gapCells.count(dCell) && fillDilationCells.count(dCell)) {
                    toConvert.insert(dCell);
                } else {
                    remaining.insert(dCell);
                }
            }

            // Phase 2: among remaining cells, keep only those that are gap connected
            // via 4-neighbour dilation connectivity.
            std::unordered_set<Position, PositionHash> thisFillGapCells;
            std::unordered_set<Position, PositionHash> gapConnected = keepOnlyGapConnected(
                remaining,
                gapCells,
                toConvert,
                cellsToFill,
                fillIdx,
                fill,
                WalkableTerrainBoard,
                width,
                height,
                &thisFillGapCells);

            // Persist this connected component for later closure analysis.
            fill.addConnectedGapSet(std::set<Position>(gapConnected.begin(), gapConnected.end()));

            if (!thisFillGapCells.empty()) {
                gapCells = std::move(thisFillGapCells);
            }

            dilToConvert.insert(dilToConvert.end(), toConvert.begin(), toConvert.end());
            fill.addGapGroup(std::vector<Position>(gapCells.begin(), gapCells.end()));
        }
    }

    // Apply conversions: dilation cells to fill cells
    fill.batchConvertDilationToFill(
        dilToConvert, plLocalTerritories, cellsToFill, dilatedWalls, visited, fillIdx);
}

// ---------- floodFill ----------

/**
 * @brief BFS flood fill seeded from (startX, startY), expanding into cells that are not
 *        obstructions or dilation walls.  Accumulates cells-to-register, dilation neighbours,
 *        and map-edge touches in batch buffers, then flushes them via
 *        batchRegisterCellsAndMapEdgeCells.
 */
static void floodFill(
    int startX,
    int startY,
    int width,
    int height,
    Fill& fill,
    int fillIdx,
    std::vector<std::vector<bool>>& visited,
    std::vector<std::vector<int>>& cellsToFill,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    const std::vector<std::vector<bool>>& dilatedWalls,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<bool>* defeatedPlayers)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    // Flat vector queue: contiguous memory gives better cache locality than
    // std::queue (deque-backed) which allocates in discontiguous chunks.
    std::vector<Position> q;
    q.reserve(2048);
    q.push_back({static_cast<size_t>(startX), static_cast<size_t>(startY)});
    size_t head = 0;

    // Batch buffers flushed after the BFS loop to keep the hot path free of
    // scattered Fill mutations.
    std::vector<std::pair<int, int>> mapEdgeCellsToAdd;
    std::vector<Position> dilationCellsToAdd;

    visited[startX][startY] = true;
    if (WalkableTerrainBoard != nullptr) {
        fill.setWalkableBool(static_cast<bool>((*WalkableTerrainBoard)[startX][startY]));
    } else {
        fill.setWalkableBool(true); // default to walkable if no terrain board provided
    }

    // Hoist the null check out of the hot loop — it is constant for the whole BFS.
    const bool hasDefeatedPlayers = (defeatedPlayers != nullptr);

    while (head < q.size()) {
        const auto cur = q[head++];
        const size_t cellX = cur.first;
        const size_t cellY = cur.second;

        // Check 4-neighbors
        for (int d = 0; d < 4; d++) {
            int nextX = static_cast<int>(cellX) + dx4[d];
            int nextY = static_cast<int>(cellY) + dy4[d];

            if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) {
                // Record that this fill touches the map edge
                mapEdgeCellsToAdd.push_back({nextX, nextY});
                continue;
            }
            if (WalkableTerrainBoard != nullptr && !fill.sameWalkableTerrain((*WalkableTerrainBoard)[nextX][nextY])) {
                continue; // block expansion into different terrain type
            }

            // Record adjacent dilation cells (excluding original obstructions)
            if (dilatedWalls[nextX][nextY] && !obstructionsBoard[nextX][nextY]) {
                dilationCellsToAdd.push_back({static_cast<size_t>(nextX), static_cast<size_t>(nextY)});
            }

            // Expand BFS into non-obstruction, non-dilation, unvisited cells
            if (!obstructionsBoard[nextX][nextY] && !dilatedWalls[nextX][nextY] && !visited[nextX][nextY]) {
                visited[nextX][nextY] = true;
                q.push_back({static_cast<size_t>(nextX), static_cast<size_t>(nextY)});
            }
        }
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "TIMING: flood fill time taken: " << duration.count() << "ms" << std::endl;

// --- Batch flush ---
measureAndLogExecutionTime<int>("Step 2b. batch register cells and map edge cells for one fill",
    std::function<int()>([&]() {    
    fill.batchRegisterCellsAndMapEdgeCells(
        hasDefeatedPlayers,
        std::move(q),
        plLocalTerritories,
        mapEdgeCellsToAdd,
        dilationCellsToAdd,
        defeatedPlayers,
        cellsToFill,
        fillIdx);
    return 0;
    }) );
    
}

// ---------- initialise ----------

/**
 * @brief Validates the input board and initialises all working arrays for a fill analysis run:
 *        dimensions, obstruction boards (resized to match board dimensions), merged obstruction
 *        board, visited flags, cellsToFill, and per-player weight counters.
 * @return false if the board is empty.
 */
static bool initialise(
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    int& width,
    int& height,
    std::vector<std::vector<std::vector<bool>>>& localObstructionBoards,
    std::vector<std::vector<bool>>& obstructionsBoard,
    std::vector<std::vector<bool>>& visited,
    std::vector<std::vector<int>>& cellsToFill,
    std::unordered_map<size_t, double>& playerWeightCount)
{
    if (plLocalTerritories.empty()) {
        return false;
    }

    width = static_cast<int>(plLocalTerritories.size());
    height = static_cast<int>(plLocalTerritories[0].size());
    localObstructionBoards = playerObstructionBoards;
    obstructionsBoard.assign(width, std::vector<bool>(height, false));
    visited.assign(width, std::vector<bool>(height, false));
    cellsToFill.assign(width, std::vector<int>(height, -1));

    // Build a local ownership board for safe bounds-checked ownership queries.
    if (localObstructionBoards.empty()) {
        localObstructionBoards.resize(1, std::vector<std::vector<bool>>(width, std::vector<bool>(height, false)));
    }

    // Ensure all ownership layers match board dimensions.
    for (auto& board : localObstructionBoards) {
        if (static_cast<int>(board.size()) != width) {
            board.resize(width, std::vector<bool>(height, false));
        }
        for (auto& xColumn : board) {
            if (static_cast<int>(xColumn.size()) != height) {
                xColumn.resize(height, false);
            }
        }
    }

    // Merge all obstruction layers into one board for fill analysis.
    for (const auto& board : localObstructionBoards) {
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                obstructionsBoard[x][y] = obstructionsBoard[x][y] || board[x][y];
            }
        }
    }

    for (size_t p = 1; p <= numPlayers; p++) {
        playerWeightCount[p] = 0.0;
    }

    return true;
}

// ---------- completeBoardFill ----------

/**
 * @brief Sweeps every non-obstruction, non-dilation, unvisited cell and seeds a flood fill from
 *        each, creating a new Fill object per connected region (step 2).  Immediately follows
 *        each flood fill with gap analysis and dilation conversion (step 3).
 */
static void completeBoardFill(
    std::vector<Fill>& fills,
    int width,
    int height,
    std::vector<std::vector<bool>>& visited,
    std::vector<std::vector<int>>& cellsToFill,
    const std::vector<std::vector<double>>& plLocalTerritories,
    const std::vector<std::vector<bool>>& obstructionsBoard,
    const std::vector<std::vector<int>>& masterPlayerObstructionBoard,
    std::vector<std::vector<bool>>& dilatedWalls,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<bool>* defeatedPlayers)
{
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (obstructionsBoard[x][y] || dilatedWalls[x][y] || visited[x][y]) {
                continue;
            }

            Fill fill(width, height);
            int fillIdx = static_cast<int>(fills.size());
            measureAndLogExecutionTime<int>("Step 2. flood fill for one component",
                std::function<int()>([&]() {
            floodFill(x, y, width, height, fill, fillIdx, visited, cellsToFill,
                      plLocalTerritories, obstructionsBoard, dilatedWalls,
                      WalkableTerrainBoard, defeatedPlayers);
                      return 0;
                }) );

            // Step 3 – Gap detection and dilation conversion
            measureAndLogExecutionTime<int>("Step 3. gap analysis and dilation conversion for one fill",
                std::function<int()>([&]() {
            gapAnalysisAndDilationConversion(
                fill, fillIdx, width, height,
                plLocalTerritories, obstructionsBoard, masterPlayerObstructionBoard,
                dilatedWalls, cellsToFill, visited,
                WalkableTerrainBoard);
                return 0;
            }) );

            fills.push_back(std::move(fill));
        }
    }
}

// ---------- initialiseFill ----------

/**
 * @brief Full fill analysis entry point.  Executes all seven pipeline steps:
 *        1.     Dilate obstruction and terrain boundaries.
 *        2.     BFS flood fill to partition the walkable board into Fill regions.
 *        3.     Gap detection and dilation conversion per fill.
 *        3b.    Materialise connected-gap bridge fills.
 *        4.     Merge fills that share gap groups with the same dominant player.
 *        5.     Discard fills exceeding the gap-group threshold.
 *        6/7.   Encode dominant-player ownership into fillBoard and collect gap groups.
 * @return FillResult containing fills, gaps, fillBoard, and fillAssignmentBoard.
 */
FillResult initialiseFill(
    const std::vector<std::vector<double>>& plLocalTerritories, 
    const std::vector<std::vector<int>>& masterPlayerObstructionBoard,
    const std::vector<std::vector<std::vector<bool>>>& playerObstructionBoards,
    size_t numPlayers,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<bool>* defeatedPlayers)
{
    // Initialise
    const Config& config = AppConfig::get();

    FillResult result;
    int width = 0;
    int height = 0;
    std::vector<std::vector<std::vector<bool>>> localObstructionBoards;
    std::vector<std::vector<bool>> obstructionsBoard;
    std::vector<std::vector<bool>> visited;
    std::vector<std::vector<int>> cellsToFill;
    std::unordered_map<size_t, double> playerWeightCount;

    if (!initialise(
        plLocalTerritories,
        playerObstructionBoards,
        numPlayers,
        width,
        height,
        localObstructionBoards,
        obstructionsBoard,
        visited,
        cellsToFill,
        playerWeightCount)) {
        return result;
    }

    // Steps 1, 1b, 1c – Dilate obstructions and terrain boundaries
    std::vector<std::vector<bool>> dilatedWalls = measureAndLogExecutionTime<std::vector<std::vector<bool>>>("Step 1. dilate obstr and terrains", 
        std::function<std::vector<std::vector<bool>>()>([&]() { return dilateObstructionsAndTerrain(obstructionsBoard, WalkableTerrainBoard, width, height); })
    );

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
    measureAndLogExecutionTime<int>("Step 2. Complete board fill",
        std::function<int()> ([&]() {
    completeBoardFill(
        fills,
        width,
        height,
        visited,
        cellsToFill,
        plLocalTerritories,
        obstructionsBoard,
        masterPlayerObstructionBoard,
        dilatedWalls,
        WalkableTerrainBoard,
        defeatedPlayers);

        return 0;
    } ) );

    // Step 3b – Materialise each connected-gap set as its own Fill object.
    measureAndLogExecutionTime<int>("Step 3b. materialise connected-gap sets",
        std::function<int()>([&]() { fillConnectedGapSets(fills, cellsToFill, plLocalTerritories, WalkableTerrainBoard, width, height); return 0; })
    );
    // dilatedWalls is intentionally not returned or persisted. After fillConnectedGapSets()
    // runs, connectedGapSets_ is emptied on all fills (gap-connected cells promoted to bridge
    // fill cells_). Remaining dilatedWalls==true cells are exactly the bridge fill cells,
    // recoverable as fillAssignmentBoard >= 0 at those positions.

    // Step 4 – Merge Fills that share gap groups with the same dominant player
    measureAndLogExecutionTime<int>("Step 4. merge fills",
        std::function<int()>([&]() { mergeFills(fills, cellsToFill, localObstructionBoards, obstructionsBoard, WalkableTerrainBoard, dilatedWalls, width, height, numPlayers); return 0; })
    );

    
    std::vector<std::vector<int>> outputForTesting(width, std::vector<int>(height, 0));
    size_t Idx = 0;
    for (const auto& fill : fills) {
        for (const auto& cell : fill.getCells()) {
            outputForTesting[cell.first][cell.second] = Idx;
        }
        ++Idx;
    }
    Idx = 1;
    for (const auto& plObstr : playerObstructionBoards) {
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                if (plObstr[x][y]) {
                    outputForTesting[x][y] = -Idx; // Mark obstructions with a distinct value
                }
            }
        }
        Idx++;
    }
    // printBoard(outputForTesting); // For config.testingMode

    // Step 5 – Gap constraint: discard fills with more than 2 gap groups
    measureAndLogExecutionTime<int>("Step 5. remove shapes by gap threshold",
        std::function<int()>([&]() { removeShapesByGapThreshold(fills); return 0; })
    );

    // Step 6 and Step 7 – Collect gaps and encode dominant player fills
    std::vector<std::vector<Position>> allGaps = measureAndLogExecutionTime<std::vector<std::vector<Position>>>("Step 6/7. collect gaps and encode fills",
        std::function<std::vector<std::vector<Position>>()>([&]() { return encodeFillsAndGapsForOutput(fills, numPlayers, width, height, result); })
    );

    
    if (config.testingMode) {
    // {
        
        // merge the obstructions and dilations together for config.testingMode
        std::vector<std::vector<int>> obsDilBoard(width, std::vector<int>(height, 0));
        std::vector<std::vector<int>> obsDilFillBoard(width, std::vector<int>(height, 0));
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                for (int player = 0; player < static_cast<int>(localObstructionBoards.size()); ++player) {
                    int value;
                    if(player == 0) {value = 9;}
                    if(player > 0) {value = player;}
                    obsDilBoard[x][y] += -value * localObstructionBoards[player][x][y];
                }
                obsDilBoard[x][y] -= 8 * dilatedWalls[x][y];
                obsDilFillBoard[x][y] = (obsDilBoard[x][y] != 0) ? obsDilBoard[x][y] : result.fillBoard[x][y];
            }
        }
        
        // std::cout << "Obstructions Board:" << std::endl;
        // printBoard(obstructionsBoard); // For config.testingMode
        // std::cout << std::endl;

        std::cout << "Obsdillfill Board:" << std::endl;
        printBoard(obsDilFillBoard); // For config.testingMode
        std::cout << std::endl;
        

        // std::vector<std::vector<int>> testGapsBoardTeam(config.mapSize, std::vector<int>(config.mapSize, -1));
        // const auto& gapBoardTeam = allGaps;
        // for(int team = 0; team < gapBoardTeam.size(); ++team) {
        //     for(int cell = 0; cell < gapBoardTeam[team].size(); ++cell) {
        //         // Process each gap cell
        //         const auto& gap = gapBoardTeam[team][cell];
        //         testGapsBoardTeam[gap.first][gap.second] = team; // Mark the gap cell with the team number (or any other value you want)
        //     }
        // }  

        // std::cout << "gap board" << std::endl;
        // printBoard(testGapsBoardTeam, 0);
        // std::cout << std::endl;

        // std::cout << "Player Local Territories Board:" << std::endl;
        // printBoard(plLocalTerritories); // For config.testingMode
        // std::cout << std::endl;
    }

    result.fills = std::move(fills);
    result.gaps = std::move(allGaps);
    result.fillAssignmentBoard = std::move(cellsToFill);
    return result;
}


// ---------- updateFill ----------

/**
 * @brief Performs an incremental fill update after a single building is added or removed.
 *
 * Fast path (isFastPath = true): for an "add", evicts any footprint cells from the
 * fills they currently belong to and zeros out their fillBoard/fillAssignmentBoard
 * entries; for a "remove", (a) all fills overlapping the influence bbox have their
 * per-player counts refreshed via the territory delta pass, and (b) the freed
 * footprint cells are registered into the single fill that surrounds them (identified
 * by scanning the Chebyshev-1 ring around the footprint in fillAssignmentBoard),
 * with fillAssignmentBoard and fillBoard updated accordingly.
 *
 * Slow path (isFastPath = false): falls back to a full initialiseFill() recomputation
 * to handle topology changes (fill splits, merges, and new closures).
 *
 * @param footprintX/Y/W/H    Position and size of the changed building footprint.
 * @param influenceExtent      Chebyshev radius used for eviction boundary checks.
 * @param isFastPath           True if no other building is within Chebyshev-2 of footprint.
 * @param mod                  "add" or "remove".
 * @param fills                Current Fill objects (passed by value; modified for fast path).
 * @param fillAssignmentBoard  Cell->fill-index map (-1 = unclaimed); modified for fast path.
 * @param fillBoard            Per-cell encoded ownership board; modified for fast path.
 * @param gaps                 Current gap groups (returned unchanged for fast path).
 * @param plLocalTerritories   Territory influence values (forwarded to slow-path initialiseFill).
 * @return Updated FillResult with consistent fills, gaps, fillBoard, fillAssignmentBoard.
 */
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
    const std::vector<size_t>& dsuGroupBounds,
    const std::vector<std::vector<size_t>>* WalkableTerrainBoard,
    const std::vector<bool>* defeatedPlayers)
{
    if (!isFastPath) {
        // Slow path: reprocess only fills that overlap the DSU group bounds.
        // The DSU group bounds cover the bounding box of all buildings in the
        // connected cluster that the modified building belongs to.  Only fills
        // inside that region can have changed topology; everything outside is untouched.

        const int width  = static_cast<int>(masterObstructionBoard.size());
        const int height = width > 0 ? static_cast<int>(masterObstructionBoard[0].size()) : 0;

        // Fall back to full recompute if board dimensions are degenerate or bounds are missing.
        if (width == 0 || height == 0 || dsuGroupBounds.size() < 4) {
            return initialiseFill(plLocalTerritories, masterObstructionBoard,
                                  playerObstructionBoards, numPlayers,
                                  WalkableTerrainBoard, defeatedPlayers);
        }

        // Step 1: Build merged bool obstruction board from masterObstructionBoard
        // (same merge that `initialise` does from per-player boards).
        std::vector<std::vector<bool>> obstrBoard(width, std::vector<bool>(height, false));
        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y)
                obstrBoard[x][y] = (masterObstructionBoard[x][y] != -1);

        // Step 2: Dilate obstruction and terrain boundaries (mirrors initialiseFill step 1).
        std::vector<std::vector<bool>> dilatedWalls =
            dilateObstructionsAndTerrain(obstrBoard, WalkableTerrainBoard, width, height);

        // Step 3: Clamp the DSU group bounds to the valid map range.
        const int regionMinX = static_cast<int>(std::min(dsuGroupBounds[0],
                                    static_cast<size_t>(std::max(0, width  - 1))));
        const int regionMaxX = static_cast<int>(std::min(dsuGroupBounds[1],
                                    static_cast<size_t>(std::max(0, width  - 1))));
        const int regionMinY = static_cast<int>(std::min(dsuGroupBounds[2],
                                    static_cast<size_t>(std::max(0, height - 1))));
        const int regionMaxY = static_cast<int>(std::min(dsuGroupBounds[3],
                                    static_cast<size_t>(std::max(0, height - 1))));

        if (regionMinX > regionMaxX || regionMinY > regionMaxY) {
            return initialiseFill(plLocalTerritories, masterObstructionBoard,
                                  playerObstructionBoards, numPlayers,
                                  WalkableTerrainBoard, defeatedPlayers);
        }

        // Step 4: Identify fills whose bounding boxes overlap the reprocessing region.
        std::vector<int> toRemoveIndices;
        for (int i = 0; i < static_cast<int>(fills.size()); ++i) {
            const auto& b = fills[i].getBounds(); // {minX, maxX, minY, maxY}
            if (b[1] >= regionMinX && b[0] <= regionMaxX &&
                b[3] >= regionMinY && b[2] <= regionMaxY) {
                toRemoveIndices.push_back(i);
            }
        }

        // Step 5: Clear those fills' cells from fillAssignmentBoard and fillBoard.
        for (const int idx : toRemoveIndices) {
            for (const auto& cell : fills[idx].getCells()) {
                fillAssignmentBoard[cell.first][cell.second] = -1;
                fillBoard[cell.first][cell.second] = 0;
            }
        }

        // Step 6: Remove cleared fills from the fills vector; remap surviving fill indices
        // in fillAssignmentBoard so they remain consistent with the compacted vector.
        {
            std::sort(toRemoveIndices.begin(), toRemoveIndices.end());
            std::vector<int> indexMap(fills.size(), -1);
            int newIdx = 0;
            {
                int removePtr = 0;
                for (int i = 0; i < static_cast<int>(fills.size()); ++i) {
                    if (removePtr < static_cast<int>(toRemoveIndices.size()) &&
                        toRemoveIndices[removePtr] == i) {
                        ++removePtr;
                    } else {
                        indexMap[i] = newIdx++;
                    }
                }
            }
            // Remap fillAssignmentBoard for surviving fills.
            for (int x = 0; x < width; ++x) {
                for (int y = 0; y < height; ++y) {
                    const int old = fillAssignmentBoard[x][y];
                    if (old >= 0 && old < static_cast<int>(indexMap.size())) {
                        fillAssignmentBoard[x][y] = indexMap[old];
                    }
                }
            }
            // Compact the fills vector.
            std::vector<Fill> remaining;
            remaining.reserve(fills.size() - toRemoveIndices.size());
            for (int i = 0; i < static_cast<int>(fills.size()); ++i) {
                if (indexMap[i] != -1)
                    remaining.push_back(std::move(fills[i]));
            }
            fills = std::move(remaining);
        }

        // Step 7: Build visited board from the updated fillAssignmentBoard.
        // Cells that already belong to surviving fills are marked visited so that
        // the new flood fills do not expand into unchanged territory.
        std::vector<std::vector<bool>> visited(width, std::vector<bool>(height, false));
        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y)
                visited[x][y] = (fillAssignmentBoard[x][y] != -1);

        // Step 8: Sweep the reprocessing region for unvisited, non-obstruction,
        // non-dilation cells.  Each becomes the seed for a new flood fill
        // (instead of seeding from one of the old fill's cells, we use
        // completeBoardFill-style discovery but restricted to the DSU region).
        for (int x = regionMinX; x <= regionMaxX; ++x) {
            for (int y = regionMinY; y <= regionMaxY; ++y) {
                if (obstrBoard[x][y] || dilatedWalls[x][y] || visited[x][y]) continue;
                const int newFillIdx = static_cast<int>(fills.size());
                Fill newFill(width, height);
                floodFill(x, y, width, height, newFill, newFillIdx, visited,
                          fillAssignmentBoard, plLocalTerritories, obstrBoard, dilatedWalls,
                          WalkableTerrainBoard, defeatedPlayers);
                gapAnalysisAndDilationConversion(
                    newFill, newFillIdx, width, height,
                    plLocalTerritories, obstrBoard, masterObstructionBoard,
                    dilatedWalls, fillAssignmentBoard, visited,
                    WalkableTerrainBoard);
                fills.push_back(std::move(newFill));
            }
        }

        // Step 9: Run the same post-fill pipeline as initialiseFill (steps 3b–5).
        // fillConnectedGapSets is safe to call on the whole fills vector because
        // connectedGapSets_ is empty on surviving fills (already consumed earlier).
        fillConnectedGapSets(fills, fillAssignmentBoard, plLocalTerritories,
                             WalkableTerrainBoard, width, height);
        mergeFills(fills, fillAssignmentBoard, playerObstructionBoards,  obstrBoard,
                   WalkableTerrainBoard, dilatedWalls, width, height, numPlayers);
        removeShapesByGapThreshold(fills);

        // Step 10: Encode fills and gaps for output.
        FillResult result;
        std::vector<std::vector<Position>> allGaps =
            encodeFillsAndGapsForOutput(fills, numPlayers, width, height, result);
        result.fills               = std::move(fills);
        result.gaps                = std::move(allGaps);
        result.fillAssignmentBoard = std::move(fillAssignmentBoard);
        return result;
    }

    // Fast path: topology cannot change near an isolated footprint.
    // "add":    footprint cells become obstructions; evict them from any fill they belonged to.
    // "remove": freed cells stay unclaimed — no fill boundary nearby to absorb them.
    //           However, removing the building's influence can shift territory bitmask values
    //           for cells already in nearby fills.  Those fills have no footprint cells to evict,
    //           but still need their per-player counts refreshed via the influence-area delta pass.
    if (mod == "add") {
        const int boardW = static_cast<int>(fillAssignmentBoard.size());
        std::unordered_set<int> touchedFillIndices;
        for (size_t cx = footprintX; cx < footprintX + footprintW; ++cx) {
            if (static_cast<int>(cx) >= boardW) break;
            const int boardH = static_cast<int>(fillAssignmentBoard[cx].size());
            for (size_t cy = footprintY; cy < footprintY + footprintH; ++cy) {
                if (static_cast<int>(cy) >= boardH) break;
                const int idx = fillAssignmentBoard[cx][cy];
                if (idx >= 0) {
                    touchedFillIndices.insert(idx);
                    fillAssignmentBoard[cx][cy] = -1;
                }
                fillBoard[cx][cy] = 0;
            }
        }
        for (const int idx : touchedFillIndices) {
            if (idx >= static_cast<int>(fills.size())) continue;
            fills[static_cast<size_t>(idx)].evictCellsInFootprint(
                footprintX, footprintY, footprintW, footprintH,
                influenceExtent, plLocalTerritories);
        }
    } else {
        // "remove" fast path:
        // (a) Fills overlapping the influence bbox may have cells whose territory bitmask
        //     values changed now that the building's influence is gone.  Apply the delta.
        // (b) The freed footprint cells now have territory values and must be registered
        //     into the single fill that surrounded the footprint (if any).
        const int boardW = static_cast<int>(fillAssignmentBoard.size());
        const int boardH = boardW > 0 ? static_cast<int>(fillAssignmentBoard[0].size()) : 0;

        // (a) Influence-area delta pass.
        const int iinfX    = static_cast<int>(
            (footprintX > influenceExtent) ? footprintX - influenceExtent : 0);
        const int iinfY    = static_cast<int>(
            (footprintY > influenceExtent) ? footprintY - influenceExtent : 0);
        const int iinfXEnd = static_cast<int>(footprintX + footprintW + influenceExtent);
        const int iinfYEnd = static_cast<int>(footprintY + footprintH + influenceExtent);
        for (size_t i = 0; i < fills.size(); ++i) {
            const auto& b = fills[i].getBounds(); // {minX, maxX, minY, maxY}
            if (b[1] < iinfX || b[0] > iinfXEnd ||
                b[3] < iinfY || b[2] > iinfYEnd) continue;
            fills[i].evictCellsInFootprint(
                footprintX, footprintY, footprintW, footprintH,
                influenceExtent, plLocalTerritories);
        }

        // (b) Find the fill surrounding the footprint by scanning its Chebyshev-1 ring.
        const int fpX  = static_cast<int>(footprintX);
        const int fpY  = static_cast<int>(footprintY);
        const int fpXE = static_cast<int>(footprintX + footprintW);
        const int fpYE = static_cast<int>(footprintY + footprintH);
        int surroundingFillIdx = -1;

        // Top and bottom rows of the ring.
        for (int cx = std::max(0, fpX - 1); cx <= std::min(boardW - 1, fpXE) && surroundingFillIdx < 0; ++cx) {
            if (fpY - 1 >= 0) {
                const int idx = fillAssignmentBoard[cx][fpY - 1];
                if (idx >= 0) surroundingFillIdx = idx;
            }
            if (surroundingFillIdx < 0 && fpYE < boardH) {
                const int idx = fillAssignmentBoard[cx][fpYE];
                if (idx >= 0) surroundingFillIdx = idx;
            }
        }
        // Left and right columns of the ring.
        for (int cy = std::max(0, fpY - 1); cy <= std::min(boardH - 1, fpYE) && surroundingFillIdx < 0; ++cy) {
            if (fpX - 1 >= 0) {
                const int idx = fillAssignmentBoard[fpX - 1][cy];
                if (idx >= 0) surroundingFillIdx = idx;
            }
            if (surroundingFillIdx < 0 && fpXE < boardW) {
                const int idx = fillAssignmentBoard[fpXE][cy];
                if (idx >= 0) surroundingFillIdx = idx;
            }
        }

        if (surroundingFillIdx >= 0 && surroundingFillIdx < static_cast<int>(fills.size())) {
            Fill& sf = fills[static_cast<size_t>(surroundingFillIdx)];
            const int plW = static_cast<int>(plLocalTerritories.size());
            for (size_t cx = footprintX; cx < footprintX + footprintW; ++cx) {
                if (static_cast<int>(cx) >= boardW) break;
                const int plH = static_cast<int>(cx) < plW
                    ? static_cast<int>(plLocalTerritories[cx].size()) : 0;
                const int bH = static_cast<int>(fillAssignmentBoard[cx].size());
                for (size_t cy = footprintY; cy < footprintY + footprintH; ++cy) {
                    if (static_cast<int>(cy) >= bH) break;
                    const double val = (static_cast<int>(cx) < plW && static_cast<int>(cy) < plH)
                        ? plLocalTerritories[cx][cy] : 0.0;
                    sf.registerCell(cx, cy, val);
                    fillAssignmentBoard[cx][cy] = surroundingFillIdx;
                }
            }
            // Encode fillBoard for the freed cells based on the fill's updated dominant player.
            const size_t dp = sf.getDominantPlayer();
            const size_t encoded = (dp > 0) ? (static_cast<size_t>(1) << (dp - 1)) : 0;
            for (size_t cx = footprintX; cx < footprintX + footprintW; ++cx) {
                if (static_cast<int>(cx) >= boardW) break;
                const int bH = static_cast<int>(fillBoard[cx].size());
                for (size_t cy = footprintY; cy < footprintY + footprintH; ++cy) {
                    if (static_cast<int>(cy) >= bH) break;
                    fillBoard[cx][cy] = encoded;
                }
            }
        }
    }

    FillResult result;
    result.fills               = std::move(fills);
    result.gaps                = std::move(gaps);
    result.fillBoard           = std::move(fillBoard);
    result.fillAssignmentBoard = std::move(fillAssignmentBoard);
    return result;
}
