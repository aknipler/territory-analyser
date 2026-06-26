

#include "../include/territory_analyser.h"
#include "../include/header.h"
// #include "grid.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <queue>

// ---------- TerritoryAnalyser constructor ----------

/**
 * @brief Initialises all boards and grids for givenSize x givenSize maps with numPlayers players and
 *        numTeams teams.  Loads the obstructions dictionary and initial state from JSON/command files,
 *        runs a full territory merge, and seeds both player and team fill analyses.
 */
// Constructor to initialize the dynamic 2D array
TerritoryAnalyser::TerritoryAnalyser(size_t givenSize, size_t numPlayers, size_t numTeams, 
    std::map<int, int> teamAssignments, size_t threshold, const std::string& initialStatePath) 
    : playerGrids(numPlayers + 1, Grid(givenSize)),
    teamGrids(numTeams + 1, Grid(givenSize)),
    size(givenSize), numPlayers(numPlayers), numTeams(numTeams), 
    teamAssignments(teamAssignments), threshold(threshold), 
    seed(std::chrono::high_resolution_clock::now().time_since_epoch().count()), gaiaBoard(givenSize, 
        std::vector<size_t>(givenSize, 0)),

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

    obstructionsDict(), rangedObstructions(), 
    isPlayerDefeated(numPlayers + 1, false)
                          {

    // If no initial game state provided, throw an error.
    if (initialStatePath.empty()) {
        std::cerr << "Error: No initial game state path provided." << std::endl;
        exit(1);
    }


    // initialise nonwalkable terrain
    measureAndLogExecutionTime<int>("initialise walkable terrain)", 
        std::function<int()>([this]() { this->initWalkableTerrain(); return 0; } )
    );



    if (teamAssignments.size() != numPlayers) {
        std::cerr << "Error: teamAssignments size must match numPlayers." << std::endl;
        exit(1);
    }
    // Load obstructionsDict and rangedObstructions from JSON file
    measureAndLogExecutionTime<int>("load obstructions dict", 
        std::function<int()>([this]() { this->loadObstructionsDict("obstructionsDict.json"); return 0; } )
    );

    // Setup the initial state of the map (can change this to integrate with your system)
    // This should be replaced by batchAddObstructions() when it is ready. Current implementation is incredibly slow.
    measureAndLogExecutionTime<int>("load obstruction commands from file", 
        std::function<int()>([this, initialStatePath]() { loadObstructionCommandsFromFile(*this, initialStatePath); return 0; } )
    );

    // if (!loadObstructionCommandsFromFile(*this, initialStatePath)) {
    //     std::cerr << "Error: Failed to load obstruction commands from file: " << initialStatePath << std::endl;
    //     std::cout << "Please ensure the file exists and is properly formatted." << std::endl;
    //     exit(1);
    // }

    measureAndLogExecutionTime<int>("mapMergedTerritories", 
        std::function<int()>([this]() { this->mapMergedTerritories(); return 0; } )
    );

    
    auto fillResultPlayers = initialiseFill(getMasterBoard("player"), masterPlayerObstructionBoard, 
                                    playerObstructionBoards, numPlayers, &WalkableTerrainBoard, &isPlayerDefeated);
    playerFills = fillResultPlayers.fills;
    playerGaps = fillResultPlayers.gaps;
    masterPlayerBoardFill = fillResultPlayers.fillBoard;
    playerFillAssignmentBoard = std::move(fillResultPlayers.fillAssignmentBoard);
    playerInteriorGroups = std::move(fillResultPlayers.interiorGroups);

    auto fillResultTeams = initialiseFill(getMasterBoard("team"), masterTeamObstructionBoard,
                                    teamObstructionBoards, numTeams, &WalkableTerrainBoard);
    teamFills = fillResultTeams.fills;
    teamGaps = fillResultTeams.gaps;
    masterTeamBoardFill = fillResultTeams.fillBoard;
    teamFillAssignmentBoard = std::move(fillResultTeams.fillAssignmentBoard);
    teamInteriorGroups = std::move(fillResultTeams.interiorGroups);
}


// ---------- initWalkableTerrain ----------

/**
 * @brief Populates WalkableTerrainBoard with a hardcoded example water body (cells outside a
 *        circular radius are set to 0 = non-walkable).  In production this would be replaced
 *        by reading actual terrain data from the game state.
 */
void TerritoryAnalyser::initWalkableTerrain() {
    // This function identifies non-walkable terrain edges
    // For simplicity, let's assume that any cell with a value of 0 in the gaiaBoard is walkable terrain,
    // and any cell with a value of 1 is walkable terrain (0 = non-walkable/water).

    Config config = AppConfig::get();

    // Read walkable terrain from AAAImageWater.txt (each row is a string of '0'/'1' chars;
    // '1' = walkable land, '0' = non-walkable water).
    std::vector<std::vector<size_t>> WalkableTerrain(size, std::vector<size_t>(size, 1));

    std::ifstream waterFile("../examples/AAAImageWater.txt");
    if (!waterFile.is_open()) {
        std::cerr << "Warning: Could not open " << std::filesystem::current_path() << " + ../examples/AAAImageWater.txt" << ". Defaulting to all-walkable terrain." << std::endl;
    } else {
        std::string line;
        size_t row = 0;
        while (row < size && std::getline(waterFile, line)) {
            for (size_t col = 0; col < size && col < line.size(); ++col) {
                WalkableTerrain[row][col] = (line[col] == '1') ? 1 : 0;
            }
            ++row;
        }
    }

    // store walkable terrain board (1 = walkable, 0 = water/non-walkable)
    WalkableTerrainBoard = WalkableTerrain;

    return;
}


// ---------- hasNearbyObstructions ----------

// Returns true if any obstruction exists within Chebyshev distance 2 of
// the given footprint [x, x+width) x [y, y+height), excluding the footprint cells
// themselves.
//
// Called after updateObstructionBoards() so masterPlayerObstructionBoard reflects the
// current game state. A non-empty ring means an incremental fill recomputation
// (slow path) is needed; an empty ring means no existing fill shape can be split
// or merged by this change (fast path: only footprint cells need evicting/restoring
// in fillAssignmentBoard).
//
// x, y          - top-left corner of the footprint
// width, height - dimensions of the footprint
//
// Returns: true = another obstruction within 2 tiles (slow path needed);
//          false = isolated footprint (fast path safe).
bool TerritoryAnalyser::hasNearbyObstructions(size_t x, size_t y, size_t width, size_t height) const {
    const int fx = static_cast<int>(x),  fy = static_cast<int>(y);
    const int fw = static_cast<int>(width), fh = static_cast<int>(height);
    const int mapSize = static_cast<int>(size);

    for (int cx = std::max(0, fx - 2); cx <= std::min(mapSize - 1, fx + fw + 1); ++cx) {
        for (int cy = std::max(0, fy - 2); cy <= std::min(mapSize - 1, fy + fh + 1); ++cy) {
            // Skip cells inside the footprint itself.
            if (cx >= fx && cx < fx + fw && cy >= fy && cy < fy + fh) continue;
            if (masterPlayerObstructionBoard[cx][cy] != -1) return true;
        }
    }
    return false;
}


// ---------- updateObstructionState ----------

/**
 * @brief Performs all territory/obstruction/DSU state updates for a obstruction add/remove,
 *        but does NOT update fills.  Use this during initial state loading (before initialiseFill
 *        has been called).  Returns a result whose shouldProceed is false on early-return paths.
 */
TerritoryAnalyser::ObstructionStateResult TerritoryAnalyser::updateObstructionState(
    size_t x, size_t y, const std::string& obstruction, int player, const std::string& mod) {

    ObstructionInfo info;
    int team = 0;
    if (!lookupObstructionAndTeam(obstruction, player, info, team)) return {};

    // For defeated players removing non-ranged obstructions: obstructions still update normally
    // but territory is already cleared, so skip all territory-related work and return.
    const bool isRangedObstruction = std::find(rangedObstructions.begin(), rangedObstructions.end(), obstruction) != rangedObstructions.end();
    if (mod == "remove" && player > 0
        && static_cast<size_t>(player) < isPlayerDefeated.size()
        && isPlayerDefeated[static_cast<size_t>(player)]
        && !isRangedObstruction) {
        updateObstructionBoards(x, y, info.width, info.height, player, team, mod);
        return {};
    }

    const PlayerObject obstructionInstance = makeObstructionInstance(x, y, obstruction, player, team, info);
    const auto impactedObstructions = collectAttributedObstructionsInFootprint(x, y, info.width, info.height);
    auto [impactedPlayers, impactedTeams] = collectImpactedOwners(player, team, impactedObstructions);

    // For "remove": capture DSU group bounds BEFORE performTerritoryUpdateWithDSUSync
    // calls removeFromConnectedObstructions (which erases the group data).
    // Default to the footprint bounds as a safe fallback.
    std::vector<size_t> dsuGroupBounds = {x, x + info.width - 1, y, y + info.height - 1};
    if (mod == "remove") {
        const ObstructionInstanceKey remKey{
            static_cast<size_t>(player), static_cast<size_t>(team),
            obstruction, Position{x, y}
        };
        const auto remKeyIt = obstructionInstanceIdsByKey.find(remKey);
        if (remKeyIt != obstructionInstanceIdsByKey.end() && !remKeyIt->second.empty()) {
            const size_t remId = remKeyIt->second.back();
            if (dsuParent_.count(remId)) {
                const size_t root = dsuFind(remId);
                const auto groupIt = connectedGroupData_.find(root);
                if (groupIt != connectedGroupData_.end() && groupIt->second.bounds.size() >= 4)
                    dsuGroupBounds = groupIt->second.bounds;
            }
        }
    }

    auto bounds = performTerritoryUpdateWithDSUSync(obstructionInstance, mod);

    // For "add": capture DSU group bounds AFTER performTerritoryUpdateWithDSUSync
    // calls addToConnectedObstructions (which creates/updates the group).
    if (mod == "add") {
        const ObstructionInstanceKey newKey{
            static_cast<size_t>(player), static_cast<size_t>(team),
            obstruction, Position{x, y}
        };
        const auto newKeyIt = obstructionInstanceIdsByKey.find(newKey);
        if (newKeyIt != obstructionInstanceIdsByKey.end() && !newKeyIt->second.empty()) {
            const size_t newId = newKeyIt->second.back();
            if (dsuParent_.count(newId)) {
                const size_t root = dsuFind(newId);
                const auto groupIt = connectedGroupData_.find(root);
                if (groupIt != connectedGroupData_.end() && groupIt->second.bounds.size() >= 4)
                    dsuGroupBounds = groupIt->second.bounds;
            }
        }
    }

    updateObstructionBoards(x, y, info.width, info.height, player, team, mod);
    const bool isFastPath = !hasNearbyObstructions(x, y, info.width, info.height);
    bounds = mergeBounds(bounds, reapplyAttributedObstructions(impactedObstructions));

    refreshOwnerBoolPasses(impactedPlayers, impactedTeams, bounds);
    rebuildMasterBoards();

    return {true, info, isFastPath, dsuGroupBounds};
}


