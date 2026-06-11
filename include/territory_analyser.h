#pragma once

#include "grid.h"
#include "fill.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <tuple>
#include <string>
#include <list>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <array>
#include <unordered_set>

struct BuildingInfo {
    size_t influenceRadius;
    size_t influenceWeight;
    size_t influenceSoftExpansion;
    double softEdgeDivFactor;
    size_t width;
    size_t height;
    std::string label;
};

struct PlayerObject {
    std::size_t instanceId;
    std::size_t player;
    std::size_t team;
    std::string building;
    Position position;
    std::vector<std::vector<double>> influenceBoard;
    std::size_t influenceMinY;
    std::size_t influenceMinX;
    BuildingInfo info;
};

struct PlayerObjectHash {
    size_t operator()(const PlayerObject& p) const {
        const size_t h1 = std::hash<size_t>{}(p.position.first);
        const size_t h2 = std::hash<size_t>{}(p.position.second);
        const size_t h3 = std::hash<size_t>{}(p.player);
        const size_t h4 = std::hash<size_t>{}(p.team);
        const size_t h5 = std::hash<std::string>{}(p.building);
        size_t seed = h1;
        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h4 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h5 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct PlayerObjectEq {
    bool operator()(const PlayerObject& lhs, const PlayerObject& rhs) const {
        return lhs.position == rhs.position
            && lhs.player == rhs.player
            && lhs.team == rhs.team
            && lhs.building == rhs.building;
    }
};

struct BuildingInstanceKey {
    std::size_t player;
    std::size_t team;
    std::string building;
    Position position;
};

struct BuildingInstanceKeyHash {
    size_t operator()(const BuildingInstanceKey& key) const {
        const size_t h1 = std::hash<size_t>{}(key.position.first);
        const size_t h2 = std::hash<size_t>{}(key.position.second);
        const size_t h3 = std::hash<size_t>{}(key.player);
        const size_t h4 = std::hash<size_t>{}(key.team);
        const size_t h5 = std::hash<std::string>{}(key.building);
        size_t seed = h1;
        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h4 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h5 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct BuildingInstanceKeyEq {
    bool operator()(const BuildingInstanceKey& lhs, const BuildingInstanceKey& rhs) const {
        return lhs.position == rhs.position
            && lhs.player == rhs.player
            && lhs.team == rhs.team
            && lhs.building == rhs.building;
    }
};

struct TerritoryPositionHash {
    size_t operator()(const Position& p) const {
        const size_t h1 = std::hash<size_t>{}(p.first);
        const size_t h2 = std::hash<size_t>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

struct ConnectedObstructions {
    std::unordered_set<PlayerObject, PlayerObjectHash, PlayerObjectEq> buildings;
    std::vector<size_t> bounds = {0, 0, 0, 0}; // minX, maxX, minY, maxY
    bool closed = false;
    bool closedWithMapEdge = false;
    std::unordered_set<size_t> mapEdgeBuildingIds; // instanceIds of buildings whose footprint reaches a map boundary
};

class TerritoryAnalyser {
    private:
        std::vector<Grid> playerGrids, teamGrids; // index 0 unused; ids start at 1
        std::map<int, int> teamAssignments; // map to hold the team assignments of each player
        size_t size, threshold, seed;
        std::vector<std::vector<size_t>> gaiaBoard; 
        std::vector<std::vector<int>> masterPlayerObstructionBoard, masterTeamObstructionBoard;
        std::vector<std::vector<size_t>> masterPlayerBoardEdges, masterPlayerBoardFill, masterTeamBoardEdges, masterTeamBoardFill;
        std::vector<std::vector<double>> masterPlayerBoard, masterTeamBoard;
        cv::Mat finalPlayerTerritoryMap, finalTeamTerritoryMap, playerContestedMap, teamContestedMap;
        std::vector<std::vector<bool>> playerContestedMapTruth, teamContestedMapTruth;
        std::vector<Fill> playerFills, teamFills;
        std::vector<std::vector<int>> playerFillAssignmentBoard, teamFillAssignmentBoard;
        std::vector<std::vector<Position>> playerGaps, teamGaps;
        std::vector<std::vector<std::vector<bool>>> playerObstructionBoards, teamObstructionBoards;
        std::vector<std::vector<std::vector<size_t>>> playerObstructionCounts, teamObstructionCounts;
        std::vector<std::vector<size_t>> WalkableTerrainBoard;
        size_t numPlayers, numTeams;
        std::unordered_map<std::string, BuildingInfo> buildingsDict;
        std::vector<std::string> militaryBuildings;
        size_t nextBuildingInstanceId = 1;
        std::unordered_map<size_t, PlayerObject> buildingInstancesById;
        std::unordered_map<BuildingInstanceKey, std::vector<size_t>, BuildingInstanceKeyHash, BuildingInstanceKeyEq> buildingInstanceIdsByKey;
        std::vector<std::string> rangedBuildings;
        std::vector<bool> isPlayerDefeated;
        std::unordered_map<Position, std::unordered_set<size_t>, TerritoryPositionHash> cellBuildingAttribution;
        // Union-Find for ConnectedObstructions: O(α(N)) per union/find.
        std::unordered_map<size_t, size_t> dsuParent_; // instanceId -> parent instanceId
        std::unordered_map<size_t, size_t> dsuRank_;   // instanceId -> rank (union-by-rank)
        std::unordered_map<size_t, ConnectedObstructions> connectedGroupData_; // root instanceId -> group data

        size_t dsuFind(size_t id);
        void dsuUnion(size_t a, size_t b);
        void addToConnectedObstructions(size_t instanceId, size_t x, size_t y, size_t width, size_t height);
        void removeFromConnectedObstructions(size_t instanceId);
        bool buildingTouchesMapEdge(const PlayerObject& b) const;
        bool checkMapEdgeClosure(const ConnectedObstructions& group) const;

        std::unordered_set<size_t> collectAttributedBuildingsInFootprint(size_t x, size_t y, size_t width, size_t height) const;
        void updateCellAttributionForBuilding(const PlayerObject& buildingInstance, std::string mod);
        std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> reapplyAttributedBuildings(const std::unordered_set<size_t>& buildingIds, size_t skipId = 0);
        static std::tuple<size_t, size_t, size_t, size_t> mergeBounds(const std::tuple<size_t, size_t, size_t, size_t>& lhs, const std::tuple<size_t, size_t, size_t, size_t>& rhs);
        bool hasNearbyBuildings(size_t x, size_t y, size_t width, size_t height) const;

        // updateBuilding() helpers
        bool lookupBuildingAndTeam(const std::string& building, int player, BuildingInfo& infoOut, int& teamOut);
        PlayerObject makeBuildingInstance(size_t x, size_t y, const std::string& building, int player, int team, const BuildingInfo& info) const;
        std::pair<std::unordered_set<size_t>, std::unordered_set<size_t>> collectImpactedOwners(int player, int team, const std::unordered_set<size_t>& impactedBuildings) const;
        std::tuple<size_t, size_t, size_t, size_t> performTerritoryUpdateWithDSUSync(PlayerObject buildingInstance, const std::string& mod);
        void refreshOwnerBoolPasses(const std::unordered_set<size_t>& impactedPlayers, const std::unordered_set<size_t>& impactedTeams, const std::tuple<size_t, size_t, size_t, size_t>& bounds);
        void rebuildMasterBoards();
        void updatePlayerAndTeamFills(size_t x, size_t y, const BuildingInfo& info, bool isFastPath, const std::string& mod, const std::vector<size_t>& dsuGroupBounds);

    public:
        struct BuildingStateResult {
            bool shouldProceed = false;
            BuildingInfo info;
            bool isFastPath = false;
            std::vector<size_t> dsuGroupBounds;
        };

        // Constructor to initialize the dynamic 2D array
        TerritoryAnalyser(size_t givenSize, size_t numPlayers, size_t numTeams, std::map<int, int> teamAssignments, size_t threshold, const std::string& initialStatePath);

        /** @brief Populates WalkableTerrainBoard with a hardcoded circular water-body example (cells
         *         outside the radius are non-walkable).  Replace with real terrain data in production. */
        void initWalkableTerrain();

        /** @brief Performs all territory/obstruction/DSU state updates for a building add/remove,
         *         but does NOT update fills.  Returns a result struct whose shouldProceed flag is
         *         false when the call was a no-op (e.g. unknown building or defeated-player early
         *         return).  Use this instead of updateBuilding when fill recomputation is unwanted
         *         (e.g. during initial state loading before initialiseFill has been called). */
        BuildingStateResult updateBuildingState(size_t x, size_t y, const std::string& building, int player, const std::string& mod);

        /** @brief Main entry point for adding or removing a building: orchestrates territory,
         *         obstruction, DSU connectivity, and fill recomputation for all affected players/teams. */
        void updateBuilding(size_t x, size_t y, std::string building, int player, std::string mod);

        /** @brief Updates per-player and per-team obstruction boards for the given footprint and
         *         rebuilds the master bitmask boards for each affected cell. */
        void updateObstruction(size_t x, size_t y, size_t bw, size_t bh, int player, int team, std::string mod);

        /** @brief Adds or removes a building's territory influence in playerGrids/teamGrids.
         *  @return The (minX, maxX, minY, maxY) bounds of the affected region. */
        std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> updateTerritory(PlayerObject buildingInstance, size_t player, std::string mod);

        /** @brief Recomputes edge boards from master boards and fill boards for the given render type
         *         ("player" or "team"), then runs colourPass(). */
        void updateRender(const std::string& renderType);

        /** @brief Applies a triangular-wave alpha pulse to contested cells; period 2.5 s anchored to
         *         the seed timestamp set at construction. */
        void updateContestedFlash();

        /** @brief Awards each multi-owner (contested) cell to the owner with the best
         *         distance-weighted truth score; optional bounds restricts the scan region. */
        void resolveContestedTerritoryGrowth(std::tuple<size_t, size_t, size_t, size_t> bounds = std::make_tuple(0, 0, 0, 0));

        /** @brief Rebuilds masterPlayerBoard and masterTeamBoard from per-player/team boolean grids
         *         using power-of-two bitmask multipliers to encode multi-owner cells. */
        void mapMergedTerritories();
        
        void printPlayerBoards();

        /** @brief Renders all territory layers into the final territory map for the given render
         *         type ("player" or "team") and its contested map.  Multi-owner cells are blended
         *         per contestedTerritoryMethod. */
        void colourPass(const std::string& renderType);

        std::vector<std::vector<Position>> getGaps(std::string type = "player") const;
        std::vector<std::vector<size_t>> getGaiaBoard() const;
        std::vector<std::vector<size_t>> getWalkableTerrainBoard() const;
        std::vector<std::vector<int>> getMasterObstructionBoard(std::string type) const;
        std::vector<std::vector<double>> getMasterBoard(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardEdges(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardFill(std::string type = "player") const;
        cv::Mat getFinalTerritoryMap(std::string type = "player") const;
        cv::Mat getContestedMap(std::string type = "player") const;
        /** @brief Merges a single player's boolean territory board into masterBoard using
         *         mult_factor as the bitmask weight; obstruction cells bypass the bool grid. */
        std::vector<std::vector<double>> addPlTerrToMaster(std::vector<std::vector<double>> masterBoard, std::vector<std::vector<bool>> playerBoard, const std::vector<std::vector<int>>& obstructionBoard, int mult_factor);

        /** @brief Loads building definitions from a JSON file (tries "../" prefix as fallback).
         *         Populates buildingsDict, rangedBuildings, and militaryBuildings. */
        void loadBuildingsDict(const std::string& dictPath);

        /** @brief Marks player as defeated, zeroes their territory grid, and adjusts the team grid
         *         by removing non-ranged and re-applying ranged building contributions. */
        void playerDefeated(size_t player);

};