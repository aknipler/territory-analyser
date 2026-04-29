

#include "../include/territory_analyser.h"
#include "../include/header.h"
// #include "grid.h"

#include <cstdlib>
#include <limits>
#include <queue>

// Constructor to initialize the dynamic 2D array
TerritoryAnalyser::TerritoryAnalyser(size_t givenSize, size_t numPlayers, size_t numTeams, std::map<int, int> teamAssignments, size_t threshold) 
    : playerGrids(numPlayers + 1, Grid(givenSize)),
    teamGrids(numTeams + 1, Grid(givenSize)),
    size(givenSize), numPlayers(numPlayers), numTeams(numTeams), teamAssignments(teamAssignments), threshold(threshold), seed(std::chrono::high_resolution_clock::now().time_since_epoch().count()), gaia_board(givenSize, std::vector<size_t>(givenSize, 0)),

    masterPlayerObstructionBoard(givenSize, std::vector<int>(givenSize, -1)),
    masterTeamObstructionBoard(givenSize, std::vector<int>(givenSize, -1)),

    masterPlayerBoard(givenSize, std::vector<double>(givenSize, 0)), masterPlayerBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), 
    masterPlayerBoardFill(givenSize, std::vector<size_t>(givenSize, 0)), finalPlayerTerritoryMap(givenSize, givenSize, CV_8UC4),
    
    masterTeamBoard(givenSize, std::vector<double>(givenSize, 0)),   masterTeamBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), 
    masterTeamBoardFill(givenSize, std::vector<size_t>(givenSize, 0)),   finalTeamTerritoryMap(givenSize, givenSize, CV_8UC4),
    
    playerObstructionBoards(numPlayers + 1,
                            std::vector<std::vector<bool>>(givenSize, std::vector<bool>(givenSize, false))),
    teamObstructionBoards(numTeams + 1,
                          std::vector<std::vector<bool>>(givenSize, std::vector<bool>(givenSize, false))),
    playerObstructionCounts(numPlayers + 1,
                            std::vector<std::vector<size_t>>(givenSize, std::vector<size_t>(givenSize, 0))),
    teamObstructionCounts(numTeams + 1,
                          std::vector<std::vector<size_t>>(givenSize, std::vector<size_t>(givenSize, 0))),
    WalkableTerrainBoard(givenSize, std::vector<size_t>(givenSize, 0)),

    buildingsDict(), rangedBuildings(), 
    isPlayerDefeated(numPlayers + 1, false)
                          {

// initialise nonwalkable terrain
    initWalkableTerrain();



    if (teamAssignments.size() != numPlayers) {
        std::cerr << "Error: teamAssignments size must match numPlayers." << std::endl;
        exit(1);
    }
    // Load buildingsDict and rangedBuildings from JSON file
    loadBuildingsDict("buildingsDict.json");


    // 0 is gaia (neutral objects), index 0 remains reserved in each vector-backed board.
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {

            gaia_board[i][j] = 0; 

            masterPlayerBoard[i][j] = 0;
            masterPlayerBoardEdges[i][j] = 0;
            masterPlayerBoardFill[i][j] = 0;
            
            masterTeamBoard[i][j] = 0;
            masterTeamBoardEdges[i][j] = 0;
            masterTeamBoardFill[i][j] = 0;
        }
    }
}


void TerritoryAnalyser::initWalkableTerrain() {
    // This function identifies non-walkable terrain edges
    // For simplicity, let's assume that any cell with a value of 0 in the gaia_board is walkable terrain,
    // and any cell with a value of 1 is walkable terrain (0 = non-walkable/water).

    Config config = AppConfig::get();

    // read in walkable terrain: create example
    std::vector<std::vector<size_t>> WalkableTerrain(size, std::vector<size_t>(size, 1));
    for(size_t i=0; i < size; i++) {
        for(size_t j=0; j < size; j++) {
            if(i*i + j*j > 27*27+28*27) { // example circular water body
                WalkableTerrain[i][j] = 0;
            }
        }
    }
    
    // store walkable terrain board (1 = walkable, 0 = water/non-walkable)
    WalkableTerrainBoard = WalkableTerrain;

    return;
}


void TerritoryAnalyser::updateBuilding(size_t x, size_t y, std::string building, int player, int team, std::string mod) {

    BuildingInfo info;

    const auto& config = AppConfig::get();

    try {
        info = buildingsDict.at(building);
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: Building type '" << building << "' not found in buildingsDict." << std::endl;
        // Can set to automatically use a default value for r, such as the dark age vision of the building.
        return;
    }

    // For defeated players removing non-ranged buildings: obstructions still update normally
    // but territory is already cleared, so skip all territory-related work and return.
    const bool isRangedBuilding = std::find(rangedBuildings.begin(), rangedBuildings.end(), building) != rangedBuildings.end();
    if (mod == "remove" && player > 0
        && static_cast<size_t>(player) < isPlayerDefeated.size()
        && isPlayerDefeated[static_cast<size_t>(player)]
        && !isRangedBuilding) {
        updateObstruction(x, y, info.width, info.height, player, team, mod);
        return;
    }

    PlayerObject buildingInstance;
    buildingInstance.instanceId = 0;
    buildingInstance.player = player;
    buildingInstance.team = team;
    buildingInstance.building = building;
    buildingInstance.info = info;
    buildingInstance.position = Position{x,y};
    buildingInstance.influenceMinRow = 0;
    buildingInstance.influenceMinCol = 0;

    std::tuple<size_t, size_t, size_t, size_t> bounds = std::make_tuple(0U, 0U, 0U, 0U);
    std::tuple<size_t, size_t, size_t, size_t> impactedBounds = std::make_tuple(0U, 0U, 0U, 0U);

    // Buildings that may need recomputation after an obstruction change.
    const auto impactedBuildings = collectAttributedBuildingsInFootprint(x, y, info.width, info.height);
    std::unordered_set<size_t> impactedPlayers;
    std::unordered_set<size_t> impactedTeams;

    if (player > 0) {
        impactedPlayers.insert(static_cast<size_t>(player));
        impactedTeams.insert(static_cast<size_t>(teamAssignments.at(player)));
    }
    for (const size_t id : impactedBuildings) {
        const auto impactedIt = buildingInstancesById.find(id);
        if (impactedIt == buildingInstancesById.end()) {
            continue;
        }

        if (impactedIt->second.player > 0) {
            impactedPlayers.insert(static_cast<size_t>(impactedIt->second.player));
            impactedTeams.insert(static_cast<size_t>(impactedIt->second.team));
        }
    }

    bounds = updateTerritory(buildingInstance, player, mod);
    updateObstruction(x, y, info.width, info.height, player, team, mod);
    impactedBounds = reapplyAttributedBuildings(impactedBuildings);
    bounds = mergeBounds(bounds, impactedBounds);

    // Refresh raw ownership bool passes for all impacted owners.
    for (const size_t impactedPlayer : impactedPlayers) {
        if (impactedPlayer < playerGrids.size()) {
            playerGrids.at(impactedPlayer).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
        }
    }
    for (const size_t impactedTeam : impactedTeams) {
        if (impactedTeam < teamGrids.size()) {
            teamGrids.at(impactedTeam).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
        }
    }

    return;

}

