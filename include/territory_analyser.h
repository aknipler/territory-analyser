#pragma once

#include "grid.h"
#include "aux.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <tuple>
#include <string>
#include <list>
#include <opencv2/opencv.hpp>
#include <bit>
#include <iostream>
#include <array>


class TerritoryAnalyser {
    private:
        std::map<int, Grid> playerGrids; // map to hold the grids of all players
        std::map<int, Grid> teamGrids; // map to hold the grids of all teams
        std::map<int, int> teamAssignments; // map to hold the team assignments of each player
        size_t size;
        std::vector<std::vector<size_t>> gaia_board; 
        std::vector<std::vector<double>> masterPlayerBoard;
        std::vector<std::vector<size_t>> masterPlayerBoardEdges;
        std::vector<std::vector<size_t>> masterPlayerBoardFill;
        std::vector<std::vector<double>> masterTeamBoard;
        std::vector<std::vector<size_t>> masterTeamBoardEdges;
        std::vector<std::vector<size_t>> masterTeamBoardFill;
        int numPlayers;
        int numTeams;

        std::unordered_map <std::string, std::tuple <std::size_t,std::size_t>> building_dict;
        
    public:
        // Constructor to initialize the dynamic 2D array
        TerritoryAnalyser(int givenSize, int numPlayers, int numTeams, std::map<int, int> teamAssignments);
                
        void updateBuilding(size_t x, size_t y, std::string building, int player, int team, std::string mod);

        std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> updateTerritory(size_t centerX, size_t centerY, size_t r, size_t player, size_t weighting=5, size_t soft_edge=4);
                
        void mapMergedTerritories();
        
        void printPlayerBoards();

        std::tuple<cv::Mat, cv::Mat> colour_pass();

        std::vector<std::vector<double>> getMasterBoard(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardEdges(std::string type = "player") const;
        std::vector<std::vector<size_t>> getMasterBoardFill(std::string type = "player") const;

};