/**
 * @brief Performs all territory/obstruction/DSU state updates for a obstruction add/remove,
 *        but does NOT update fills.  Use this during initial state loading (before initialiseFill
 *        has been called).  Returns a result whose shouldProceed is false on early-return paths.
 */
TerritoryAnalyser::ObstructionStateResult TerritoryAnalyser::batchAddObstructions(
    std::string filePath) {


    // Step 0: Read the file and hold it in memory

    // Step 1: Loop through all obstructions, add them to the obstruction boards using something like 
    // makeObstructionInstance and updateObstructionBoards?
    // Finish loop.

    // Step 2: Loop through all obstructions, add territory around them using performTerritoryUpdateWithDSUSync? 
    // Or just update Territory?
    // Finish loop.

    // Step 3: Loop through all obstructions, update DSU connectivity? Could this be done in step 1 instead?

    // Step 4: make boolean boards and master boards (refreshOwnerBoolPasses and rebuildMasterBoards)

    ObstructionInfo info;
    int team = 0;
    // if (!lookupObstructionAndTeam(obstruction, player, info, team)) return {};
    // const PlayerObject obstructionInstance = makeObstructionInstance(x, y, obstruction, player, team, info); // 1



    // auto bounds = performTerritoryUpdateWithDSUSync(obstructionInstance, "add"); // 2



    // For "add": capture DSU group bounds AFTER performTerritoryUpdateWithDSUSync
    // calls addToConnectedObstructions (which creates/updates the group).
    // std::vector<size_t> dsuGroupBounds = {x, x + info.width - 1, y, y + info.height - 1}; // 3
    // // if (mod == "add") {
    //     const ObstructionInstanceKey newKey{
    //         static_cast<size_t>(player), static_cast<size_t>(team),
    //         obstruction, Position{x, y}
    //     };
    //     const auto newKeyIt = obstructionInstanceIdsByKey.find(newKey);
    //     if (newKeyIt != obstructionInstanceIdsByKey.end() && !newKeyIt->second.empty()) {
    //         const size_t newId = newKeyIt->second.back();
    //         if (dsuParent_.count(newId)) {
    //             const size_t root = dsuFind(newId);
    //             const auto groupIt = connectedGroupData_.find(root);
    //             if (groupIt != connectedGroupData_.end() && groupIt->second.bounds.size() >= 4)
    //                 dsuGroupBounds = groupIt->second.bounds;
    //         }
    //     }
    // // }


    // updateObstructionBoards(x, y, info.width, info.height, player, team, mod);


    // std::unordered_set<size_t> players, teams;
    // for (size_t i = 0; i < numPlayers; ++i) players.insert(i);
    // for (size_t i = 0; i < numTeams; ++i) teams.insert(i);
    // refreshOwnerBoolPasses(players, teams, bounds);
    // rebuildMasterBoards();


    // return {true, info, isFastPath, dsuGroupBounds};
}

/**
 * @brief Main entry point for adding or removing an obstruction.  Delegates all state work to
 *        updateObstructionState, then triggers fill recomputation for all affected players/teams.
 */
void TerritoryAnalyser::updateObstruction(size_t x, size_t y, std::string obstruction, int player, std::string mod) {
    auto result = updateObstructionState(x, y, obstruction, player, mod);
    if (!result.shouldProceed) return;
    updatePlayerAndTeamFills(x, y, result.info, result.isFastPath, mod, result.dsuGroupBounds);
}

// ---------- lookupObstructionAndTeam ----------

bool TerritoryAnalyser::lookupObstructionAndTeam(const std::string& obstruction, int player, ObstructionInfo& infoOut, int& teamOut) {
    const auto& config = AppConfig::get();
    try {
        infoOut = obstructionsDict.at(obstruction);
    } catch (const std::out_of_range&) {
        std::cerr << "Error: Obstruction type '" << obstruction << "' not found in obstructionsDict." << std::endl;
        return false;
    }
    teamOut = 0;
    if (player > 0) {
        const auto teamIt = config.teamAssignments.find(player);
        if (teamIt == config.teamAssignments.end()) {
            std::cerr << "Error: No team assignment found for player " << player << "." << std::endl;
            return false;
        }
        teamOut = teamIt->second;
    }
    return true;
}

// ---------- makeObstructionInstance ----------

PlayerObject TerritoryAnalyser::makeObstructionInstance(size_t x, size_t y, const std::string& obstruction, int player, int team, const ObstructionInfo& info) const {
    PlayerObject obstructionInstance;
    obstructionInstance.instanceId = 0;
    obstructionInstance.player = static_cast<size_t>(player);
    obstructionInstance.team = static_cast<size_t>(team);
    obstructionInstance.obstruction = obstruction;
    obstructionInstance.info = info;
    obstructionInstance.position = Position{x, y};
    obstructionInstance.influenceMinY = 0;
    obstructionInstance.influenceMinX = 0;
    return obstructionInstance;
}

// ---------- collectImpactedOwners ----------

std::pair<std::unordered_set<size_t>, std::unordered_set<size_t>>
TerritoryAnalyser::collectImpactedOwners(int player, int team, const std::unordered_set<size_t>& impactedObstructions) const {
    std::unordered_set<size_t> impactedPlayers;
    std::unordered_set<size_t> impactedTeams;
    if (player > 0) {
        impactedPlayers.insert(static_cast<size_t>(player));
        impactedTeams.insert(static_cast<size_t>(team));
    }
    for (const size_t id : impactedObstructions) {
        const auto it = obstructionInstancesById.find(id);
        if (it == obstructionInstancesById.end()) continue;
        if (it->second.player > 0) {
            impactedPlayers.insert(it->second.player);
            impactedTeams.insert(it->second.team);
        }
    }
    return {impactedPlayers, impactedTeams};
}

// ---------- performTerritoryUpdateWithDSUSync ----------

std::tuple<size_t, size_t, size_t, size_t>
TerritoryAnalyser::performTerritoryUpdateWithDSUSync(PlayerObject obstructionInstance, const std::string& mod) {
    if (mod == "remove") {
        // Must run before updateTerritory so that dsuParent_, obstructionInstancesById,
        // and cellObstructionAttribution still contain the removed obstruction.
        const ObstructionInstanceKey remKey{
            obstructionInstance.player, obstructionInstance.team,
            obstructionInstance.obstruction, obstructionInstance.position
        };
        const auto remKeyIt = obstructionInstanceIdsByKey.find(remKey);
        if (remKeyIt != obstructionInstanceIdsByKey.end() && !remKeyIt->second.empty()) {
            removeFromConnectedObstructions(remKeyIt->second.back());
        }
    }

    auto bounds = updateTerritory(obstructionInstance, obstructionInstance.player, mod);

    if (mod == "add") {
        // Resolve the instanceId assigned inside updateTerritory via the key lookup.
        const ObstructionInstanceKey newKey{
            obstructionInstance.player, obstructionInstance.team,
            obstructionInstance.obstruction, obstructionInstance.position
        };
        const auto newKeyIt = obstructionInstanceIdsByKey.find(newKey);
        if (newKeyIt != obstructionInstanceIdsByKey.end() && !newKeyIt->second.empty()) {
            const size_t newInstanceId = newKeyIt->second.back();
            addToConnectedObstructions(newInstanceId,
                obstructionInstance.position.first, obstructionInstance.position.second,
                obstructionInstance.info.width, obstructionInstance.info.height);
        }
    }

    return bounds;
}

// ---------- refreshOwnerBoolPasses ----------

void TerritoryAnalyser::refreshOwnerBoolPasses(
    const std::unordered_set<size_t>& impactedPlayers,
    const std::unordered_set<size_t>& impactedTeams,
    const std::tuple<size_t, size_t, size_t, size_t>& bounds) {
    const auto& config = AppConfig::get();
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
}

// ---------- rebuildMasterBoards ----------

void TerritoryAnalyser::rebuildMasterBoards() {
    mapMergedTerritories();
    const auto& config = AppConfig::get();
    if (config.contestedTerritoryMethod == "growth") {
        resolveContestedTerritoryGrowth();
    }
}

// ---------- updatePlayerAndTeamFills ----------