std::unordered_set<size_t> TerritoryAnalyser::collectAttributedBuildingsInFootprint(size_t x, size_t y, size_t width, size_t height) const {
    std::unordered_set<size_t> impactedBuildings;

    for (size_t row = x; row < x + height && row < size; ++row) {
        for (size_t col = y; col < y + width && col < size; ++col) {
            const auto attributionIt = cellBuildingAttribution.find(Position{row, col});
            if (attributionIt == cellBuildingAttribution.end()) {
                continue;
            }

            impactedBuildings.insert(attributionIt->second.begin(), attributionIt->second.end());
        }
    }

    return impactedBuildings;
}

void TerritoryAnalyser::updateCellAttributionForBuilding(const PlayerObject& buildingInstance, std::string mod) {
    const size_t buildingId = buildingInstance.instanceId;

    const size_t centerX = buildingInstance.position.first;
    const size_t centerY = buildingInstance.position.second;
    const BuildingInfo& info = buildingInstance.info;

    const size_t r = info.influenceRadius;
    const size_t bh = info.height;
    const size_t bw = info.width;
    const size_t softEdge = info.influenceSoftExpansion;

    const double rectMinX = static_cast<double>(centerX);
    const double rectMaxX = static_cast<double>(centerX + bh - 1);
    const double rectMinY = static_cast<double>(centerY);
    const double rectMaxY = static_cast<double>(centerY + bw - 1);

    const size_t minRow = std::max(0, static_cast<int>(centerX) - static_cast<int>(r) - static_cast<int>(softEdge));
    const size_t maxRow = std::min(static_cast<int>(size), static_cast<int>(centerX + bh + r + softEdge));
    const size_t minCol = std::max(0, static_cast<int>(centerY) - static_cast<int>(r) - static_cast<int>(softEdge));
    const size_t maxCol = std::min(static_cast<int>(size), static_cast<int>(centerY + bw + r + softEdge));
    const double attributionRadius = static_cast<double>(r + softEdge);

    for (size_t row = minRow; row < maxRow; ++row) {
        const double dx = std::max(0.0, std::max(rectMinX - static_cast<double>(row), static_cast<double>(row) - rectMaxX));
        for (size_t col = minCol; col < maxCol; ++col) {
            const double dy = std::max(0.0, std::max(rectMinY - static_cast<double>(col), static_cast<double>(col) - rectMaxY));
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > attributionRadius) {
                continue;
            }

            const Position cell{row, col};
            if (mod == "add") {
                cellBuildingAttribution[cell].insert(buildingId);
            } else {
                auto cellIt = cellBuildingAttribution.find(cell);
                if (cellIt == cellBuildingAttribution.end()) {
                    continue;
                }

                cellIt->second.erase(buildingId);
                if (cellIt->second.empty()) {
                    cellBuildingAttribution.erase(cellIt);
                }
            }
        }
    }
}

std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> TerritoryAnalyser::reapplyAttributedBuildings(
    const std::unordered_set<size_t>& buildingIds,
    size_t skipId) {

    std::tuple<size_t, size_t, size_t, size_t> mergedBounds = std::make_tuple(0U, 0U, 0U, 0U);

    for (const size_t id : buildingIds) {
        if (id == skipId) {
            continue;
        }

        const auto buildingIt = buildingInstancesById.find(id);
        if (buildingIt == buildingInstancesById.end()) {
            continue;
        }

        const PlayerObject affectedBuilding = buildingIt->second;
        const auto removalBounds = updateTerritory(affectedBuilding, affectedBuilding.player, "remove");
        const auto addBounds = updateTerritory(affectedBuilding, affectedBuilding.player, "add");

        mergedBounds = mergeBounds(mergedBounds, mergeBounds(removalBounds, addBounds));
    }

    return mergedBounds;
}

std::tuple<size_t, size_t, size_t, size_t> TerritoryAnalyser::mergeBounds(
    const std::tuple<size_t, size_t, size_t, size_t>& lhs,
    const std::tuple<size_t, size_t, size_t, size_t>& rhs) {

    if (lhs == std::make_tuple(0U, 0U, 0U, 0U)) {
        return rhs;
    }
    if (rhs == std::make_tuple(0U, 0U, 0U, 0U)) {
        return lhs;
    }

    size_t lMinRow, lMaxRow, lMinCol, lMaxCol;
    size_t rMinRow, rMaxRow, rMinCol, rMaxCol;
    std::tie(lMinRow, lMaxRow, lMinCol, lMaxCol) = lhs;
    std::tie(rMinRow, rMaxRow, rMinCol, rMaxCol) = rhs;

    return std::make_tuple(
        std::min(lMinRow, rMinRow),
        std::max(lMaxRow, rMaxRow),
        std::min(lMinCol, rMinCol),
        std::max(lMaxCol, rMaxCol));
}

void TerritoryAnalyser::updateObstruction(size_t x, size_t y, size_t bw, size_t bh, int player, int team, std::string mod) {

    const bool isAdd = (mod == "add");

    // Footprint semantics are [x, x + bh) for rows and [y, y + bw) for cols.
    for (size_t row = x; row < x + bh && row < size; ++row) {
        for (size_t col = y; col < y + bw && col < size; ++col) {
            const size_t playerIndex = static_cast<size_t>(player);
            const size_t teamIndex = static_cast<size_t>(team);

            if (isAdd) {
                playerObstructionCounts.at(playerIndex)[row][col] += 1;
                teamObstructionCounts.at(teamIndex)[row][col] += 1;
            } else {
                if (playerObstructionCounts.at(playerIndex)[row][col] > 0) {
                    playerObstructionCounts.at(playerIndex)[row][col] -= 1;
                }
                if (teamObstructionCounts.at(teamIndex)[row][col] > 0) {
                    teamObstructionCounts.at(teamIndex)[row][col] -= 1;
                }
            }

            playerObstructionBoards.at(playerIndex)[row][col] = (playerObstructionCounts.at(playerIndex)[row][col] > 0);
            teamObstructionBoards.at(teamIndex)[row][col] = (teamObstructionCounts.at(teamIndex)[row][col] > 0);

            int playerMask = 0;
            for (size_t playerId = 1; playerId <= numPlayers; ++playerId) {
                if (playerObstructionBoards.at(playerId)[row][col]) {
                    playerMask |= (1 << static_cast<int>(playerId - 1));
                }
            }
            if (playerMask != 0) {
                masterPlayerObstructionBoard[row][col] = playerMask;
            } else if (playerObstructionBoards.at(0)[row][col]) {
                masterPlayerObstructionBoard[row][col] = 0;
            } else {
                masterPlayerObstructionBoard[row][col] = -1;
            }

            int teamMask = 0;
            for (size_t teamId = 1; teamId <= numTeams; ++teamId) {
                if (teamObstructionBoards.at(teamId)[row][col]) {
                    teamMask |= (1 << static_cast<int>(teamId - 1));
                }
            }
            if (teamMask != 0) {
                masterTeamObstructionBoard[row][col] = teamMask;
            } else if (teamObstructionBoards.at(0)[row][col]) {
                masterTeamObstructionBoard[row][col] = 0;
            } else {
                masterTeamObstructionBoard[row][col] = -1;
            }
        }
    }
}

