#include "include/header.h"



int main() {


    auto prog_start_time = std::chrono::high_resolution_clock::now();
    // Initialise Config structure from json file
    AppConfig::set(loadConfig("settings.json"));
    const Config& config = AppConfig::get();

    // Initialise analyser
    TerritoryAnalyser analyser(config.mapSize, config.numberOfPlayers, config.numberOfTeams, config.teamAssignments, config.territoryFromBuildingThreshold); 

    // place some 'buildings'
    // gaia
    // forest1 
    analyser.updateBuilding(7,11,"Tree",0,0, "add");
    analyser.updateBuilding(8,10,"Tree",0,0, "add");
    analyser.updateBuilding(8,11,"Tree",0,0, "add");
    analyser.updateBuilding(9,10,"Tree",0,0, "add");
    analyser.updateBuilding(9,11,"Tree",0,0, "add");
    analyser.updateBuilding(9,12,"Tree",0,0, "add");
    analyser.updateBuilding(10,9,"Tree",0,0, "add");
    analyser.updateBuilding(10,10,"Tree",0,0, "add");
    analyser.updateBuilding(10,11,"Tree",0,0, "add");
    analyser.updateBuilding(10,12,"Tree",0,0, "add");
    analyser.updateBuilding(11,10,"Tree",0,0, "add");
    analyser.updateBuilding(11,11,"Tree",0,0, "add");
    analyser.updateBuilding(12,11,"Tree",0,0, "add");

    // forest 2
    analyser.updateBuilding(15,18,"Tree",0,0, "add");
    analyser.updateBuilding(16,18,"Tree",0,0, "add");
    analyser.updateBuilding(16,19,"Tree",0,0, "add");
    analyser.updateBuilding(17,17,"Tree",0,0, "add");
    analyser.updateBuilding(17,18,"Tree",0,0, "add");
    analyser.updateBuilding(17,19,"Tree",0,0, "add");
    analyser.updateBuilding(17,20,"Tree",0,0, "add");
    analyser.updateBuilding(18,18,"Tree",0,0, "add");
    analyser.updateBuilding(18,19,"Tree",0,0, "add");
    analyser.updateBuilding(19,19,"Tree",0,0, "add");

    // gold
    analyser.updateBuilding(14,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(15,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(15,11,"Gold Mine",0,0, "add");
    analyser.updateBuilding(16,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(16,11,"Gold Mine",0,0, "add");


    // p1
    analyser.updateBuilding(6,11,"Barracks",1,1, "add");
    analyser.updateBuilding(5,10,"House",1,1, "add");
    analyser.updateBuilding(3,8,"House",1,1, "add");
    analyser.updateBuilding(1,6,"House",1,1, "add");
    analyser.updateBuilding(9,10,"Blacksmith",1,1, "add");
    analyser.updateBuilding(11,11,"Barracks",1,1, "add");
    analyser.updateBuilding(16,12,"House",1,1, "add");
    
    analyser.updateBuilding(23,2,"House",1,1, "add");
    // p2
    analyser.updateBuilding(15,7,"Barracks",2,2, "add");
    // analyser.updateBuilding(13,5,"House",2,2, "add"); // remove this for good gap testing
    analyser.updateBuilding(13,3,"House",2,2, "add");
    analyser.updateBuilding(13,1,"House",2,2, "add");
    analyser.updateBuilding(11,0,"House",2,2, "add");
    analyser.updateBuilding(17,10,"Blacksmith",2,2, "add");
    analyser.updateBuilding(20,10,"House",2,2, "add");
    analyser.updateBuilding(22,10,"House",2,2, "add");
    analyser.updateBuilding(24,10,"House",2,2, "add");
    analyser.updateBuilding(26,10,"House",2,2, "add");
    analyser.updateBuilding(28,9,"House",2,2, "add");

    // time how long it takes to update the final building to test performance
    auto start_time = std::chrono::high_resolution_clock::now();
    analyser.updateBuilding(26,0,"House",2,2, "add");
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Time taken to update one (final => slowest) building: " << duration.count() << " ms" << std::endl;
    
    // p3
    analyser.updateBuilding(16,16,"Barracks",3,1, "add");
    analyser.updateBuilding(17,14,"House",3,1, "add");
    analyser.updateBuilding(17,17,"House",3,1, "add");
    analyser.updateBuilding(16,18,"House",3,1, "add");
    analyser.updateBuilding(18,13,"Blacksmith",3,1, "add");
    analyser.updateBuilding(20,16,"House",3,1, "add");
    analyser.updateBuilding(20,18,"House",3,1, "add");
    analyser.updateBuilding(20,20,"House",3,1, "add");
    analyser.updateBuilding(19,22,"House",3,1, "add");
    analyser.updateBuilding(19,24,"House",3,1, "add");
    analyser.updateBuilding(19,26,"House",3,1, "add");
    analyser.updateBuilding(21,28,"House",3,1, "add");

    analyser.updateBuilding(4,20,"Barracks",3,1, "add");
    analyser.updateBuilding(4,25,"Barracks",3,1, "add");
    analyser.updateBuilding(9,26,"Blacksmith",3,1, "add");


    // Print Boards for testing
    if (config.testingMode == true) {
        analyser.printPlayerBoards();
        printBoard(analyser.getMasterBoardFill("player"));
    }

    

    // Total program time 
    auto prog_end_time = std::chrono::high_resolution_clock::now();
    auto prog_duration = std::chrono::duration_cast<std::chrono::milliseconds>(prog_end_time - prog_start_time);
    std::cout << "Total program time: " << prog_duration.count() << " ms" << std::endl;

    createDirectoryIfNotExists("output");
    if (analyser.getFinalTerritoryMap("player").empty()) {
        std::cerr << "Error: Could not create final output image." << std::endl;
        return 1;
    }
    if (analyser.getFinalTerritoryMap("team").empty()) {
        std::cerr << "Error: Could not create final team output image." << std::endl;
        return 1;
    }
    
    if (!cv::imwrite(config.outputDirectory + "Territory Analyser Results.png", analyser.getFinalTerritoryMap("player"))) {
        std::cerr << "Error: Could not write final output image." << std::endl;
        return 1;
    }
    if (!cv::imwrite(config.outputDirectory + "Territory Analyser Results Team.png", analyser.getFinalTerritoryMap("team"))) {
        std::cerr << "Error: Could not write final team output image." << std::endl;
        return 1;
    }
    std::cout << "Current path: " << std::filesystem::current_path() << std::endl;
    return 0;

}



