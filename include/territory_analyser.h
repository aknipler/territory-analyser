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

struct BuildingInfo {
    size_t influenceRadius;
    size_t influenceWeight;
    size_t influenceSoftExpansion;
    size_t width;
    size_t height;
};

class TerritoryAnalyser {
    private:
        std::map<int, Grid> playerGrids, teamGrids; // map to hold the grids of all teams
        std::map<int, int> teamAssignments; // map to hold the team assignments of each player
        size_t size, threshold;
        std::vector<std::vector<size_t>> gaia_board; 
        std::vector<std::vector<size_t>> masterPlayerBoardEdges, masterPlayerBoardFill, masterTeamBoardEdges, masterTeamBoardFill;
        std::vector<std::vector<double>> masterPlayerBoard, masterTeamBoard;
        cv::Mat finalPlayerTerritoryMap, finalTeamTerritoryMap; 
        std::vector<std::vector<Position>> playerGaps, teamGaps;
        std::map<int, std::vector<std::vector<bool>>> playerObstructionBoards, teamObstructionBoards;
        int numPlayers, numTeams;
        std::unordered_map<std::string, BuildingInfo> building_dict;

        std::vector<std::vector<bool>> getCombinedObstructionsBoard(const std::map<int, std::vector<std::vector<bool>>>& obstructionBoards) const;

        
    public:
        // Constructor to initialize the dynamic 2D array
        TerritoryAnalyser(int givenSize, int numPlayers, int numTeams, std::map<int, int> teamAssignments, size_t threshold);
                
        void updateBuilding(size_t x, size_t y, std::string building, int player, int team, std::string mod);

        void updateObstruction(size_t x, size_t y, size_t bw, size_t bh, int player, int team, std::string mod);
        std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> updateTerritory(size_t centerX, size_t centerY, size_t r, size_t player, size_t weighting=5, size_t soft_edge=4, size_t bw=1, size_t bh=1);
                
        void mapMergedTerritories();
        
        void printPlayerBoards();

        void colour_pass();

        std::vector<std::vector<double>> getMasterBoard(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardEdges(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardFill(std::string type = "player") const;
        cv::Mat getFinalTerritoryMap(std::string type = "player") const;

        std::vector<std::vector<double>> addPlTerrToMaster(std::vector<std::vector<double>> masterBoard,std::vector<std::vector<bool>> playerBoard, int mult_factor);


};