std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> TerritoryAnalyser::updateTerritory(PlayerObject buildingInstance, size_t player, std::string mod) {
    // adds territory influence around a rectangular building footprint
    // footprint spans [centerX, centerX+bh) x [centerY, centerY+bw)
    // influence radius r extends outward from the footprint edges
    
    size_t centerX=buildingInstance.position.first, centerY=buildingInstance.position.second;
    std::string building=buildingInstance.building; 
    BuildingInfo info=buildingInstance.info;
    size_t x, y; 
    double dist;
    
    if (mod != "add" && mod != "remove") {
        std::cerr << "Error: Invalid modification type specified. Use 'add' or 'remove'." << std::endl;
        exit(1);
    }

    Config config = AppConfig::get();
    size_t r = info.influenceRadius, bh = info.height, bw = info.width, soft_edge = info.influenceSoftExpansion;
    const bool isRangedBuilding = std::find(rangedBuildings.begin(), rangedBuildings.end(), building) != rangedBuildings.end();
    const bool forceRadialInfluence = config.forceRadialInfluenceForRangedBuildings;
    
    // Rectangle bounds (inclusive max edge for distance calc)
    double rectMinX = static_cast<double>(centerX);
    double rectMaxX = static_cast<double>(centerX + bh - 1);
    double rectMinY = static_cast<double>(centerY);
    double rectMaxY = static_cast<double>(centerY + bw - 1);
    
    // Determine the bounding box (rectangle + radius + soft_edge) to avoid checking the entire grid
    size_t minRow = std::max(0, static_cast<int>(centerX) - static_cast<int>(r) - static_cast<int>(soft_edge)),
           maxRow = std::min(static_cast<int>(size), static_cast<int>(centerX + bh + r + soft_edge)),
           minCol = std::max(0, static_cast<int>(centerY) - static_cast<int>(r) - static_cast<int>(soft_edge)),
           maxCol = std::min(static_cast<int>(size), static_cast<int>(centerY + bw + r + soft_edge));

    const size_t influenceMinRow = minRow;
    const size_t influenceMinCol = minCol;


    if(mod == "remove") {
        const BuildingInstanceKey key{
            buildingInstance.player,
            buildingInstance.team,
            buildingInstance.building,
            buildingInstance.position
        };

        const auto keyedIdsIt = buildingInstanceIdsByKey.find(key);
        if (keyedIdsIt == buildingInstanceIdsByKey.end() || keyedIdsIt->second.empty()) {
            std::cerr << "Warning: No building instance found at (" << centerX << ", " << centerY << ") for removal." << std::endl;
            return std::make_tuple(0, 0, 0, 0);
        }

        size_t removalId = keyedIdsIt->second.back();
        if (buildingInstance.instanceId != 0) {
            const auto requestedIt = std::find(keyedIdsIt->second.begin(), keyedIdsIt->second.end(), buildingInstance.instanceId);
            if (requestedIt != keyedIdsIt->second.end()) {
                removalId = buildingInstance.instanceId;
            }
        }

        const auto storedIt = buildingInstancesById.find(removalId);
        if (storedIt == buildingInstancesById.end()) {
            std::cerr << "Warning: No stored building instance id " << removalId << " found for removal." << std::endl;
            return std::make_tuple(0, 0, 0, 0);
        }

        const PlayerObject removal = storedIt->second;
        const size_t removalMinRow = removal.influenceMinRow;
        const size_t removalMinCol = removal.influenceMinCol;
        const size_t removalMaxRow = removalMinRow + removal.influenceBoard.size();
        const size_t removalMaxCol = removal.influenceBoard.empty() ? removalMinCol : removalMinCol + removal.influenceBoard.front().size();

        if (removal.player == 0) {
            for (x = centerX; x < centerX + bh && x < size; ++x) {
                for (y = centerY; y < centerY + bw && y < size; ++y) {
                    gaia_board[x][y] -= sgn(info.influenceWeight);
                }
            }
        } else {
            for(size_t i = removalMinRow; i < removalMaxRow && i < size; ++i) {
                for(size_t j = removalMinCol; j < removalMaxCol && j < size; ++j) {
                    const double influenceValue = removal.influenceBoard[i - removalMinRow][j - removalMinCol];
                    playerGrids.at(removal.player).setValue(i, j, playerGrids.at(removal.player).getValue(i, j) - influenceValue);
                    teamGrids.at(removal.team).setValue(i, j, teamGrids.at(removal.team).getValue(i, j) - influenceValue);
                }
            }
        }

        updateCellAttributionForBuilding(removal, "remove");
        buildingInstancesById.erase(removalId);

        auto mutableIdsIt = buildingInstanceIdsByKey.find(key);
        if (mutableIdsIt != buildingInstanceIdsByKey.end()) {
            auto& ids = mutableIdsIt->second;
            ids.erase(std::remove(ids.begin(), ids.end(), removalId), ids.end());
            if (ids.empty()) {
                buildingInstanceIdsByKey.erase(mutableIdsIt);
            }
        }

        return std::make_tuple(removalMinRow, removalMaxRow, removalMinCol, removalMaxCol);
    }
    // else, it is add
    buildingInstance.influenceBoard = std::vector(maxRow-minRow, std::vector<double>(maxCol-minCol,0));
    buildingInstance.influenceMinRow = influenceMinRow;
    buildingInstance.influenceMinCol = influenceMinCol;
    buildingInstance.player = player;
    buildingInstance.team = (player == 0) ? 0 : static_cast<size_t>(teamAssignments.at(static_cast<int>(player)));
    buildingInstance.instanceId = nextBuildingInstanceId++;

    // if player is Gaia
    if (player == 0) {
        for (x = centerX; x < centerX + bh && x < size; ++x) {
            for (y = centerY; y < centerY + bw && y < size; ++y) {
                gaia_board[x][y] += sgn(info.influenceWeight);
            }
        }
    } else {
        const auto& config = AppConfig::get();
        const bool isNonWalkableTerrainBuilding = std::find(config.nonWalkableTerrainBuildings.begin(), config.nonWalkableTerrainBuildings.end(), building) != config.nonWalkableTerrainBuildings.end();

        // Ranged buildings: Euclidean distance, bounding box, obstructions ignored.
        for (x = minRow; x < maxRow; ++x) {
            double dx = std::max(0.0, std::max(rectMinX - static_cast<double>(x), static_cast<double>(x) - rectMaxX));
            for (y = minCol; y < maxCol; ++y) {
                
                double dy = std::max(0.0, std::max(rectMinY - static_cast<double>(y), static_cast<double>(y) - rectMaxY));
                dist = std::sqrt(dx * dx + dy * dy);
                double value = (dist > r) ? std::max(0.0, (r + soft_edge + 1.0 - dist) / (soft_edge + 1) * (static_cast<double>(threshold) / info.softEdgeDivFactor)) : static_cast<double>(info.influenceWeight);
                // AK Why not just do the cellBuildingAttribution here? Does not that save having to do a double for loop?

                if (isRangedBuilding) {
                    playerGrids.at(player).setValue(x, y, playerGrids.at(player).getValue(x, y) + value);
                    teamGrids.at(teamAssignments.at(player)).setValue(x, y, teamGrids.at(teamAssignments.at(player)).getValue(x, y) + value);
                    // save local effect to object so that removal is easy.
                    buildingInstance.influenceBoard[x-minRow][y-minCol] = value;
                }
            }
        }
        if (!isRangedBuilding && !forceRadialInfluence) {
            // Non-ranged buildings: weighted 8-neighbour Dijkstra from the footprint.
            // Diagonal moves are allowed, but corner cutting through blocked orthogonals is disallowed.
            const double infDist = std::numeric_limits<double>::infinity();
            const double maxPositiveDistance = static_cast<double>(r + soft_edge) + 1.0;
            const double orthogonalCost = 1.0;
            const double diagonalCost = std::sqrt(2.0);
            std::vector<std::vector<double>> pathDistance(size, std::vector<double>(size, infDist));
            std::vector<std::vector<bool>> finalized(size, std::vector<bool>(size, false));

            using Node = std::tuple<double, size_t, size_t>;
            std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
            size_t actualMinRow = centerX, actualMaxRow = centerX + bh - 1;
            size_t actualMinCol = centerY, actualMaxCol = centerY + bw - 1;

            const auto addInfluenceAtDistance = [&](size_t row, size_t col, double d) {
                const double value = (d > static_cast<int>(r))
                    ? std::max(0.0, (static_cast<double>(r) + static_cast<double>(soft_edge) + 1.0 - static_cast<double>(d))
                                    / (static_cast<double>(soft_edge) + 1.0) * (static_cast<double>(threshold) / info.softEdgeDivFactor))
                    : static_cast<double>(info.influenceWeight);

                if (value <= 0.0) {
                    return;
                }

                // Add val *= mod == add ? 1 -1

                playerGrids.at(player).setValue(row, col, playerGrids.at(player).getValue(row, col) + value);
                teamGrids.at(teamAssignments.at(player)).setValue(row, col, teamGrids.at(teamAssignments.at(player)).getValue(row, col) + value);
                // save local effect to object so that removal is easy.
                buildingInstance.influenceBoard[row-influenceMinRow][col-influenceMinCol] = value;

                actualMinRow = std::min(actualMinRow, row);
                actualMaxRow = std::max(actualMaxRow, row);
                actualMinCol = std::min(actualMinCol, col);
                actualMaxCol = std::max(actualMaxCol, col);
            };

            const std::array<int, 8> rowOffsets = {-1, -1, -1, 0, 0, 1, 1, 1};
            const std::array<int, 8> colOffsets = {-1, 0, 1, -1, 1, -1, 0, 1};

            const auto inBounds = [this](int row, int col) {
                return row >= 0 && col >= 0 && row < static_cast<int>(size) && col < static_cast<int>(size);
            };

            const auto isInFootprint = [centerX, centerY, bh, bw](size_t row, size_t col) {
                return row >= centerX && row < centerX + bh && col >= centerY && col < centerY + bw;
            };

            const auto isTraversable = [&](size_t row, size_t col) {
                if (isInFootprint(row, col)) {
                    return true;
                }
                if (masterPlayerObstructionBoard[row][col] != -1) {
                    return false;
                }
                if (!WalkableTerrainBoard[row][col] && !isNonWalkableTerrainBuilding) {
                    return false;
                }
                return true;
            };

            // Seed the footprint with zero path distance.
            for (x = centerX; x < centerX + bh && x < size; ++x) {
                for (y = centerY; y < centerY + bw && y < size; ++y) {
                    if (pathDistance[x][y] > 0.0) {
                        pathDistance[x][y] = 0.0;
                        frontier.push({0.0, x, y});
                    }
                }
            }

            while (!frontier.empty()) {
                const auto [currentDist, row, col] = frontier.top();
                frontier.pop();

                if (finalized[row][col]) {
                    continue;
                }
                if (currentDist >= maxPositiveDistance) {
                    continue;
                }
                finalized[row][col] = true;

                addInfluenceAtDistance(row, col, currentDist);

                for (size_t direction = 0; direction < 8; ++direction) {
                    const int nextRow = static_cast<int>(row) + rowOffsets[direction];
                    const int nextCol = static_cast<int>(col) + colOffsets[direction];

                    if (!inBounds(nextRow, nextCol)) {
                        continue;
                    }

                    const size_t nr = static_cast<size_t>(nextRow);
                    const size_t nc = static_cast<size_t>(nextCol);

                    if (!isTraversable(nr, nc)) {
                        continue;
                    }

                    // For diagonal moves, ensure that we are not cutting through a corner where one or both of the orthogonal neighbors are blocked.
                    const bool isDiagonal = (rowOffsets[direction] != 0 && colOffsets[direction] != 0);
                    if (isDiagonal) {
                        const int sideRowA = static_cast<int>(row) + rowOffsets[direction];
                        const int sideColA = static_cast<int>(col);
                        const int sideRowB = static_cast<int>(row);
                        const int sideColB = static_cast<int>(col) + colOffsets[direction];

                        if (!inBounds(sideRowA, sideColA) || !inBounds(sideRowB, sideColB)) {
                            continue;
                        }

                        const size_t sra = static_cast<size_t>(sideRowA);
                        const size_t sca = static_cast<size_t>(sideColA);
                        const size_t srb = static_cast<size_t>(sideRowB);
                        const size_t scb = static_cast<size_t>(sideColB);

                        if (!isTraversable(sra, sca) || !isTraversable(srb, scb)) {
                            continue;
                        }
                    }

                    const double stepCost = isDiagonal ? diagonalCost : orthogonalCost;
                    const double candidateDist = currentDist + stepCost;

                    if (candidateDist >= maxPositiveDistance) {
                        continue;
                    }

                    if (candidateDist + 1e-9 < pathDistance[nr][nc]) {
                        pathDistance[nr][nc] = candidateDist;
                        frontier.push({candidateDist, nr, nc});
                    }
                }
            }

            minRow = actualMinRow;
            maxRow = actualMaxRow + 1;
            minCol = actualMinCol;
            maxCol = actualMaxCol + 1;
        }
    }

    updateCellAttributionForBuilding(buildingInstance, "add");
    buildingInstancesById[buildingInstance.instanceId] = buildingInstance;
    const BuildingInstanceKey addKey{
        buildingInstance.player,
        buildingInstance.team,
        buildingInstance.building,
        buildingInstance.position
    };
    buildingInstanceIdsByKey[addKey].push_back(buildingInstance.instanceId);
    return  std::make_tuple(minRow, maxRow, minCol, maxCol);
}