void TerritoryAnalyser::updatePlayerAndTeamFills(size_t x, size_t y, const ObstructionInfo& info, bool isFastPath, const std::string& mod, const std::vector<size_t>& dsuGroupBounds) {
    const size_t influenceExtent = info.influenceRadius + info.influenceSoftExpansion;
    auto playerFillResult = updateFill(
        x, y, info.width, info.height, influenceExtent, isFastPath, mod,
        playerFills, playerFillAssignmentBoard, masterPlayerBoardFill, playerGaps,
        getMasterBoard("player"), masterPlayerObstructionBoard, playerObstructionBoards,
        numPlayers, dsuGroupBounds, &WalkableTerrainBoard, &isPlayerDefeated, "player");
    if (AppConfig::get().validateIncrementalFills) {
        validateAgainstFullRecompute(playerFillResult, "player " + mod,
            getMasterBoard("player"), masterPlayerObstructionBoard, playerObstructionBoards,
            numPlayers, &WalkableTerrainBoard, &isPlayerDefeated);
    }
    playerFills               = std::move(playerFillResult.fills);
    playerGaps                = std::move(playerFillResult.gaps);
    masterPlayerBoardFill     = std::move(playerFillResult.fillBoard);
    playerFillAssignmentBoard = std::move(playerFillResult.fillAssignmentBoard);
    playerInteriorGroups      = std::move(playerFillResult.interiorGroups);

    auto teamFillResult = updateFill(
        x, y, info.width, info.height, influenceExtent, isFastPath, mod,
        teamFills, teamFillAssignmentBoard, masterTeamBoardFill, teamGaps,
        getMasterBoard("team"), masterTeamObstructionBoard, teamObstructionBoards,
        numTeams, dsuGroupBounds, &WalkableTerrainBoard, nullptr, "team");
    if (AppConfig::get().validateIncrementalFills) {
        validateAgainstFullRecompute(teamFillResult, "team " + mod,
            getMasterBoard("team"), masterTeamObstructionBoard, teamObstructionBoards,
            numTeams, &WalkableTerrainBoard, nullptr);
    }
    teamFills               = std::move(teamFillResult.fills);
    teamGaps                = std::move(teamFillResult.gaps);
    masterTeamBoardFill     = std::move(teamFillResult.fillBoard);
    teamFillAssignmentBoard = std::move(teamFillResult.fillAssignmentBoard);
    teamInteriorGroups      = std::move(teamFillResult.interiorGroups);
}

bool TerritoryAnalyser::obstructionTouchesMapEdge(const PlayerObject& b) const {
    return b.position.first == 0
        || b.position.second == 0
        || b.position.first + b.info.width >= size
        || b.position.second + b.info.height >= size;
}

// ---------- checkMapEdgeClosure ----------

/**
 * @brief Returns true when the map-edge obstructions in group form at least two separate connected
 *        components that touch the map boundary, meaning the group can be considered closed via
 *        the map edge (the edge acts as an implicit wall segment).
 */
bool TerritoryAnalyser::checkMapEdgeClosure(const ConnectedObstructions& group) const {
    if (group.mapEdgeObstructionIds.size() < 2) return false;

    // Collect live PlayerObjects for each map-edge obstruction.
    std::vector<const PlayerObject*> edgeObstructions;
    edgeObstructions.reserve(group.mapEdgeObstructionIds.size());
    for (const size_t id : group.mapEdgeObstructionIds) {
        const auto it = obstructionInstancesById.find(id);
        if (it != obstructionInstancesById.end())
            edgeObstructions.push_back(&it->second);
    }
    if (edgeObstructions.size() < 2) return false;

    // BFS over map-edge obstructions to count separate contact clusters.
    // Closedness requires >= 2 distinct clusters (two separate touches of the boundary).
    const auto touches = [](const PlayerObject& a, const PlayerObject& b) {
        const int ax = static_cast<int>(a.position.first), ay = static_cast<int>(a.position.second);
        const int aw = static_cast<int>(a.info.width),    ah = static_cast<int>(a.info.height);
        const int bx = static_cast<int>(b.position.first), by = static_cast<int>(b.position.second);
        const int bw = static_cast<int>(b.info.width),    bh = static_cast<int>(b.info.height);
        return std::max(0, std::max(ax - (bx + bw), bx - (ax + aw))) == 0
            && std::max(0, std::max(ay - (by + bh), by - (ay + ah))) == 0;
    };

    const size_t M = edgeObstructions.size();
    std::vector<bool> visited(M, false);
    std::vector<size_t> frontier;
    size_t components = 0;
    for (size_t i = 0; i < M; ++i) {
        if (visited[i]) continue;
        ++components;
        if (components >= 2) return true;
        frontier.push_back(i);
        visited[i] = true;
        while (!frontier.empty()) {
            const size_t cur = frontier.back(); frontier.pop_back();
            for (size_t j = 0; j < M; ++j) {
                if (!visited[j] && touches(*edgeObstructions[cur], *edgeObstructions[j])) {
                    visited[j] = true;
                    frontier.push_back(j);
                }
            }
        }
    }
    return false;
}

// ---------- addToConnectedObstructions ----------

/**
 * @brief Registers newInstanceId in the DSU and merges it with any touching existing obstructions
 *        (footprint gap <= 0 in both axes).  If the new connection creates a cycle within an
 *        already-connected component, marks that component as closed.
 */
void TerritoryAnalyser::addToConnectedObstructions(size_t newInstanceId, size_t x, size_t y, size_t width, size_t height) {
    const PlayerObject& newObj = obstructionInstancesById.at(newInstanceId);

    // Scan the 1-cell border ring around the footprint in cellObstructionAttribution
    // (O(perimeter)) to find candidate neighbours, then confirm strict adjacency.
    const int fx = static_cast<int>(x);
    const int fy = static_cast<int>(y);
    const int fw = static_cast<int>(width);
    const int fh = static_cast<int>(height);
    const int mapSize = static_cast<int>(size);

    std::unordered_set<size_t> candidateIds;
    const auto gatherCandidates = [&](int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= mapSize || cy >= mapSize) return;
        const auto it = cellObstructionAttribution.find({static_cast<size_t>(cx), static_cast<size_t>(cy)});
        if (it != cellObstructionAttribution.end())
            candidateIds.insert(it->second.begin(), it->second.end());
    };
    for (int cx = fx - 1; cx <= fx + fw; ++cx) {
        gatherCandidates(cx, fy - 1);
        gatherCandidates(cx, fy + fh);
    }
    for (int cy = fy; cy < fy + fh; ++cy) {
        gatherCandidates(fx - 1, cy);
        gatherCandidates(fx + fw, cy);
    }
    candidateIds.erase(newInstanceId);

    // Register the new obstruction as its own DSU node and group.
    dsuParent_[newInstanceId] = newInstanceId;
    dsuRank_[newInstanceId] = 0;
    ConnectedObstructions& newGroup = connectedGroupData_[newInstanceId];
    newGroup.obstructions.insert(newObj);
    newGroup.bounds = {x, x + width - 1, y, y + height - 1};
    if (obstructionTouchesMapEdge(newObj))
        newGroup.mapEdgeObstructionIds.insert(newInstanceId);

    // Union with every touching obstruction; DSU merges groups automatically.
    // Skip candidates with no DSU node (safety guard: they may have been
    // removed or not yet registered).
    for (const size_t candidateId : candidateIds) {
        if (!dsuParent_.count(candidateId)) continue;
        const auto candidateIt = obstructionInstancesById.find(candidateId);
        if (candidateIt == obstructionInstancesById.end()) continue;
        const PlayerObject& candidate = candidateIt->second;
        const int bx = static_cast<int>(candidate.position.first);
        const int by = static_cast<int>(candidate.position.second);
        const int cbw = static_cast<int>(candidate.info.width);
        const int cbh = static_cast<int>(candidate.info.height);
        const int xGap = std::max(0, std::max(fx - (bx + cbw), bx - (fx + fw)));
        const int yGap = std::max(0, std::max(fy - (by + cbh), by - (fy + fh)));
        if (xGap == 0 && yGap == 0) {
            const size_t ra = dsuFind(newInstanceId);
            const size_t rb = dsuFind(candidateId);
            if (ra == rb) {
                // Both ends already in the same component: this extra adjacency
                // edge creates a cycle, meaning the group forms a closed ring.
                connectedGroupData_[ra].closed = true;
            } else {
                dsuUnion(newInstanceId, candidateId);
            }
        }
    }
}

// ---------- removeFromConnectedObstructions ----------

/**
 * @brief Removes removedId from the DSU, then reconstructs fresh DSU nodes and ConnectedObstructions
 *        groups for the surviving mates by BFS-partitioning them into connected components.
 *        Each new component is re-evaluated for cycle-closure and map-edge closure.
 */
