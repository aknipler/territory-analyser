#pragma once

#include "grid.h"
#include "aux.h"
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
};

struct PlayerObject {
    std::size_t instanceId;
    std::size_t player;
    std::size_t team;
    std::string building;
    Position position;
    std::vector<std::vector<double>> influenceBoard;
    std::size_t influenceMinRow;
    std::size_t influenceMinCol;
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

class TerritoryAnalyser {
    private:
        std::vector<Grid> playerGrids, teamGrids; // index 0 unused; ids start at 1
        std::map<int, int> teamAssignments; // map to hold the team assignments of each player
        size_t size, threshold, seed;
        std::vector<std::vector<size_t>> gaia_board; 
        std::vector<std::vector<int>> masterPlayerObstructionBoard, masterTeamObstructionBoard;
        std::vector<std::vector<size_t>> masterPlayerBoardEdges, masterPlayerBoardFill, masterTeamBoardEdges, masterTeamBoardFill;
        std::vector<std::vector<double>> masterPlayerBoard, masterTeamBoard;
        cv::Mat finalPlayerTerritoryMap, finalTeamTerritoryMap, playerContestedMap, teamContestedMap;
        std::vector<std::vector<bool>> playerContestedMapTruth, teamContestedMapTruth;
        std::vector<std::vector<Position>> playerGaps, teamGaps;
        std::vector<std::vector<std::vector<bool>>> playerObstructionBoards, teamObstructionBoards;
        std::vector<std::vector<std::vector<size_t>>> playerObstructionCounts, teamObstructionCounts;
        std::vector<std::vector<size_t>> WalkableTerrainBoard;
        size_t numPlayers, numTeams;
        std::unordered_map<std::string, BuildingInfo> buildingsDict;
        size_t nextBuildingInstanceId = 1;
        std::unordered_map<size_t, PlayerObject> buildingInstancesById;
        std::unordered_map<BuildingInstanceKey, std::vector<size_t>, BuildingInstanceKeyHash, BuildingInstanceKeyEq> buildingInstanceIdsByKey;
        std::vector<std::string> rangedBuildings;
        std::vector<bool> isPlayerDefeated;
        std::unordered_map<Position, std::unordered_set<size_t>, TerritoryPositionHash> cellBuildingAttribution;

        std::unordered_set<size_t> collectAttributedBuildingsInFootprint(size_t x, size_t y, size_t width, size_t height) const;
        void updateCellAttributionForBuilding(const PlayerObject& buildingInstance, std::string mod);
        std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> reapplyAttributedBuildings(const std::unordered_set<size_t>& buildingIds, size_t skipId = 0);
        static std::tuple<size_t, size_t, size_t, size_t> mergeBounds(const std::tuple<size_t, size_t, size_t, size_t>& lhs, const std::tuple<size_t, size_t, size_t, size_t>& rhs);



        
    public:
        // Constructor to initialize the dynamic 2D array
        TerritoryAnalyser(size_t givenSize, size_t numPlayers, size_t numTeams, std::map<int, int> teamAssignments, size_t threshold);
                
        void initWalkableTerrain();

        void updateBuilding(size_t x, size_t y, std::string building, int player, int team, std::string mod);

        void updateObstruction(size_t x, size_t y, size_t bw, size_t bh, int player, int team, std::string mod);
        std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> updateTerritory(PlayerObject buildingInstance, size_t player, std::string mod);
                
        void updateRender();

        void updateContestedFlash();

        void resolveContestedTerritoryGrowth(std::tuple<size_t, size_t, size_t, size_t> bounds = std::make_tuple(0, 0, 0, 0));

        void mapMergedTerritories();
        
        void printPlayerBoards();

        void colour_pass();

        std::vector<std::vector<Position>> getGaps(std::string type = "player") const;
        std::vector<std::vector<size_t>> getGaiaBoard() const;
        std::vector<std::vector<size_t>> getWalkableTerrainBoard() const;
        std::vector<std::vector<int>> getMasterObstructionBoard(std::string type) const;
        std::vector<std::vector<double>> getMasterBoard(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardEdges(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardFill(std::string type = "player") const;
        cv::Mat getFinalTerritoryMap(std::string type = "player") const;
        cv::Mat getContestedMap(std::string type = "player") const;
        std::vector<std::vector<double>> addPlTerrToMaster(std::vector<std::vector<double>> masterBoard, std::vector<std::vector<bool>> playerBoard, const std::vector<std::vector<int>>& obstructionBoard, int mult_factor);

        void loadBuildingsDict(const std::string& dictPath);

        void playerDefeated(size_t player);

};