void TerritoryAnalyser::updateRender() {
    
    // import config 
    const auto& config = AppConfig::get();

    // merge the different player/team boards together to create master boards
    mapMergedTerritories();

    // if using growth method for contested territories, contested territories need to be resolved before edges/fill
    if (config.contestedTerritoryMethod == "growth") {
        resolveContestedTerritoryGrowth();
    }
    
    // analyse fills and gaps
    auto fillResultPlayers = analyseFill(getMasterBoard("player"), playerObstructionBoards, numPlayers, &WalkableTerrainBoard, &isPlayerDefeated);
    playerGaps = fillResultPlayers.gaps;
    masterPlayerBoardFill = fillResultPlayers.fillBoard;

    auto fillResultTeams = analyseFill(getMasterBoard("team"), teamObstructionBoards, numTeams, &WalkableTerrainBoard);
    teamGaps = fillResultTeams.gaps;
    masterTeamBoardFill = fillResultTeams.fillBoard;

    // analyse edges
    masterPlayerBoardEdges = findEdges(getMasterBoard("player"), getMasterBoardFill("player"), "fourBox");
    masterTeamBoardEdges = findEdges(getMasterBoard("team"), getMasterBoardFill("team"), "fourBox");

    // perform colour pass
    colour_pass();
}

void TerritoryAnalyser::updateContestedFlash() {

    const auto& config = AppConfig::get();

    // seed stores a timestamp count captured at construction; use it as the pulse origin.
    const auto nowNs = static_cast<size_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const size_t elapsedNs = nowNs - seed;

    // Triangular wave over 2.5s: 0 -> peak -> 0.
    const size_t periodNs = 2500000000ULL;
    const size_t halfPeriodNs = periodNs / 2;
    const size_t phaseNs = elapsedNs % periodNs;
    const size_t peakAlpha = static_cast<size_t>(config.contestedTerritoryColour[3]);

    size_t alphaVal = 0;
    if (peakAlpha > 0) {
        if (phaseNs <= halfPeriodNs) {
            alphaVal = (phaseNs * peakAlpha) / halfPeriodNs;
        } else {
            alphaVal = ((periodNs - phaseNs) * peakAlpha) / halfPeriodNs;
        }
    }

    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {
            const bool playerIsContested = !playerContestedMapTruth.empty()
                && playerContestedMapTruth[i][j];
            if (playerIsContested) {
                finalPlayerTerritoryMap.at<cv::Vec4b>(i, j) = cv::Vec4b(
                    config.contestedTerritoryColour[0],
                    config.contestedTerritoryColour[1],
                    config.contestedTerritoryColour[2],
                    static_cast<unsigned char>(alphaVal));
            } 

            const bool teamIsContested = !teamContestedMapTruth.empty()
                && teamContestedMapTruth[i][j];
            if (teamIsContested) {
                finalTeamTerritoryMap.at<cv::Vec4b>(i, j) = cv::Vec4b(
                    config.contestedTerritoryColour[0],
                    config.contestedTerritoryColour[1],
                    config.contestedTerritoryColour[2],
                    static_cast<unsigned char>(alphaVal));
            }
        }
    }


}