void TerritoryAnalyser::removeFromConnectedObstructions(size_t removedId) {
    if (!dsuParent_.count(removedId)) return;

    const size_t root = dsuFind(removedId);
    const auto groupIt = connectedGroupData_.find(root);
    if (groupIt == connectedGroupData_.end()) {
        dsuParent_.erase(removedId);
        dsuRank_.erase(removedId);
        return;
    }

    // Collect surviving mates into an indexed list for BFS.
    std::vector<PlayerObject> mates;
    mates.reserve(groupIt->second.obstructions.size());
    for (const PlayerObject& b : groupIt->second.obstructions) {
        if (b.instanceId != removedId && obstructionInstancesById.count(b.instanceId))
            mates.push_back(b);
    }

    // Erase all old DSU and group data upfront.
    dsuParent_.erase(removedId);
    dsuRank_.erase(removedId);
    for (const PlayerObject& m : mates) {
        dsuParent_.erase(m.instanceId);
        dsuRank_.erase(m.instanceId);
    }
    connectedGroupData_.erase(root);

    if (mates.empty()) return;

    // Direct footprint adjacency: two obstructions touch when their footprint rectangles
    // are no more than 0 cells apart in both axes (including diagonal contact).
    const auto footprintTouches = [](const PlayerObject& a, const PlayerObject& b) {
        const int ax = static_cast<int>(a.position.first),  ay = static_cast<int>(a.position.second);
        const int aw = static_cast<int>(a.info.width),      ah = static_cast<int>(a.info.height);
        const int bx = static_cast<int>(b.position.first),  by = static_cast<int>(b.position.second);
        const int bw = static_cast<int>(b.info.width),      bh = static_cast<int>(b.info.height);
        return std::max(0, std::max(ax - (bx + bw), bx - (ax + aw))) == 0
            && std::max(0, std::max(ay - (by + bh), by - (ay + ah))) == 0;
    };

    // BFS over mates to discover connected components.
    // N (group size) is typically small, so the O(N²) adjacency scan is negligible.
    const size_t N = mates.size();
    std::vector<size_t> compOf(N, SIZE_MAX);
    size_t numComponents = 0;
    std::vector<size_t> frontier;
    for (size_t i = 0; i < N; ++i) {
        if (compOf[i] != SIZE_MAX) continue;
        frontier.push_back(i);
        compOf[i] = numComponents;
        while (!frontier.empty()) {
            const size_t cur = frontier.back(); frontier.pop_back();
            for (size_t j = 0; j < N; ++j) {
                if (compOf[j] == SIZE_MAX && footprintTouches(mates[cur], mates[j])) {
                    compOf[j] = numComponents;
                    frontier.push_back(j);
                }
            }
        }
        ++numComponents;
    }

    // Determine closure per component: count unique edges (i < j) within each
    // component. A connected graph of K nodes has a cycle iff it has >= K edges.
    // Also gather which mates per component touch the map edge.
    std::vector<size_t> nodeCount(numComponents, 0);
    std::vector<size_t> edgeCount(numComponents, 0);
    std::vector<std::unordered_set<size_t>> compEdgeIds(numComponents);
    for (size_t i = 0; i < N; ++i) {
        nodeCount[compOf[i]]++;
        if (obstructionTouchesMapEdge(mates[i]))
            compEdgeIds[compOf[i]].insert(mates[i].instanceId);
    }
    for (size_t i = 0; i < N; ++i)
        for (size_t j = i + 1; j < N; ++j)
            if (compOf[i] == compOf[j] && footprintTouches(mates[i], mates[j]))
                ++edgeCount[compOf[i]];

    // Build new DSU nodes and ConnectedObstructions data — one group per component.
    // All members point directly to the component root: no union-by-rank needed
    // since we already know the exact membership.
    for (size_t comp = 0; comp < numComponents; ++comp) {
        const bool compClosed = (edgeCount[comp] >= nodeCount[comp]);
        size_t compRoot = SIZE_MAX;
        for (size_t i = 0; i < N; ++i) {
            if (compOf[i] != comp) continue;
            const size_t id = mates[i].instanceId;
            if (compRoot == SIZE_MAX) {
                compRoot = id;
                dsuParent_[id] = id;
                dsuRank_[id] = 0;
                ConnectedObstructions& g = connectedGroupData_[id];
                g.obstructions.insert(mates[i]);
                const size_t px = mates[i].position.first, py = mates[i].position.second;
                g.bounds = {px, px + mates[i].info.width - 1, py, py + mates[i].info.height - 1};
                g.closed = compClosed;
                g.mapEdgeObstructionIds = compEdgeIds[comp];
                g.closedWithMapEdge = (compEdgeIds[comp].size() >= 2) ? checkMapEdgeClosure(g) : false;
            } else {
                dsuParent_[id] = compRoot;
                dsuRank_[id] = 0;
                ConnectedObstructions& g = connectedGroupData_[compRoot];
                g.obstructions.insert(mates[i]);
                const size_t px = mates[i].position.first, py = mates[i].position.second;
                g.bounds[0] = std::min(g.bounds[0], px);
                g.bounds[1] = std::max(g.bounds[1], px + mates[i].info.width - 1);
                g.bounds[2] = std::min(g.bounds[2], py);
                g.bounds[3] = std::max(g.bounds[3], py + mates[i].info.height - 1);
            }
        }
    }
}

size_t TerritoryAnalyser::dsuFind(size_t id) {
    // Path halving: avoids recursion, still achieves inverse-Ackermann amortized cost.
    while (dsuParent_[id] != id) {
        dsuParent_[id] = dsuParent_[dsuParent_[id]];
        id = dsuParent_[id];
    }
    return id;
}

// ---------- dsuUnion ----------

/**
 * @brief Merges the DSU trees rooted at a and b (union by rank) and combines the corresponding
 *        ConnectedObstructions groups: obstructions, bounds, closure flags, and map-edge obstruction ids.
 */
void TerritoryAnalyser::dsuUnion(size_t a, size_t b) {
    size_t ra = dsuFind(a);
    size_t rb = dsuFind(b);
    if (ra == rb) return;
    // Union by rank: attach the lower-rank root under the higher-rank root.
    if (dsuRank_[ra] < dsuRank_[rb]) std::swap(ra, rb);
    dsuParent_[rb] = ra;
    if (dsuRank_[ra] == dsuRank_[rb]) ++dsuRank_[ra];
    // Merge group data from the dying root (rb) into the survivor (ra).
    ConnectedObstructions& ga = connectedGroupData_[ra];
    ConnectedObstructions& gb = connectedGroupData_[rb];
    for (const PlayerObject& b : gb.obstructions)
        ga.obstructions.insert(b);
    ga.bounds[0] = std::min(ga.bounds[0], gb.bounds[0]);
    ga.bounds[1] = std::max(ga.bounds[1], gb.bounds[1]);
    ga.bounds[2] = std::min(ga.bounds[2], gb.bounds[2]);
    ga.bounds[3] = std::max(ga.bounds[3], gb.bounds[3]);
    ga.closed = ga.closed || gb.closed;
    ga.mapEdgeObstructionIds.insert(gb.mapEdgeObstructionIds.begin(), gb.mapEdgeObstructionIds.end());
    if (ga.mapEdgeObstructionIds.size() >= 2)
        ga.closedWithMapEdge = checkMapEdgeClosure(ga);
    else
        ga.closedWithMapEdge = false;
    connectedGroupData_.erase(rb);
}

// ---------- collectAttributedObstructionsInFootprint ----------

/**
 * @brief Returns the set of obstruction instance ids whose influence radius covers at least one cell
 *        in the footprint [x, x+width) x [y, y+height), queried via cellObstructionAttribution.
 */
std::unordered_set<size_t> TerritoryAnalyser::collectAttributedObstructionsInFootprint(size_t x, size_t y, size_t width, size_t height) const {
    std::unordered_set<size_t> impactedObstructions;

    for (size_t currentX = x; currentX < x + width && currentX < size; ++currentX) {
        for (size_t currentY = y; currentY < y + height && currentY < size; ++currentY) {
            const auto attributionIt = cellObstructionAttribution.find(Position{currentX, currentY});
            if (attributionIt == cellObstructionAttribution.end()) {
                continue;
            }

            impactedObstructions.insert(attributionIt->second.begin(), attributionIt->second.end());
        }
    }

    return impactedObstructions;
}

// ---------- updateCellAttributionForObstruction ----------

/**
 * @brief Adds or removes obstructionInstance's id from cellObstructionAttribution for every cell within
 *        its full influence area (footprint + influenceRadius + influenceSoftExpansion).
 *        Used to track which obstructions affect each map cell for incremental update queries.
 */
void TerritoryAnalyser::updateCellAttributionForObstruction(const PlayerObject& obstructionInstance, std::string mod) {
    const size_t obstructionId = obstructionInstance.instanceId;

    const size_t centerX = obstructionInstance.position.first;
    const size_t centerY = obstructionInstance.position.second;
    const ObstructionInfo& info = obstructionInstance.info;

    const size_t r = info.influenceRadius;
    const size_t bh = info.height;
    const size_t bw = info.width;
    const size_t softEdge = info.influenceSoftExpansion;

    const double rectMinX = static_cast<double>(centerX);
    const double rectMaxX = static_cast<double>(centerX + bw - 1);
    const double rectMinY = static_cast<double>(centerY);
    const double rectMaxY = static_cast<double>(centerY + bh - 1);

    const size_t minX = std::max(0, static_cast<int>(centerX) - static_cast<int>(r) - static_cast<int>(softEdge));
    const size_t maxX = std::min(static_cast<int>(size), static_cast<int>(centerX + bw + r + softEdge));
    const size_t minY = std::max(0, static_cast<int>(centerY) - static_cast<int>(r) - static_cast<int>(softEdge));
    const size_t maxY = std::min(static_cast<int>(size), static_cast<int>(centerY + bh + r + softEdge));
    const double attributionRadius = static_cast<double>(r + softEdge);

    for (size_t x = minX; x < maxX; ++x) {
        const double dx = std::max(0.0, std::max(rectMinX - static_cast<double>(x), static_cast<double>(x) - rectMaxX));
        for (size_t y = minY; y < maxY; ++y) {
            const double dy = std::max(0.0, std::max(rectMinY - static_cast<double>(y), static_cast<double>(y) - rectMaxY));
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > attributionRadius) {
                continue;
            }

            const Position cell{x, y};
            if (mod == "add") {
                cellObstructionAttribution[cell].insert(obstructionId);
            } else {
                auto cellIt = cellObstructionAttribution.find(cell);
                if (cellIt == cellObstructionAttribution.end()) {
                    continue;
                }

                cellIt->second.erase(obstructionId);
                if (cellIt->second.empty()) {
                    cellObstructionAttribution.erase(cellIt);
                }
            }
        }
    }
}

// ---------- reapplyAttributedObstructions ----------

/**
 * @brief Remove-then-readd each obstruction in obstructionIds (except skipId) to recompute their
 *        stored influence boards after an obstruction change that may have altered path distances.
 * @return The union of all territory bounds affected.
 */
