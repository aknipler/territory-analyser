#pragma once
#include "territory_analyser.h"
#include "grid.h"
#include "fill.h"
#include "aux.h"

#include "json.hpp" 
using json = nlohmann::json;

#include <opencv2/opencv.hpp>

#include <array>
#include <map>
#include <string>

struct Config {

    // App Settings
    std::string appName;
    std::string version;
    std::string outputDirectory;

    // Testing Settings
    bool testingMode;
    bool forceRadialInfluenceForAllBuildings;

    // Match Settings
    int numberOfPlayers;
    int numberOfTeams;
    size_t mapSize;
    std::map<int, int> teamAssignments;

    // Analytics Settings
    std::string contestedTerritoryMethod;
    std::vector<std::string> contestedTerritoryMethodOptions;
    std::array<int, 4> contestedTerritoryColour;
    size_t rawTerritoryOwnershipThreshold;
    int CLOSED_SHAPE_GAPS_THRESHOLD;
    double ownershipThreshold;
    double contestedOwnershipThreshold;
    double isWalledMultiplier;
    std::vector<std::string> nonWalkableTerrainBuildings;

    std::vector<std::vector<bool>> nonWalkableTerrainExample;

    // Visual Settings
    std::map<int, std::array<int, 4>> playerColours;
    std::map<int, std::array<int, 4>> teamColours;
    int edgeOpacity;
    int territoryOpacity;
};

bool outputForTests(const TerritoryAnalyser& analyser, const Config& config, const std::string& fileName = "2x3_Test");

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Config, appName, version, testingMode, forceRadialInfluenceForAllBuildings, outputDirectory, numberOfPlayers, numberOfTeams, mapSize, teamAssignments, playerColours, teamColours, contestedTerritoryMethod, contestedTerritoryMethodOptions, contestedTerritoryColour, rawTerritoryOwnershipThreshold, CLOSED_SHAPE_GAPS_THRESHOLD, ownershipThreshold, contestedOwnershipThreshold, isWalledMultiplier, nonWalkableTerrainBuildings, edgeOpacity, territoryOpacity)