void TerritoryAnalyser::resolveContestedTerritoryGrowth(std::tuple<size_t, size_t, size_t, size_t> bounds) {
    
    // This function identifies contested territories based on the growth method and updates the master boards accordingly.
    
    // we are going to go through all the contested cells and find each players nearest uncontested cell.
    // using the distances in an equation, we will bias the weight of a contested cell to be 10% less it's value for every cell distance away from an uncontested cell of that player.
    // Finally, the highest adjusted weight will be the owner of the contested cell, and if there is a tie for highest, the closest gets it, otherwise the lowest player ID gets it (to ensure deterministic results).
    
    size_t minRow = 0;
    size_t maxRow = size;
    size_t minCol = 0;
    size_t maxCol = size;

    if (bounds != std::make_tuple(0, 0, 0, 0)) {
        std::tie(minRow, maxRow, minCol, maxCol) = bounds;
        minRow = std::min(minRow, size);
        maxRow = std::min(maxRow, size);
        minCol = std::min(minCol, size);
        maxCol = std::min(maxCol, size);

        if (minRow >= maxRow || minCol >= maxCol) {
            return;
        }
    }

    const auto resolveBoard = [this](std::vector<std::vector<double>>& masterBoard,
                                     const std::vector<Grid>& grids,
                                     size_t ownerCount,
                                     size_t minRow,
                                     size_t maxRow,
                                     size_t minCol,
                                     size_t maxCol) {
        if (masterBoard.empty() || ownerCount == 0) {
            return;
        }

        const size_t boardSize = masterBoard.size();
        const int unreachable = std::numeric_limits<int>::max() / 4;

        std::vector<std::vector<size_t>> ownership(boardSize, std::vector<size_t>(boardSize, 0));
        for (size_t row = 0; row < boardSize; ++row) {
            for (size_t col = 0; col < boardSize; ++col) {
                ownership[row][col] = static_cast<size_t>(masterBoard[row][col]);
            }
        }

        std::vector<std::vector<std::vector<int>>> distances(
            ownerCount + 1,
            std::vector<std::vector<int>>(boardSize, std::vector<int>(boardSize, unreachable)));
        std::vector<std::vector<std::vector<double>>> ownerTruthBoards(ownerCount + 1);
        std::vector<bool> relevantOwners(ownerCount + 1, false);

        bool hasContestedCellsInBounds = false;
        for (size_t row = minRow; row < maxRow; ++row) {
            for (size_t col = minCol; col < maxCol; ++col) {
                const size_t cellMask = ownership[row][col];
                if (cellMask == 0 || (cellMask & (cellMask - 1)) == 0) {
                    continue;
                }

                hasContestedCellsInBounds = true;
                for (size_t ownerId = 1; ownerId <= ownerCount; ++ownerId) {
                    const size_t ownerMask = size_t{1} << (ownerId - 1);
                    if ((cellMask & ownerMask) != 0U) {
                        relevantOwners[ownerId] = true;
                    }
                }
            }
        }

        if (!hasContestedCellsInBounds) {
            return;
        }

        const std::array<int, 4> rowOffsets = {-1, 1, 0, 0};
        const std::array<int, 4> colOffsets = {0, 0, -1, 1};

        for (size_t ownerId = 1; ownerId <= ownerCount; ++ownerId) {
            if (!relevantOwners[ownerId]) {
                continue;
            }

            if (ownerId < grids.size()) {
                ownerTruthBoards[ownerId] = grids[ownerId].getDataTruth();
            }

            const size_t ownerMask = size_t{1} << (ownerId - 1);
            std::queue<std::pair<size_t, size_t>> frontier;
            bool hasUniqueSeed = false;

            for (size_t row = 0; row < boardSize; ++row) {
                for (size_t col = 0; col < boardSize; ++col) {
                    if (ownership[row][col] == ownerMask) {
                        distances[ownerId][row][col] = 0;
                        frontier.push({row, col});
                        hasUniqueSeed = true;
                    }
                }
            }

            // If an owner has no unique cells, fall back to all of its occupied cells so
            // fully-overlapped regions can still be resolved by raw influence weights.
            if (!hasUniqueSeed) {
                for (size_t row = 0; row < boardSize; ++row) {
                    for (size_t col = 0; col < boardSize; ++col) {
                        if ((ownership[row][col] & ownerMask) != 0U) {
                            distances[ownerId][row][col] = 0;
                            frontier.push({row, col});
                        }
                    }
                }
            }

            while (!frontier.empty()) {
                const auto [row, col] = frontier.front();
                frontier.pop();

                for (size_t direction = 0; direction < rowOffsets.size(); ++direction) {
                    const int nextRow = static_cast<int>(row) + rowOffsets[direction];
                    const int nextCol = static_cast<int>(col) + colOffsets[direction];

                    if (nextRow < 0 || nextCol < 0 || nextRow >= static_cast<int>(boardSize) || nextCol >= static_cast<int>(boardSize)) {
                        continue;
                    }

                    if (distances[ownerId][static_cast<size_t>(nextRow)][static_cast<size_t>(nextCol)] != unreachable) {
                        continue;
                    }

                    distances[ownerId][static_cast<size_t>(nextRow)][static_cast<size_t>(nextCol)] = distances[ownerId][row][col] + 1;
                    frontier.push({static_cast<size_t>(nextRow), static_cast<size_t>(nextCol)});
                }
            }
        }

        for (size_t row = minRow; row < maxRow; ++row) {
            for (size_t col = minCol; col < maxCol; ++col) {
                const size_t cellMask = ownership[row][col];
                if (cellMask == 0 || (cellMask & (cellMask - 1)) == 0) {
                    continue;
                }

                double bestAdjustedWeight = -1.0;
                int bestDistance = unreachable;
                int bestOwnerId = std::numeric_limits<int>::max();

                for (size_t ownerId = 1; ownerId <= ownerCount; ++ownerId) {
                    const size_t ownerMask = size_t{1} << (ownerId - 1);
                    if ((cellMask & ownerMask) == 0U) {
                        continue;
                    }

                    if (ownerTruthBoards[ownerId].empty()) {
                        continue;
                    }

                    const double rawWeight = ownerTruthBoards[ownerId][row][col];
                    const int distance = distances[ownerId][row][col];
                    const double falloff = std::max(0.0, 1.0 - (0.15 * static_cast<double>(distance)));
                    const double adjustedWeight = rawWeight * falloff;

                    const bool isBetterWeight = adjustedWeight > bestAdjustedWeight;
                    const bool isTiedWeight = std::abs(adjustedWeight - bestAdjustedWeight) < 1e-9;
                    const bool isCloser = distance < bestDistance;
                    const bool isLowerId = static_cast<int>(ownerId) < bestOwnerId;

                    if (isBetterWeight || (isTiedWeight && (isCloser || (distance == bestDistance && isLowerId)))) {
                        bestAdjustedWeight = adjustedWeight;
                        bestDistance = distance;
                        bestOwnerId = static_cast<int>(ownerId);
                    }
                }

                if (bestOwnerId != std::numeric_limits<int>::max()) {
                    masterBoard[row][col] = static_cast<double>(size_t{1} << (bestOwnerId - 1));
                }
            }
        }
    };

    resolveBoard(masterPlayerBoard, playerGrids, numPlayers, minRow, maxRow, minCol, maxCol);
    resolveBoard(masterTeamBoard, teamGrids, numTeams, minRow, maxRow, minCol, maxCol);
}