std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> TerritoryAnalyser::reapplyAttributedObstructions(
    const std::unordered_set<size_t>& obstructionIds,
    size_t skipId) {

    std::tuple<size_t, size_t, size_t, size_t> mergedBounds = std::make_tuple(0U, 0U, 0U, 0U);

    for (const size_t id : obstructionIds) {
        if (id == skipId) {
            continue;
        }

        const auto obstructionIt = obstructionInstancesById.find(id);
        if (obstructionIt == obstructionInstancesById.end()) {
            continue;
        }

        const PlayerObject affectedObstruction = obstructionIt->second;
        const auto removalBounds = updateTerritory(affectedObstruction, affectedObstruction.player, "remove");
        const auto addBounds = updateTerritory(affectedObstruction, affectedObstruction.player, "add");

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

    size_t lMinX, lMaxX, lMinY, lMaxY;
    size_t rMinX, rMaxX, rMinY, rMaxY;
    std::tie(lMinX, lMaxX, lMinY, lMaxY) = lhs;
    std::tie(rMinX, rMaxX, rMinY, rMaxY) = rhs;

    return std::make_tuple(
        std::min(lMinX, rMinX),
        std::max(lMaxX, rMaxX),
        std::min(lMinY, rMinY),
        std::max(lMaxY, rMaxY));
}

// ---------- updateObstructionBoards ----------

/**
 * @brief Updates the per-player and per-team obstruction boards and counts for the footprint
 *        [x, x+bw) x [y, y+bh), then rebuilds the master bitmask boards for each affected cell.
 */
void TerritoryAnalyser::updateObstructionBoards(size_t x, size_t y, size_t bw, size_t bh, int player, int team, std::string mod) {

    const bool isAdd = (mod == "add");

    // Footprint semantics are [x, x + bw) in width and [y, y + bh) in height.
    for (size_t currentX = x; currentX < x + bw && currentX < size; ++currentX) {
        for (size_t currentY = y; currentY < y + bh && currentY < size; ++currentY) {
            const size_t playerIndex = static_cast<size_t>(player);
            const size_t teamIndex = static_cast<size_t>(team);

            if (isAdd) {
                playerObstructionCounts.at(playerIndex)[currentX][currentY] += 1;
                teamObstructionCounts.at(teamIndex)[currentX][currentY] += 1;
            } else {
                if (playerObstructionCounts.at(playerIndex)[currentX][currentY] > 0) {
                    playerObstructionCounts.at(playerIndex)[currentX][currentY] -= 1;
                }
                if (teamObstructionCounts.at(teamIndex)[currentX][currentY] > 0) {
                    teamObstructionCounts.at(teamIndex)[currentX][currentY] -= 1;
                }
            }

            playerObstructionBoards.at(playerIndex)[currentX][currentY] = (playerObstructionCounts.at(playerIndex)[currentX][currentY] > 0);
            teamObstructionBoards.at(teamIndex)[currentX][currentY] = (teamObstructionCounts.at(teamIndex)[currentX][currentY] > 0);

            int playerMask = 0;
            for (size_t playerId = 1; playerId <= numPlayers; ++playerId) {
                if (playerObstructionBoards.at(playerId)[currentX][currentY]) {
                    playerMask |= (1 << static_cast<int>(playerId - 1));
                }
            }
            if (playerMask != 0) {
                masterPlayerObstructionBoard[currentX][currentY] = playerMask;
            } else if (playerObstructionBoards.at(0)[currentX][currentY]) {
                masterPlayerObstructionBoard[currentX][currentY] = 0;
            } else {
                masterPlayerObstructionBoard[currentX][currentY] = -1;
            }

            int teamMask = 0;
            for (size_t teamId = 1; teamId <= numTeams; ++teamId) {
                if (teamObstructionBoards.at(teamId)[currentX][currentY]) {
                    teamMask |= (1 << static_cast<int>(teamId - 1));
                }
            }
            if (teamMask != 0) {
                masterTeamObstructionBoard[currentX][currentY] = teamMask;
            } else if (teamObstructionBoards.at(0)[currentX][currentY]) {
                masterTeamObstructionBoard[currentX][currentY] = 0;
            } else {
                masterTeamObstructionBoard[currentX][currentY] = -1;
            }
        }
    }
}

// ---------- updateTerritory ----------

/**
 * @brief Adds or removes an obstruction's territory influence contribution to playerGrids and teamGrids.
 *        For "add": computes Euclidean (ranged) or Dijkstra (non-ranged) influence, stores the
 *        influence board on the instance for later removal, and registers the instance.
 *        For "remove": retrieves the stored influence board and subtracts it.
 * @return The (minX, maxX, minY, maxY) bounds of the affected region.
 */
std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> 
TerritoryAnalyser::updateTerritory(PlayerObject obstructionInstance, size_t player, std::string mod) {
    // adds territory influence around a rectangular obstruction footprint
    // footprint spans [centerX, centerX+bw) x [centerY, centerY+bh)
    // influence radius r extends outward from the footprint edges
    
    size_t centerX=obstructionInstance.position.first, centerY=obstructionInstance.position.second;
    std::string obstruction=obstructionInstance.obstruction; 
    ObstructionInfo info=obstructionInstance.info;
    size_t x, y; 
    double dist;
    
    if (mod != "add" && mod != "remove") {
        std::cerr << "Error: Invalid modification type specified. Use 'add' or 'remove'." << std::endl;
        exit(1);
    }

    Config config = AppConfig::get();
    size_t r = info.influenceRadius, bh = info.height, bw = info.width, softEdge = info.influenceSoftExpansion;
    const bool isRangedObstruction = std::find(rangedObstructions.begin(), rangedObstructions.end(), obstruction) != rangedObstructions.end();
    const bool forceRadialInfluence = config.forceRadialInfluenceForAllObstructions;
    
    // Rectangle bounds (inclusive max edge for distance calc)
    double rectMinX = static_cast<double>(centerX);
    double rectMaxX = static_cast<double>(centerX + bw - 1);
    double rectMinY = static_cast<double>(centerY);
    double rectMaxY = static_cast<double>(centerY + bh - 1);
    
    // Determine the bounding box (rectangle + radius + softEdge) to avoid checking the entire grid
        size_t minX = std::max(0, static_cast<int>(centerX) - static_cast<int>(r) - static_cast<int>(softEdge)),
            maxX = std::min(static_cast<int>(size), static_cast<int>(centerX + bw + r + softEdge)),
            minY = std::max(0, static_cast<int>(centerY) - static_cast<int>(r) - static_cast<int>(softEdge)),
            maxY = std::min(static_cast<int>(size), static_cast<int>(centerY + bh + r + softEdge));

    const size_t influenceMinX = minX;
    const size_t influenceMinY = minY;


    if(mod == "remove") {
        const ObstructionInstanceKey key{
            obstructionInstance.player,
            obstructionInstance.team,
            obstructionInstance.obstruction,
            obstructionInstance.position
        };

        const auto keyedIdsIt = obstructionInstanceIdsByKey.find(key);
        if (keyedIdsIt == obstructionInstanceIdsByKey.end() || keyedIdsIt->second.empty()) {
            std::cerr << "Warning: No obstruction instance found at (" << centerX << ", " << centerY << ") for removal." << std::endl;
            return std::make_tuple(0, 0, 0, 0);
        }

        size_t removalId = keyedIdsIt->second.back();
        if (obstructionInstance.instanceId != 0) {
            const auto requestedIt = std::find(keyedIdsIt->second.begin(), keyedIdsIt->second.end(), obstructionInstance.instanceId);
            if (requestedIt != keyedIdsIt->second.end()) {
                removalId = obstructionInstance.instanceId;
            }
        }

        const auto storedIt = obstructionInstancesById.find(removalId);
        if (storedIt == obstructionInstancesById.end()) {
            std::cerr << "Warning: No stored obstruction instance id " << removalId << " found for removal." << std::endl;
            return std::make_tuple(0, 0, 0, 0);
        }

        const PlayerObject removal = storedIt->second;
        const size_t removalMinX = removal.influenceMinX;
        const size_t removalMinY = removal.influenceMinY;
        const size_t removalMaxX = removalMinX + removal.influenceBoard.size();
        const size_t removalMaxY = removal.influenceBoard.empty() ? removalMinY : removalMinY + removal.influenceBoard.front().size();

        if (removal.player == 0) {
            for (x = centerX; x < centerX + bw && x < size; ++x) {
                for (y = centerY; y < centerY + bh && y < size; ++y) {
                    gaiaBoard[x][y] -= sgn(info.influenceWeight);
                }
            }
        } else {
            for (size_t i = removalMinX; i < removalMaxX && i < size; ++i) {
                for (size_t j = removalMinY; j < removalMaxY && j < size; ++j) {
                    const double influenceValue = removal.influenceBoard[i - removalMinX][j - removalMinY];
                    playerGrids.at(removal.player).setValue(i, j, playerGrids.at(removal.player).getValue(i, j) - influenceValue);
                    teamGrids.at(removal.team).setValue(i, j, teamGrids.at(removal.team).getValue(i, j) - influenceValue);
                }
            }
        }

        updateCellAttributionForObstruction(removal, "remove");
        obstructionInstancesById.erase(removalId);

        auto mutableIdsIt = obstructionInstanceIdsByKey.find(key);
        if (mutableIdsIt != obstructionInstanceIdsByKey.end()) {
            auto& ids = mutableIdsIt->second;
            ids.erase(std::remove(ids.begin(), ids.end(), removalId), ids.end());
            if (ids.empty()) {
                obstructionInstanceIdsByKey.erase(mutableIdsIt);
            }
        }

        return std::make_tuple(removalMinX, removalMaxX, removalMinY, removalMaxY);
    }
    // else, it is add
    obstructionInstance.influenceBoard = std::vector(maxX - minX, std::vector<double>(maxY - minY, 0));
    obstructionInstance.influenceMinX = influenceMinX;
    obstructionInstance.influenceMinY = influenceMinY;
    obstructionInstance.player = player;
    obstructionInstance.team = (player == 0) ? 0 : static_cast<size_t>(teamAssignments.at(static_cast<int>(player)));
    obstructionInstance.instanceId = nextObstructionInstanceId++;

    // if player is Gaia
    if (player == 0) {
        for (x = centerX; x < centerX + bw && x < size; ++x) {
            for (y = centerY; y < centerY + bh && y < size; ++y) {
                gaiaBoard[x][y] += sgn(info.influenceWeight);
            }
        }
    } else {
        const auto& config = AppConfig::get();
        const bool isNonWalkableTerrainObstruction = std::find(config.nonWalkableTerrainObstructions.begin(), config.nonWalkableTerrainObstructions.end(), obstruction) != config.nonWalkableTerrainObstructions.end();

        // Ranged obstructions: Euclidean distance, bounding box, obstructions ignored.
        for (x = minX; x < maxX; ++x) {
            double dx = std::max(0.0, std::max(rectMinX - static_cast<double>(x), static_cast<double>(x) - rectMaxX));
            for (y = minY; y < maxY; ++y) {
                
                double dy = std::max(0.0, std::max(rectMinY - static_cast<double>(y), static_cast<double>(y) - rectMaxY));
                dist = std::sqrt(dx * dx + dy * dy);
                double value = (dist > r) ? std::max(0.0, (r + softEdge + 1.0 - dist) / (softEdge + 1) * (static_cast<double>(threshold) / info.softEdgeDivFactor)) : static_cast<double>(info.influenceWeight);
                // AK Why not just do the cellObstructionAttribution here? Does not that save having to do a double for loop?

                if (isRangedObstruction || forceRadialInfluence) {
                    playerGrids.at(player).setValue(x, y, playerGrids.at(player).getValue(x, y) + value);
                    teamGrids.at(teamAssignments.at(player)).setValue(x, y, teamGrids.at(teamAssignments.at(player)).getValue(x, y) + value);
                    // save local effect to object so that removal is easy.
                    obstructionInstance.influenceBoard[x - minX][y - minY] = value;
                }
            }
        }
        if (!isRangedObstruction && !forceRadialInfluence) {
            // Non-ranged obstructions: weighted 8-neighbour Dijkstra from the footprint.
            // Diagonal moves are allowed, but corner cutting through blocked orthogonals is disallowed.
            const double infDist = std::numeric_limits<double>::infinity();
            const double maxPositiveDistance = static_cast<double>(r + softEdge) + 1.0;
            const double orthogonalCost = 1.0;
            const double diagonalCost = std::sqrt(2.0);
            std::vector<std::vector<double>> pathDistance(size, std::vector<double>(size, infDist));
            std::vector<std::vector<bool>> finalized(size, std::vector<bool>(size, false));

            using Node = std::tuple<double, size_t, size_t>;
            std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
            size_t actualMinX = centerX, actualMaxX = centerX + bw - 1;
            size_t actualMinY = centerY, actualMaxY = centerY + bh - 1;

            const auto addInfluenceAtDistance = [&](size_t x, size_t y, double d) {
                const double value = (d > static_cast<int>(r))
                    ? std::max(0.0, (static_cast<double>(r) + static_cast<double>(softEdge) + 1.0 - static_cast<double>(d))
                                    / (static_cast<double>(softEdge) + 1.0) * (static_cast<double>(threshold) / info.softEdgeDivFactor))
                    : static_cast<double>(info.influenceWeight);

                if (value <= 0.0) {
                    return;
                }

                // Add val *= mod == add ? 1 -1

                playerGrids.at(player).setValue(x, y, playerGrids.at(player).getValue(x, y) + value);
                teamGrids.at(teamAssignments.at(player)).setValue(x, y, teamGrids.at(teamAssignments.at(player)).getValue(x, y) + value);
                // save local effect to object so that removal is easy.
                obstructionInstance.influenceBoard[x - influenceMinX][y - influenceMinY] = value;

                actualMinX = std::min(actualMinX, x);
                actualMaxX = std::max(actualMaxX, x);
                actualMinY = std::min(actualMinY, y);
                actualMaxY = std::max(actualMaxY, y);
            };

            const std::array<int, 8> yOffsets = {-1, -1, -1, 0, 0, 1, 1, 1};
            const std::array<int, 8> xOffsets = {-1, 0, 1, -1, 1, -1, 0, 1};

            const auto inBounds = [this](int x, int y) {
                return y >= 0 && x >= 0 && y < static_cast<int>(size) && x < static_cast<int>(size);
            };

            const auto isInFootprint = [centerX, centerY, bh, bw](size_t x, size_t y) {
                return x >= centerX && x < centerX + bw && y >= centerY && y < centerY + bh;
            };
            
            const auto isInFootprintPlusRadius = [centerX, centerY, bh, bw, r](size_t x, size_t y) {
                return x >= centerX - r && x < centerX + bw + r && y >= centerY - r && y < centerY + bh + r;
            };

            const auto isTraversable = [&](size_t x, size_t y) {
                if (isInFootprintPlusRadius(x, y)) {
                    return true;
                }
                if (masterPlayerObstructionBoard[x][y] != -1) {
                    return false;
                }
                if (!WalkableTerrainBoard[x][y] && !isNonWalkableTerrainObstruction) {
                    return false;
                }
                return true;
            };

            // Seed the footprint with zero path distance.
            for (x = centerX; x < centerX + bw && x < size; ++x) {
                for (y = centerY; y < centerY + bh && y < size; ++y) {
                    if (pathDistance[x][y] > 0.0) {
                        pathDistance[x][y] = 0.0;
                        frontier.push({0.0, x, y});
                    }
                }
            }

            while (!frontier.empty()) {
                const auto [currentDist, x, y] = frontier.top();
                frontier.pop();

                if (finalized[x][y]) {
                    continue;
                }
                if (currentDist >= maxPositiveDistance) {
                    continue;
                }
                finalized[x][y] = true;

                addInfluenceAtDistance(x, y, currentDist);

                for (size_t direction = 0; direction < 8; ++direction) {
                    const int nextX = static_cast<int>(x) + xOffsets[direction];
                    const int nextY = static_cast<int>(y) + yOffsets[direction];

                    if (!inBounds(nextX, nextY)) {
                        continue;
                    }

                    const size_t nx = static_cast<size_t>(nextX);
                    const size_t ny = static_cast<size_t>(nextY);

                    if (!isTraversable(nx, ny)) {
                        continue;
                    }

                    // For diagonal moves, ensure that we are not cutting through a corner where one or both of the orthogonal neighbors are blocked.
                    const bool isDiagonal = (yOffsets[direction] != 0 && xOffsets[direction] != 0);
                    if (isDiagonal) {
                        const int sideYA = static_cast<int>(y) + yOffsets[direction];
                        const int sideXA = static_cast<int>(x);
                        const int sideYB = static_cast<int>(y);
                        const int sideXB = static_cast<int>(x) + xOffsets[direction];

                        if (!inBounds(sideXA, sideYA) || !inBounds(sideXB, sideYB)) {
                            continue;
                        }

                        const size_t sya = static_cast<size_t>(sideYA);
                        const size_t sxa = static_cast<size_t>(sideXA);
                        const size_t syb = static_cast<size_t>(sideYB);
                        const size_t sxb = static_cast<size_t>(sideXB);

                        if (!isTraversable(sxa, sya) || !isTraversable(sxb, syb)) {
                            continue;
                        }
                    }

                    const double stepCost = isDiagonal ? diagonalCost : orthogonalCost;
                    const double candidateDist = currentDist + stepCost;

                    if (candidateDist >= maxPositiveDistance) {
                        continue;
                    }

                    if (candidateDist + 1e-9 < pathDistance[nx][ny]) {
                        pathDistance[nx][ny] = candidateDist;
                        frontier.push({candidateDist, nx, ny});
                    }
                }
            }

            minX = actualMinX;
            maxX = actualMaxX + 1;
            minY = actualMinY;
            maxY = actualMaxY + 1;
        }
    }

    updateCellAttributionForObstruction(obstructionInstance, "add");
    obstructionInstancesById[obstructionInstance.instanceId] = obstructionInstance;
    const ObstructionInstanceKey addKey{
        obstructionInstance.player,
        obstructionInstance.team,
        obstructionInstance.obstruction,
        obstructionInstance.position
    };
    obstructionInstanceIdsByKey[addKey].push_back(obstructionInstance.instanceId);
    return  std::make_tuple(minX, maxX, minY, maxY);
}


// ---------- updateRender ----------

/**
 * @brief Recomputes the edge boards from the current master boards and fill boards for the
 *        given render type ("player" or "team"), then runs colourPass() to produce the
 *        updated final territory map for that type.
 */
void TerritoryAnalyser::updateRender(const std::string& renderType) {
    
    // analyse edges for the requested render type only
    if (renderType == "player") {
        masterPlayerBoardEdges = findEdges(getMasterBoard("player"), getMasterBoardFill("player"), "fourBox");
    } else if (renderType == "team") {
        masterTeamBoardEdges = findEdges(getMasterBoard("team"), getMasterBoardFill("team"), "fourBox");
    }

    // perform colour pass for the requested render type
    colourPass(renderType);
}

// ---------- updateContestedFlash ----------

/**
 * @brief Applies a triangular-wave alpha pulse to contested cells in the final territory maps.
 *        The pulse period is 2.5 s and is anchored to the seed timestamp set at construction.
 */
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

    for (size_t x = 0; x < size; ++x) {
        for (size_t y = 0; y < size; ++y) {
            const bool playerIsContested = !playerContestedMapTruth.empty()
                && playerContestedMapTruth[x][y];
            if (playerIsContested) {
                finalPlayerTerritoryMap.at<cv::Vec4b>(x, y) = cv::Vec4b(
                    config.contestedTerritoryColour[0],
                    config.contestedTerritoryColour[1],
                    config.contestedTerritoryColour[2],
                    static_cast<unsigned char>(alphaVal));
            } 

            const bool teamIsContested = !teamContestedMapTruth.empty()
                && teamContestedMapTruth[x][y];
            if (teamIsContested) {
                finalTeamTerritoryMap.at<cv::Vec4b>(x, y) = cv::Vec4b(
                    config.contestedTerritoryColour[0],
                    config.contestedTerritoryColour[1],
                    config.contestedTerritoryColour[2],
                    static_cast<unsigned char>(alphaVal));
            }
        }
    }


}

