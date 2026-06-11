#include "include/header.h"



int main() {

    auto progStartTime = std::chrono::high_resolution_clock::now();
    // Initialise Config structure from json file
    AppConfig::set(loadConfig("settings.json"));
    const Config& config = AppConfig::get();

    // Initialise analyser
    // std::string initBoardStatePath = "examples/AAAImageExample.txt";
    std::string initBoardStatePath = "examples/example1.txt";
    TerritoryAnalyser analyser(config.mapSize, config.numberOfPlayers, config.numberOfTeams, config.teamAssignments, config.rawTerritoryOwnershipThreshold, initBoardStatePath);

    // Load example
    // if (!loadBuildingCommandsFromFile(analyser, "examples/example1.txt")) {
    //     return 1;
    // }

    // Measure tests
    if (initBoardStatePath == "examples/example1.txt") {
        measureAndLogExecutionTime<int>("add one (final => slowest) building", 
            std::function<int()>([&analyser]() { analyser.updateBuilding(27,14,"Blacksmith",3, "add"); return 0; } )
        );
        
        measureAndLogExecutionTime<int>("remove one (final => slowest) building", 
            std::function<int()>([&analyser]() { analyser.updateBuilding(27,14,"Blacksmith",3, "remove"); return 0; } )
        );
        
        
        analyser.updateRender("player");
        analyser.updateRender("team");
        
        if (config.testingMode) {
            if (!outputForTests(analyser, config, "pre_defeat")) {
                std::cerr << "Error: Could not write 2x3 testing image." << std::endl;
                return 1;
            }
        }

        std::cout << std::endl;
        measureAndLogExecutionTime<int>("remove connected building", 
            // std::function<int()>([&analyser]() { analyser.updateBuilding(7,29,"House",2, "remove"); return 0; } )
            std::function<int()>([&analyser]() { analyser.updateBuilding(17,42,"House",4, "remove"); return 0; } )
        );
        std::cout << std::endl;
        // Test player defeated
        // measureAndLogExecutionTime<int>("defeat player", 
        //     std::function<int()>([&analyser]() { analyser.playerDefeated(1); return 0; } )
        // );

        analyser.updateRender("player");
        analyser.updateRender("team");


        // if(config.testingMode == true) {
        if (config.testingMode) {
            if (!outputForTests(analyser, config, "post_defeat")) {
                std::cerr << "Error: Could not write 2x3 testing image." << std::endl;
                return 1;
           }

            measureAndLogExecutionTime<int>("remove building from defeated player", 
                std::function<int()>([&analyser]() { analyser.updateBuilding(6,1,"House",1, "remove"); return 0; } )
            );
        }
    }
    
    measureAndLogExecutionTime<int>("update render (player)", 
        std::function<int()>([&analyser]() { analyser.updateRender("player"); return 0; } )
    );
    measureAndLogExecutionTime<int>("update render (team)", 
        std::function<int()>([&analyser]() { analyser.updateRender("team"); return 0; } )
    );

    


    

    // Total program time 
    auto progEndTime = std::chrono::high_resolution_clock::now(); auto progDuration = std::chrono::duration_cast<std::chrono::milliseconds>(progEndTime - progStartTime);
    std::cout << "Total program time: " << progDuration.count() << " ms" << std::endl;


    // Output final results
    createDirectoryIfNotExists(config.outputDirectory);

    if (!outputForTests(analyser, config, "FinalTestingOutput")) {std::cerr << "Error: Could not write final testing image." << std::endl;return 1;}

    if(config.contestedTerritoryMethod == "flash") {
        while(true) {
            analyser.updateContestedFlash();
            cv::imwrite(config.outputDirectory + "Territory Analyser Results.png", analyser.getFinalTerritoryMap("player"));
            cv::imwrite(config.outputDirectory + "Territory Analyser Results Team.png", analyser.getFinalTerritoryMap("team"));
        }
    }
    // reprint the final output in case flash method broke it.
    if (!cv::imwrite(config.outputDirectory + "Territory Analyser Results.png", analyser.getFinalTerritoryMap("player"))) {std::cerr << "Error: Could not write final output image." << std::endl; return 1;}
    if (!cv::imwrite(config.outputDirectory + "Territory Analyser Results Team.png", analyser.getFinalTerritoryMap("team"))) {std::cerr << "Error: Could not write final team output image." << std::endl; return 1;}
    
    std::cout << "Current path: " << std::filesystem::current_path() << std::endl;
    return 0;

}