void TerritoryAnalyser::printPlayerBoards() {
    for (size_t playerId = 1; playerId < playerGrids.size(); ++playerId) {
        const Grid& board = playerGrids[playerId];

        std::cout << "Player" << playerId << " Board:" << std::endl;
        ::printBoard(board.getDataTruth());
        std::cout << std::endl;

        std::cout << "Player " << playerId << " Board Bool:" << std::endl;
        ::printBoard(board.getDataBool());
        std::cout << std::endl;
    }
    std::cout << "Gaia Board:" << std::endl;
    ::printBoard(gaia_board);
    std::cout << std::endl;
}
    

void TerritoryAnalyser::mapMergedTerritories() {

    std::vector<std::vector<double>> mergedPlayerBoard(size, std::vector<double>(size, 0)), mergedTeamBoard(size, std::vector<double>(size, 0));
    
    int counter = 1;
    for (size_t playerId = 1; playerId < playerGrids.size(); ++playerId) {
        mergedPlayerBoard = addPlTerrToMaster(mergedPlayerBoard, playerGrids[playerId].getDataBool(), masterPlayerObstructionBoard, counter);
        counter = counter * 2;
    }
    counter = 1;
    for (size_t teamId = 1; teamId < teamGrids.size(); ++teamId) {
        mergedTeamBoard = addPlTerrToMaster(mergedTeamBoard, teamGrids[teamId].getDataBool(), masterTeamObstructionBoard, counter);
        counter = counter * 2;
    }

    masterPlayerBoard = mergedPlayerBoard;
    masterTeamBoard = mergedTeamBoard;
}

std::vector<std::vector<Position>> TerritoryAnalyser::getGaps(std::string type) const {
    if (type == "player") {
        return playerGaps;
    } else if (type == "team") {
        return teamGaps;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player board by default. (Gap cells)" << std::endl;
        return playerGaps;
    }
}

std::vector<std::vector<double>> TerritoryAnalyser::getMasterBoard(std::string type) const {
    if (type == "player") {
        return masterPlayerBoard;
    } else if (type == "team") {
        return masterTeamBoard;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player board by default." << std::endl;
        return masterPlayerBoard;
    }
}

std::vector<std::vector<size_t>> TerritoryAnalyser::getGaiaBoard() const {
    return gaia_board;
}

std::vector<std::vector<size_t>> TerritoryAnalyser::getWalkableTerrainBoard() const {
    return WalkableTerrainBoard;
}

std::vector<std::vector<int>> TerritoryAnalyser::getMasterObstructionBoard(std::string type) const {
    if (type == "player") {
        return masterPlayerObstructionBoard;
    } else if (type == "team") {
        return masterTeamObstructionBoard;
    } else {
        std::cerr << "Error: Invalid obstruction board type specified (" << type << "). Returning player obstruction board by default." << std::endl;
        return masterPlayerObstructionBoard;
    }
}

std::vector<std::vector<size_t>> TerritoryAnalyser::getMasterBoardEdges(std::string type) const {
    if (type == "player") {
        return masterPlayerBoardEdges;
    } else if (type == "team") {
        return masterTeamBoardEdges;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player edges board by default." << std::endl;
        return masterPlayerBoardEdges;
    }
}