// ---------- resolveContestedTerritoryGrowth ----------

/**
 * @brief Resolves multi-owner bitmask cells in masterPlayerBoard and masterTeamBoard.
 *        Each contested cell is awarded to the owner with the highest raw truth weight after a
 *        15%-per-step Manhattan falloff from their nearest unique cell.  Ties are broken by
 *        distance first, then by owner id.  An optional bounds tuple restricts the scan region.
 */
void TerritoryAnalyser::resolveContestedTerritoryGrowth(std::tuple<size_t, size_t, size_t, size_t> bounds) {
    
    // This function identifies contested territories based on the growth method and updates the master boards accordingly.
    
    // we are going to go through all the contested cells and find each players nearest uncontested cell.
    // using the distances in an equation, we will bias the weight of a contested cell to be 10% less it's value for every cell distance away from an uncontested cell of that player.
    // Finally, the highest adjusted weight will be the owner of the contested cell, and if there is a tie for highest, the closest gets it, otherwise the lowest player ID gets it (to ensure deterministic results).
    
    size_t minX = 0;
    size_t maxX = size;
    size_t minY = 0;
    size_t maxY = size;

    if (bounds != std::make_tuple(0, 0, 0, 0)) {
        std::tie(minX, maxX, minY, maxY) = bounds;
        minX = std::min(minX, size);
        maxX = std::min(maxX, size);
        minY = std::min(minY, size);
        maxY = std::min(maxY, size);

        if (minY >= maxY || minX >= maxX) {
            return;
        }
    }

    const auto resolveBoard = [this](std::vector<std::vector<double>>& masterBoard,
                                     const std::vector<Grid>& grids,
                                     size_t ownerCount,
                                     size_t minX,
                                     size_t maxX,
                                     size_t minY,
                                     size_t maxY) {
        if (masterBoard.empty() || ownerCount == 0) {
            return;
        }

        const size_t boardSize = masterBoard.size();
        const int unreachable = std::numeric_limits<int>::max() / 4;

        std::vector<std::vector<size_t>> ownership(boardSize, std::vector<size_t>(boardSize, 0));
        for (size_t x = 0; x < boardSize; ++x) {
            for (size_t y = 0; y < boardSize; ++y) {
                ownership[x][y] = static_cast<size_t>(masterBoard[x][y]);
            }
        }

        std::vector<std::vector<std::vector<int>>> distances(
            ownerCount + 1,
            std::vector<std::vector<int>>(boardSize, std::vector<int>(boardSize, unreachable)));
        std::vector<std::vector<std::vector<double>>> ownerTruthBoards(ownerCount + 1);
        std::vector<bool> relevantOwners(ownerCount + 1, false);

        bool hasContestedCellsInBounds = false;
        for (size_t x = minX; x < maxX; ++x) {
            for (size_t y = minY; y < maxY; ++y) {
                const size_t cellMask = ownership[x][y];
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

        const std::array<int, 4> yOffsets = {-1, 1, 0, 0};
        const std::array<int, 4> xOffsets = {0, 0, -1, 1};

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

            for (size_t x = 0; x < boardSize; ++x) {
                for (size_t y = 0; y < boardSize; ++y) {
                    if (ownership[x][y] == ownerMask) {
                        distances[ownerId][x][y] = 0;
                        frontier.push({x, y});
                        hasUniqueSeed = true;
                    }
                }
            }

            // If an owner has no unique cells, fall back to all of its occupied cells so
            // fully-overlapped regions can still be resolved by raw influence weights.
            if (!hasUniqueSeed) {
                for (size_t x = 0; x < boardSize; ++x) {
                    for (size_t y = 0; y < boardSize; ++y) {
                        if ((ownership[x][y] & ownerMask) != 0U) {
                            distances[ownerId][x][y] = 0;
                            frontier.push({x, y});
                        }
                    }
                }
            }

            while (!frontier.empty()) {
                const auto [x, y] = frontier.front();
                frontier.pop();

                for (size_t direction = 0; direction < yOffsets.size(); ++direction) {
                    const int nextY = static_cast<int>(y) + yOffsets[direction];
                    const int nextX = static_cast<int>(x) + xOffsets[direction];

                    if (nextY < 0 || nextX < 0 || nextY >= static_cast<int>(boardSize) || nextX >= static_cast<int>(boardSize)) {
                        continue;
                    }

                    if (distances[ownerId][static_cast<size_t>(nextX)][static_cast<size_t>(nextY)] != unreachable) {
                        continue;
                    }

                    distances[ownerId][static_cast<size_t>(nextX)][static_cast<size_t>(nextY)] = distances[ownerId][x][y] + 1;
                    frontier.push({static_cast<size_t>(nextX), static_cast<size_t>(nextY)});
                }
            }
        }

        for (size_t x = minX; x < maxX; ++x) {
            for (size_t y = minY; y < maxY; ++y) {
                const size_t cellMask = ownership[x][y];
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

                    const double rawWeight = ownerTruthBoards[ownerId][x][y];
                    const int distance = distances[ownerId][x][y];
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
                    masterBoard[x][y] = static_cast<double>(size_t{1} << (bestOwnerId - 1));
                }
            }
        }
    };

    resolveBoard(masterPlayerBoard, playerGrids, numPlayers, minX, maxX, minY, maxY);
    resolveBoard(masterTeamBoard, teamGrids, numTeams, minX, maxX, minY, maxY);
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
    ::printBoard(gaiaBoard);
    std::cout << std::endl;
}
    

