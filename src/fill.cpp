#include "fill.h"

#include <queue>
#include <stack>
#include <algorithm>
#include <numeric>
#include <unordered_map>

// ---------- Fill methods ----------

void Fill::addCell(size_t row, size_t col) {
    cells_.emplace_back(row, col);
}

void Fill::addGapGroup(const std::vector<Position>& group) {
    gapGroups_.push_back(group);
}

void Fill::addDilationCell(size_t row, size_t col) {
    dilationCells_.emplace(row, col);
}

void Fill::addPlayerCell(size_t playerID) {
    playerCount_[playerID]++;
}

void Fill::merge(const Fill& other) {
    cells_.insert(cells_.end(), other.cells_.begin(), other.cells_.end());
    gapGroups_.insert(gapGroups_.end(), other.gapGroups_.begin(), other.gapGroups_.end());
    dilationCells_.insert(other.dilationCells_.begin(), other.dilationCells_.end());
    for (const auto& kv : other.playerCount_) {
        playerCount_[kv.first] += kv.second;
    }
}

size_t Fill::getDominantPlayer() const {
    size_t maxCount = 0, dominant = 0;
    for (const auto& kv : playerCount_) {
        if (kv.second > maxCount) {
            maxCount = kv.second;
            dominant = kv.first;
        }
    }
    return dominant;
}

const std::vector<Position>& Fill::getCells() const { return cells_; }
const std::vector<std::vector<Position>>& Fill::getGapGroups() const { return gapGroups_; }
const std::set<Position>& Fill::getDilationCells() const { return dilationCells_; }
const std::unordered_map<size_t, size_t>& Fill::getPlayerCount() const { return playerCount_; }
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

} // anonymous namespace

// ---------- analyseFill ----------