std::vector<std::vector<size_t>> TerritoryAnalyser::getMasterBoardFill(std::string type) const {
    if (type == "player") {
        return masterPlayerBoardFill;
    } else if (type == "team") {
        return masterTeamBoardFill;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player fill board by default." << std::endl;
        return masterPlayerBoardFill;
    }
}


cv::Mat TerritoryAnalyser::getFinalTerritoryMap(std::string type) const {
    if (type == "player") {
        return finalPlayerTerritoryMap;
    } else if (type == "team") {
        return finalTeamTerritoryMap;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player board by default." << std::endl;
        return finalPlayerTerritoryMap;
    }
}

cv::Mat TerritoryAnalyser::getContestedMap(std::string type) const {
    if (type == "player") {
        return playerContestedMap;
    } else if (type == "team") {
        return teamContestedMap;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player board by default." << std::endl;
        return playerContestedMap;
    }
}

void TerritoryAnalyser::colour_pass() {

    const auto& config = AppConfig::get();

    std::map<int, cv::Vec4b> playerColours, teamColours;
    for (const auto& [playerId, rgba] : config.playerColours) {
        playerColours[playerId] = cv::Vec4b(
            static_cast<uchar>(rgba[0]),
            static_cast<uchar>(rgba[1]),
            static_cast<uchar>(rgba[2]),
            static_cast<uchar>(rgba[3]));
    }
    for (const auto& [teamId, rgba] : config.teamColours) {
        teamColours[teamId] = cv::Vec4b(
            static_cast<uchar>(rgba[0]),
            static_cast<uchar>(rgba[1]),
            static_cast<uchar>(rgba[2]),
            static_cast<uchar>(rgba[3]));
    }

    auto getColour = [&](int id, bool useTeamColours = false) -> cv::Vec4b {
        const auto& primaryColours = useTeamColours ? teamColours : playerColours;

        auto primaryIt = primaryColours.find(id);
        if (primaryIt != primaryColours.end()) {
            return primaryIt->second;
        }

        throw std::runtime_error("Missing colour in settings for id: " + std::to_string(id));
    };

    cv::Mat mat(size, size, CV_8UC4);
    
    // create new board
    std::vector<std::vector<double>> board(size, std::vector<double>(size, 0));
    std::vector<std::vector<size_t>> edge_board(size, std::vector<size_t>(size, 0)), fill_board(size, std::vector<size_t>(size, 0));

    enum class BoardType {
        Player,
        Team
    };

    std::array<std::pair<BoardType, const std::vector<std::vector<double>>*>, 2> boardCycle = {{
        {BoardType::Player, &masterPlayerBoard},
        {BoardType::Team, &masterTeamBoard}
    }};

    for (const auto& [boardType, masterBoardPtr] : boardCycle) {
        cv::Mat contestedMat = cv::Mat::zeros(static_cast<int>(size), static_cast<int>(size), CV_8UC4);
        std::vector<std::vector<bool>> contestedTruthMat(size, std::vector<bool>(size, false));

        const auto& masterBoard = *masterBoardPtr;
        const bool useTeamColours = (boardType == BoardType::Team);

        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                board[i][j] = masterBoard[i][j];
            }
        }
        if (boardType == BoardType::Player) {
            edge_board = masterPlayerBoardEdges;
            fill_board = masterPlayerBoardFill;
        } else {
            edge_board = masterTeamBoardEdges;
            fill_board = masterTeamBoardFill;
        }


        for (size_t i = 0; i < board.size(); ++i) {
            for (size_t j = 0; j < board.size(); ++j) {

                // initialise values
                bool existing_val = false, is_edge = false, is_building_territory = false;

// ************** 1. GAIA: If the cell is unoccupied or occupied by gaia, skip **************** //

                if (gaia_board[i][j] == 1 || (fill_board[i][j] == 0 && edge_board[i][j] == 0 && board[i][j] == 0)) {
                    mat.at<cv::Vec4b>(i, j) = getColour(0, useTeamColours);
                    continue;
                }

// ************** 2. EDGES **************** // 

                // loop through all the players in reverse order
                for (size_t k=8; k>0; k--) {

                    if (static_cast<int>(edge_board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) { // check if the cell is an edge for player k

                        // remove the highest power of 2 to find out if there are multiple players in the cell
                        edge_board[i][j] = static_cast<double>(static_cast<int>(edge_board[i][j]) % static_cast<int>(std::pow(2, k-1))); 

                        // if the cell is currently unoccupied, assign it to the player
                        if (!existing_val) { 
                            mat.at<cv::Vec4b>(i, j) = getColour(static_cast<int>(k), useTeamColours);
                            mat.at<cv::Vec4b>(i, j)[3] = config.edgeOpacity; 
                            existing_val = true;
                            is_edge = true;

                        // if the cell is already occupied, mark it as contested.
                        // For now, we make contested areas grey, the other idea was to have it be perpendicular 
                        // lines of the two/multiple players, but that is more complex to implement and may not 
                        // be worth the effort for the visualisation.
                        } else { 
                            if(config.contestedTerritoryMethod == "flash") {
                                mat.at<cv::Vec4b>(i, j) = getColour(0, useTeamColours);
                                mat.at<cv::Vec4b>(i, j)[3] = 0;
                                // contested mat will just be white and flash between white and transparent by toggling the alpha value
                                contestedMat.at<cv::Vec4b>(i, j) = cv::Vec4b(255, 255, 255, 255);
                                contestedTruthMat[i][j] = true;
                                // no need to check further players since it's already contested 
                                break; 
                            } else if (config.contestedTerritoryMethod == "perpendicularLines") {
                                // Implement perpendicular lines method here if desired
                                // This would likely involve drawing lines on the cell in the colours of the players involved
                                // and may require a more complex data structure to track which players are contesting the cell
                            } else if (config.contestedTerritoryMethod == "growth") {
                                // Implemented in updateBuilding
                                break;
                            } else if (config.contestedTerritoryMethod == "staticColour") {
                                // Implement static colour method, where the cell is coloured a specific colour for contested regardless of players involved
                            
                                mat.at<cv::Vec4b>(i, j) = cv::Vec4b(config.contestedTerritoryColour[0], config.contestedTerritoryColour[1], config.contestedTerritoryColour[2], config.contestedTerritoryColour[3]);
                                mat.at<cv::Vec4b>(i, j)[3] = config.edgeOpacity;
                                // no need to check further players since it's already contested 
                                break; 
                            } else {
                                std::cerr << "Error: Invalid contested territory method specified (" << config.contestedTerritoryMethod << "). Defaulting to flash method." << std::endl;
                                mat.at<cv::Vec4b>(i, j) = getColour(4, useTeamColours);
                                mat.at<cv::Vec4b>(i, j)[3] = config.edgeOpacity;
                            }
                        }
                    }
                }
                
// loop through all the players in reverse order - must be separate from edges or there are 3+ player situations 
// where the cell might be marked as contested territory before it gets to the edge player
                if(!is_edge) {
                    for (size_t k=8; k>0; k--) {

// ************** 3. TERRITORY FROM BUILDINGS **************** // 

                        if (static_cast<int>(board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) {

                            // remove the highest power of 2 to find out if there are multiple players in the cell
                            board[i][j] = static_cast<double>(static_cast<int>(board[i][j]) % static_cast<int>(std::pow(2, k-1))); 
                            
                            // if the cell is currently unoccupied, assign it to the player
                            if (!existing_val) { 
                                mat.at<cv::Vec4b>(i, j) = getColour(static_cast<int>(k), useTeamColours);
                                mat.at<cv::Vec4b>(i, j)[3] = config.territoryOpacity; 
                                existing_val = true;
                                is_building_territory = true;

                            // if the cell is already occupied, mark it as contested
                            } else { 
                                mat.at<cv::Vec4b>(i, j) = getColour(4, useTeamColours);
                                mat.at<cv::Vec4b>(i, j)[3] = config.territoryOpacity; 
                                // no need to check further players since it's already contested
                                break; 
                            }
                        }
                    }
                }

// ************** 4. TERRITORY FROM FILL **************** // 

                if(!is_edge && !is_building_territory) { 

                    if (fill_board[i][j] == 0) { 
                        continue;
                    } else {
                        int k = fill_board[i][j];
                        mat.at<cv::Vec4b>(i, j) = getColour(k, useTeamColours);
                        mat.at<cv::Vec4b>(i, j)[3] = static_cast<int>(config.territoryOpacity/2);
                        existing_val = true;
                    }
                }
            }
        }


        // When finished with individual players, repeat for teams
        if (boardType == BoardType::Player) {
            finalPlayerTerritoryMap = mat.clone();
            playerContestedMap = contestedMat.clone();
            playerContestedMapTruth = contestedTruthMat;
        } else {
            finalTeamTerritoryMap = mat.clone();
            teamContestedMap = contestedMat.clone();
            teamContestedMapTruth = contestedTruthMat;
        }
    }

    return;
}