// ---------- mapMergedTerritories ----------

/**
 * @brief Rebuilds masterPlayerBoard and masterTeamBoard from the per-player/team boolean grids.
 *        Each player/team is merged with addPlTerrToMaster using a power-of-two multiplier so
 *        that multi-owner cells encode a bitmask of all contributing owners.
 */
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
    return gaiaBoard;
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

// ---------- colourPass ----------

/**
 * @brief Renders all territory layers (gaia, edges, raw obstruction territory, fill territory) for
 *        the given render type ("player" or "team") into the corresponding final territory map.
 *        Also produces the contested map and its truth mask for that type.  Multiple owners in
 *        a cell produce a contested rendering depending on contestedTerritoryMethod.
 */
void TerritoryAnalyser::colourPass(const std::string& renderType) {

    const auto& config = AppConfig::get();

    const bool useTeamColours = (renderType == "team");

    // Build only the colour map needed for this render type
    std::map<int, cv::Vec4b> colours;
    const auto& colourSource = useTeamColours ? config.teamColours : config.playerColours;
    for (const auto& [id, rgba] : colourSource) {
        colours[id] = cv::Vec4b(
            static_cast<uchar>(rgba[0]),
            static_cast<uchar>(rgba[1]),
            static_cast<uchar>(rgba[2]),
            static_cast<uchar>(rgba[3]));
    }

    auto getColour = [&](int id) -> cv::Vec4b {
        auto it = colours.find(id);
        if (it != colours.end()) {
            return it->second;
        }
        throw std::runtime_error("Missing colour in settings for id: " + std::to_string(id));
    };

    cv::Mat mat(size, size, CV_8UC4);
    cv::Mat contestedMat = cv::Mat::zeros(static_cast<int>(size), static_cast<int>(size), CV_8UC4);
    std::vector<std::vector<bool>> contestedTruthMat(size, std::vector<bool>(size, false));

    // Select boards directly based on render type
    const auto& masterBoard = useTeamColours ? masterTeamBoard : masterPlayerBoard;
    auto edgeBoard = useTeamColours ? masterTeamBoardEdges : masterPlayerBoardEdges;
    auto fillBoard = useTeamColours ? masterTeamBoardFill : masterPlayerBoardFill;

    // create a mutable copy of board data
    std::vector<std::vector<double>> board(size, std::vector<double>(size, 0));
    for (size_t x = 0; x < size; ++x) {
        for (size_t y = 0; y < size; ++y) {
            board[x][y] = masterBoard[x][y];
        }
    }

    for (size_t x = 0; x < board.size(); ++x) {
        for (size_t y = 0; y < board.size(); ++y) {

            // initialise values
            bool existingVal = false, isEdge = false, isObstructionTerritory = false;

// ************** 1. GAIA: If the cell is unoccupied or occupied by gaia, skip **************** //

            if (gaiaBoard[x][y] == 1 || (fillBoard[x][y] == 0 && edgeBoard[x][y] == 0 && board[x][y] == 0)) {
                mat.at<cv::Vec4b>(x, y) = getColour(0);
                continue;
            }

// ************** 2. EDGES **************** // 

            // loop through all the players in reverse order
            for (size_t k=8; k>0; k--) {

                if (static_cast<int>(edgeBoard[x][y])/static_cast<int>(std::pow(2, k-1)) >= 1) { // check if the cell is an edge for player k

                    // remove the highest power of 2 to find out if there are multiple players in the cell
                    edgeBoard[x][y] = static_cast<double>(static_cast<int>(edgeBoard[x][y]) % static_cast<int>(std::pow(2, k-1)));

                    // if the cell is currently unoccupied, assign it to the player
                    if (!existingVal) { 
                        mat.at<cv::Vec4b>(x, y) = getColour(static_cast<int>(k));
                        mat.at<cv::Vec4b>(x, y)[3] = config.edgeOpacity;
                        existingVal = true;
                        isEdge = true;

                    // if the cell is already occupied, mark it as contested.
                    // For now, we make contested areas grey, the other idea was to have it be perpendicular 
                    // lines of the two/multiple players, but that is more complex to implement and may not 
                    // be worth the effort for the visualisation.
                    } else { 
                        if(config.contestedTerritoryMethod == "flash") {
                            mat.at<cv::Vec4b>(x, y) = getColour(0);
                            mat.at<cv::Vec4b>(x, y)[3] = 0;
                            // contested mat will just be white and flash between white and transparent by toggling the alpha value
                            contestedMat.at<cv::Vec4b>(x, y) = cv::Vec4b(255, 255, 255, 255);
                            contestedTruthMat[x][y] = true;
                            // no need to check further players since it's already contested 
                            break; 
                        } else if (config.contestedTerritoryMethod == "perpendicularLines") {
                            // Implement perpendicular lines method here if desired
                            // This would likely involve drawing lines on the cell in the colours of the players involved
                            // and may require a more complex data structure to track which players are contesting the cell
                        } else if (config.contestedTerritoryMethod == "growth") {
                            // Implemented in updateObstruction
                            break;
                        } else if (config.contestedTerritoryMethod == "staticColour") {
                            // Implement static colour method, where the cell is coloured a specific colour for contested regardless of players involved
                        
                            mat.at<cv::Vec4b>(x, y) = cv::Vec4b(config.contestedTerritoryColour[0], config.contestedTerritoryColour[1], config.contestedTerritoryColour[2], config.contestedTerritoryColour[3]);
                            mat.at<cv::Vec4b>(x, y)[3] = config.edgeOpacity;
                            // no need to check further players since it's already contested 
                            break; 
                        } else {
                            std::cerr << "Error: Invalid contested territory method specified (" << config.contestedTerritoryMethod << "). Defaulting to flash method." << std::endl;
                            mat.at<cv::Vec4b>(x, y) = getColour(4);
                            mat.at<cv::Vec4b>(x, y)[3] = config.edgeOpacity;
                        }
                    }
                }
            }
            
// loop through all the players in reverse order - must be separate from edges or there are 3+ player situations 
// where the cell might be marked as contested territory before it gets to the edge player
            if(!isEdge) {
                for (size_t k=8; k>0; k--) {

// ************** 3. TERRITORY FROM BUILDINGS **************** // 

                    if (static_cast<int>(board[x][y])/static_cast<int>(std::pow(2, k-1)) >= 1) {

                        // remove the highest power of 2 to find out if there are multiple players in the cell
                        board[x][y] = static_cast<double>(static_cast<int>(board[x][y]) % static_cast<int>(std::pow(2, k-1)));
                        
                        // if the cell is currently unoccupied, assign it to the player
                        if (!existingVal) { 
                            mat.at<cv::Vec4b>(x, y) = getColour(static_cast<int>(k));
                            mat.at<cv::Vec4b>(x, y)[3] = config.territoryOpacity;
                            existingVal = true;
                            isObstructionTerritory = true;

                        // if the cell is already occupied, mark it as contested
                        } else { 
                            mat.at<cv::Vec4b>(x, y) = getColour(4);
                            mat.at<cv::Vec4b>(x, y)[3] = config.territoryOpacity;
                            // no need to check further players since it's already contested
                            break; 
                        }
                    }
                }
            }

// ************** 4. TERRITORY FROM FILL **************** // 

            if(!isEdge && !isObstructionTerritory) { 

                if (fillBoard[x][y] == 0) {
                    continue;
                } else {
                    int k = fillBoard[x][y];
                    mat.at<cv::Vec4b>(x, y) = getColour(k);
                    mat.at<cv::Vec4b>(x, y)[3] = static_cast<int>(config.territoryOpacity / 2);
                    existingVal = true;
                }
            }
        }
    }

    cv::Mat rotatedMat;
    cv::Mat rotatedContestedMat;
    cv::rotate(mat, rotatedMat, cv::ROTATE_90_COUNTERCLOCKWISE);
    cv::rotate(contestedMat, rotatedContestedMat, cv::ROTATE_90_COUNTERCLOCKWISE);
    const auto rotatedContestedTruthMat = rotateBoolBoard90CounterClockwise(contestedTruthMat);

    if (!useTeamColours) {
        finalPlayerTerritoryMap = rotatedMat;
        playerContestedMap = rotatedContestedMat;
        playerContestedMapTruth = rotatedContestedTruthMat;
    } else {
        finalTeamTerritoryMap = rotatedMat;
        teamContestedMap = rotatedContestedMat;
        teamContestedMapTruth = rotatedContestedTruthMat;
    }
}