FillResult analyseFill(
    const std::vector<std::vector<double>>& board_weights,
    const std::vector<std::vector<size_t>>& board_edges)
{
    FillResult result;
    if (board_weights.empty()) return result;

    const int rows = static_cast<int>(board_weights.size());
    const int cols = static_cast<int>(board_weights[0].size());

    // Step 0 – Initialization
    std::vector<std::vector<bool>> originalWall(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<bool>> dilatedWall(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));
    std::vector<std::vector<int>>  cellToFill(rows, std::vector<int>(cols, -1));

    // Step 1 – Identify original walls from edge board
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            if (board_edges[i][j] != 0)
                originalWall[i][j] = true;

    // Step 1 – Dilate walls: mark 8-directional neighbors of each wall cell
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (!originalWall[i][j]) continue;
            for (int d = 0; d < 8; d++) {
                int ni = i + dx8[d], nj = j + dy8[d];
                if (ni >= 0 && ni < rows && nj >= 0 && nj < cols && !originalWall[ni][nj])
                    dilatedWall[ni][nj] = true;
            }
        }
    }

    // Step 2 – BFS flood fill to create Fill regions
    std::vector<Fill> fills;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            if (originalWall[i][j] || dilatedWall[i][j] || visited[i][j])
                continue;

            Fill fill;
            int fillIdx = static_cast<int>(fills.size());
            std::queue<Position> q;
            q.push({static_cast<size_t>(i), static_cast<size_t>(j)});
            visited[i][j] = true;

            while (!q.empty()) {
                auto cur = q.front();
                q.pop();
                size_t r = cur.first, c = cur.second;

                fill.addCell(r, c);
                cellToFill[r][c] = fillIdx;

                // Decode player ownership (powers-of-2 encoding)
                size_t val = static_cast<size_t>(board_weights[r][c]);
                for (size_t p = 1; val > 0; p++, val >>= 1) {
                    if (val & 1) fill.addPlayerCell(p);
                }

                // Check 8-neighbors
                for (int d = 0; d < 8; d++) {
                    int nr = static_cast<int>(r) + dx8[d];
                    int nc = static_cast<int>(c) + dy8[d];
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;

                    // Record adjacent dilation cells (excluding original walls)
                    if (dilatedWall[nr][nc] && !originalWall[nr][nc])
                        fill.addDilationCell(static_cast<size_t>(nr), static_cast<size_t>(nc));

                    // Expand BFS into non-wall, non-dilation, unvisited cells
                    if (!originalWall[nr][nc] && !dilatedWall[nr][nc] && !visited[nr][nc]) {
                        visited[nr][nc] = true;
                        q.push({static_cast<size_t>(nr), static_cast<size_t>(nc)});
                    }
                }
            }

            // Step 3 – Gap detection: explore dilation boundaries of this Fill
            std::set<Position> dilVisited;
            for (const auto& dilPos : fill.getDilationCells()) {
                if (dilVisited.count(dilPos)) continue;

                std::vector<Position> gapCells;
                std::set<Position> gapSeen;
                std::stack<Position> stk;
                stk.push(dilPos);
                dilVisited.insert(dilPos);

                while (!stk.empty()) {
                    auto top = stk.top();
                    stk.pop();
                    int dr = static_cast<int>(top.first);
                    int dc = static_cast<int>(top.second);

                    for (int d = 0; d < 8; d++) {
                        int ndr = dr + dx8[d];
                        int ndc = dc + dy8[d];
                        if (ndr < 0 || ndr >= rows || ndc < 0 || ndc >= cols) continue;

                        Position np{static_cast<size_t>(ndr), static_cast<size_t>(ndc)};

                        // Continue through connected dilation cells
                        if (dilatedWall[ndr][ndc] && !originalWall[ndr][ndc]
                            && !dilVisited.count(np)) {
                            dilVisited.insert(np);
                            stk.push(np);
                        }
                        // Gap cell: not wall, not dilation, not part of this Fill
                        else if (!originalWall[ndr][ndc] && !dilatedWall[ndr][ndc]
                                 && cellToFill[ndr][ndc] != fillIdx
                                 && !gapSeen.count(np)) {
                            gapSeen.insert(np);
                            gapCells.push_back(np);
                        }
                    }
                }

                if (!gapCells.empty())
                    fill.addGapGroup(gapCells);
            }

            fills.push_back(std::move(fill));
        }
    }

    // Step 5 – Merge Fills that share gap groups with the same dominant player
    {
        int n = static_cast<int>(fills.size());
        UnionFind uf(n);

        for (int fi = 0; fi < n; fi++) {
            for (const auto& gg : fills[fi].getGapGroups()) {
                for (const auto& gp : gg) {
                    int other = cellToFill[gp.first][gp.second];
                    if (other >= 0 && other != fi
                        && fills[fi].getDominantPlayer() != 0
                        && fills[fi].getDominantPlayer() == fills[other].getDominantPlayer()) {
                        uf.unite(fi, other);
                    }
                }
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

    // Step 6 – Gap constraint: discard fills with more than 2 gap groups
    {
        std::vector<Fill> filtered;
        filtered.reserve(fills.size());
        for (auto& f : fills) {
            if (f.numGapGroups() <= 2)
                filtered.push_back(std::move(f));
        }
        fills = std::move(filtered);
    }

    // Step 7 – Collect all remaining gap groups
    std::vector<std::vector<Position>> allGaps;
    for (const auto& f : fills) {
        for (const auto& gg : f.getGapGroups())
            allGaps.push_back(gg);
    }

    // Build fill board: encode dominant player per cell (powers-of-2)
    result.fillBoard.assign(rows, std::vector<size_t>(cols, 0));
    for (size_t fi = 0; fi < fills.size(); fi++) {
        size_t dp = fills[fi].getDominantPlayer();
        if (dp == 0) continue;
        size_t encoded = static_cast<size_t>(1) << (dp - 1);
        for (const auto& cell : fills[fi].getCells())
            result.fillBoard[cell.first][cell.second] = encoded;
    }

    result.fills = std::move(fills);
    result.gaps = std::move(allGaps);
    return result;
}