std::vector<std::vector<double>> TerritoryAnalyser::addPlTerrToMaster(std::vector<std::vector<double>> masterBoard, std::vector<std::vector<bool>> playerBoard, const std::vector<std::vector<int>>& obstructionBoard, int mult_factor) {

    for (size_t i = 0; i < masterBoard.size(); ++i) {
        for (size_t j = 0; j < masterBoard[i].size(); ++j) {
            if (obstructionBoard[i][j] != -1) {
                masterBoard[i][j] = static_cast<double>(obstructionBoard[i][j]);
            } else {
                masterBoard[i][j] = masterBoard[i][j] + static_cast<double>(playerBoard[i][j] * mult_factor);
            }
        }
    }
    return masterBoard;
}




void TerritoryAnalyser::loadBuildingsDict(const std::string& givenDictPath) {

    std::ifstream file;
    std::string dictPath;
    for (const auto& candidate : {givenDictPath, "../"+givenDictPath}) {
        file.open(candidate);
        if (file.is_open()) {
            dictPath = candidate;
            break;
        }
        file.clear();
    }

    if (!file.is_open()) {
        std::cerr << "Error: Could not open buildingsDict.json. Tried: buildingsDict.json, ../buildingsDict.json" << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    }

    try {
        json data = json::parse(file);
        
        // Extract the buildingsDict array from JSON
        if (!data.contains("buildingsDict") || !data["buildingsDict"].is_array()) {
            throw std::runtime_error("buildingsDict must be an array in JSON file");
        }
        if (!data.contains("rangedBuildings") || !data["rangedBuildings"].is_array()) {
            std::cerr << "Warning: rangedBuildings array not found in buildingsDict.json. Ranged buildings will not be identified." << std::endl;
        }


        // Ranged buildings 
        rangedBuildings = data["rangedBuildings"].get<std::vector<std::string>>();
        
        // Parse each building entry in the buildingsDict array
        for (const auto& building : data["buildingsDict"]) {
            if (!building.is_array() || building.size() != 2) {
                throw std::runtime_error("Each building entry must be [name, properties_array]");
            }
            
            std::string name = building[0].get<std::string>();
            std::vector<double> properties = building[1].get<std::vector<double>>();
            
            if (properties.size() != 6) {
                throw std::runtime_error("Each building must have 6 properties: [radius, weight, soft_edge, soft_edge_div_factor, width, height]");
            }
            
            buildingsDict[name] = BuildingInfo{
                static_cast<size_t>(properties[0]),  // influenceRadius
                static_cast<size_t>(properties[1]),  // influenceWeight
                static_cast<size_t>(properties[2]),  // influenceSoftExpansion
                properties[3],                       // softEdgeDivFactor
                static_cast<size_t>(properties[4]),  // width
                static_cast<size_t>(properties[5])   // height
            };
        }
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse buildingsDict file '" << dictPath << "': " << e.what() << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid buildingsDict format in '" << dictPath << "': " << e.what() << std::endl;
        std::exit(1);
    }
    
    return;
}

void TerritoryAnalyser::playerDefeated(size_t player) {
    if (player == 0 || player >= playerGrids.size()) return;

    const Config& config = AppConfig::get();
    isPlayerDefeated[player] = true;

    // Re-initialise this player's territory grid (all influence zeroed).
    playerGrids.at(player) = Grid(static_cast<int>(size));

    // Iterate all stored buildings for this player and:
    //   - ranged: re-apply stored influence to player grid; no change needed for team grid
    //             (ranged contribution was never cleared from the team grid).
    //   - non-ranged: remove stored influence from the team grid.
    size_t minRow = 0, maxRow = size, minCol = 0, maxCol = size;

    for (const auto& [id, obj] : buildingInstancesById) {
        if (obj.player != player) continue;

        const bool isRanged = std::find(rangedBuildings.begin(), rangedBuildings.end(), obj.building) != rangedBuildings.end();
        const size_t team = obj.team;

        for (size_t i = 0; i < obj.influenceBoard.size(); ++i) {
            for (size_t j = 0; j < obj.influenceBoard[i].size(); ++j) {
                const double val = obj.influenceBoard[i][j];
                if (val == 0.0) continue;
                const size_t row = obj.influenceMinRow + i;
                const size_t col = obj.influenceMinCol + j;
                if (row >= size || col >= size) continue;

                minRow = std::min(minRow, row);
                maxRow = std::max(maxRow, row + 1);
                minCol = std::min(minCol, col);
                maxCol = std::max(maxCol, col + 1);

                if (isRanged) {
                    // Re-add ranged influence to the fresh player grid.
                    playerGrids.at(player).setValue(row, col,
                        playerGrids.at(player).getValue(row, col) + val);
                } else {
                    // Remove non-ranged contribution from the shared team grid.
                    if (team > 0 && team < teamGrids.size()) {
                        teamGrids.at(team).setValue(row, col,
                            teamGrids.at(team).getValue(row, col) - val);
                    }
                }
            }
        }
    }

    // Keep bool territory in sync with dataTruth for render and merge passes.
    const auto bounds = std::make_tuple(minRow, maxRow, minCol, maxCol);
    playerGrids.at(player).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
    teamGrids.at(teamAssignments.at(player)).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
}