#pragma once
#include "territory_analyser.h"
#include "grid.h"
#include "fill.h"
#include "aux.h"

#include "json.hpp" 
using json = nlohmann::json;

#include <cmath>
#include <opencv2/opencv.hpp>

#include <list>
#include <array>
#include <map>

#include <string>

struct Config {

    // App Settings
    std::string appName;
    std::string version;
    bool testingMode;
    std::string outputDirectory;

    // Match Settings
    int numberOfPlayers;
    int numberOfTeams;
    size_t mapSize;
    std::map<int, int> teamAssignments;

    // Analytics Settings
    size_t territoryFromBuildingThreshold;
    int CLOSED_SHAPE_GAPS_THRESHOLD;

    // Visual Settings
    std::map<int, std::array<int, 4>> playerColours;
    std::map<int, std::array<int, 4>> teamColours;
    int edgeOpacity;
    int territoryOpacity;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Config, appName, version, testingMode, outputDirectory, numberOfPlayers, numberOfTeams, mapSize, teamAssignments, playerColours, teamColours, territoryFromBuildingThreshold, CLOSED_SHAPE_GAPS_THRESHOLD, edgeOpacity, territoryOpacity)