// ---------- addPlTerrToMaster ----------

/**
 * @brief Merges a single player's boolean territory board into masterBoard.
 *        Obstruction cells are written directly from obstructionBoard; unobstructed cells add
 *        playerBoard * multFactor to the running bitmask total.
 */
std::vector<std::vector<double>> 
TerritoryAnalyser::addPlTerrToMaster(std::vector<std::vector<double>> masterBoard, 
    std::vector<std::vector<bool>> playerBoard, const std::vector<std::vector<int>>& obstructionBoard, int multFactor) {

    if (masterBoard.empty()) {
        return masterBoard;
    }

    for (size_t x = 0; x < masterBoard.size(); ++x) {
        for (size_t y = 0; y < masterBoard[x].size(); ++y) {
            if (obstructionBoard[x][y] != -1) {
                masterBoard[x][y] = static_cast<double>(obstructionBoard[x][y]);
            } else {
                masterBoard[x][y] = masterBoard[x][y] + static_cast<double>(playerBoard[x][y] * multFactor);
            }
        }
    }
    return masterBoard;
}




// ---------- loadObstructionsDict ----------

/**
 * @brief Loads obstruction definitions and ranged-obstruction names from a JSON file (also tries the
 *        "../" prefix as a fallback).  Populates obstructionsDict and rangedObstructions; military
 *        obstructions are collected into militaryObstructions.  Exits on file or schema errors.
 */
void TerritoryAnalyser::loadObstructionsDict(const std::string& givenDictPath) {

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
        std::cerr << "Error: Could not open obstructionsDict.json. Tried: obstructionsDict.json, ../obstructionsDict.json" << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    }

    try {
        json data = json::parse(file);
        
        // Extract the obstructionsDict array from JSON
        if (!data.contains("obstructionsDict") || !data["obstructionsDict"].is_array()) {
            throw std::runtime_error("obstructionsDict must be an array in JSON file");
        }
        if (!data.contains("rangedObstructions") || !data["rangedObstructions"].is_array()) {
            std::cerr << "Warning: rangedObstructions array not found in obstructionsDict.json. Ranged obstructions will not be identified." << std::endl;
        }


        // Ranged obstructions 
        rangedObstructions = data["rangedObstructions"].get<std::vector<std::string>>();
        
        // Parse each obstruction entry in the obstructionsDict array
        for (const auto& obstruction : data["obstructionsDict"]) {
            if (!obstruction.is_object()) {
                throw std::runtime_error("Each obstruction entry must be an object with \"name\", \"params\", and \"label\" fields");
            }
            
            std::string name = obstruction.at("name").get<std::string>();
            std::vector<double> properties = obstruction.at("params").get<std::vector<double>>();
            std::string label = obstruction.at("label").get<std::string>();
            
            if (properties.size() != 6) {
                throw std::runtime_error("Each obstruction must have 6 properties: [radius, weight, softEdge, softEdgeDivFactor, width, height]");
            }
            
            obstructionsDict[name] = ObstructionInfo{
                static_cast<size_t>(properties[0]),  // influenceRadius
                static_cast<size_t>(properties[1]),  // influenceWeight
                static_cast<size_t>(properties[2]),  // influenceSoftExpansion
                properties[3],                       // softEdgeDivFactor
                static_cast<size_t>(properties[4]),  // width
                static_cast<size_t>(properties[5]),  // height
                label                                // label ("Combative" or "Other")
            };
            if (label == "Combative") {
                combativeObstructions.push_back(name);
            }
        }
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse obstructionsDict file '" << dictPath << "': " << e.what() << std::endl;
        std::cerr << "Current path: " << std::filesystem::current_path() << std::endl;
        std::exit(1);
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid obstructionsDict format in '" << dictPath << "': " << e.what() << std::endl;
        std::exit(1);
    }
    
    return;
}

// ---------- playerDefeated ----------

/**
 * @brief Marks player as defeated, zeroes their territory grid, and adjusts team-grid contributions:
 *        ranged obstructions are re-added to the fresh player grid; non-ranged obstructions are removed
 *        from the team grid.  Refreshes bool grids for the affected player and their team.
 */
void TerritoryAnalyser::playerDefeated(size_t player) {
    if (player == 0 || player >= playerGrids.size()) return;

    const Config& config = AppConfig::get();
    isPlayerDefeated[player] = true;

    // Re-initialise this player's territory grid (all influence zeroed).
    playerGrids.at(player) = Grid(static_cast<int>(size));

    // Iterate all stored obstructions for this player and:
    //   - ranged: re-apply stored influence to player grid; no change needed for team grid
    //             (ranged contribution was never cleared from the team grid).
    //   - non-ranged: remove stored influence from the team grid.
    size_t minX = 0, maxX = size, minY = 0, maxY = size;

    for (const auto& [id, obj] : obstructionInstancesById) {
        if (obj.player != player) continue;

        const bool isRanged = std::find(rangedObstructions.begin(), rangedObstructions.end(), obj.obstruction) != rangedObstructions.end();
        const size_t team = obj.team;

        for (size_t i = 0; i < obj.influenceBoard.size(); ++i) {
            for (size_t j = 0; j < obj.influenceBoard[i].size(); ++j) {
                const double val = obj.influenceBoard[i][j];
                if (val == 0.0) continue;
                const size_t x = obj.influenceMinX + i;
                const size_t y = obj.influenceMinY + j;
                if (x >= size || y >= size) continue;

                minY = std::min(minY, y);
                maxY = std::max(maxY, y + 1);
                minX = std::min(minX, x);
                maxX = std::max(maxX, x + 1);

                if (isRanged) {
                    // Re-add ranged influence to the fresh player grid.
                    playerGrids.at(player).setValue(x, y,
                        playerGrids.at(player).getValue(x, y) + val);
                } else {
                    // Remove non-ranged contribution from the shared team grid.
                    if (team > 0 && team < teamGrids.size()) {
                        teamGrids.at(team).setValue(x, y,
                            teamGrids.at(team).getValue(x, y) - val);
                    }
                }
            }
        }
    }

    // Keep bool territory in sync with dataTruth for render and merge passes.
    const auto bounds = std::make_tuple(minX, maxX, minY, maxY);
    playerGrids.at(player).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
    teamGrids.at(teamAssignments.at(player)).terrainBoolPass(config.rawTerritoryOwnershipThreshold, bounds);
}