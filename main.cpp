#include "include/header.h"



int main() {


    // Initialise variables (some for testing purposes)
    bool testing = true;
    size_t size = 30, threshold = 3;
    std::string output_directory = "output/";

    std::map<int, int> teamAssignments = {{1, 1}, {2, 2}, {3, 1}, {4, 2}}; // player 1 and 3 are in team 1, player 2 is in team 2
    std::vector<std::vector<double>> complete_board(size, std::vector<double>(size, 0));
    TerritoryAnalyser analyser(size, 4, 2, teamAssignments); 

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
    // p2
    analyser.updateBuilding(15,7,"Barracks",2,2, "add");
    analyser.updateBuilding(14,5,"House",2,2, "add");
    analyser.updateBuilding(13,3,"House",2,2, "add");
    analyser.updateBuilding(13,1,"House",2,2, "add");
    analyser.updateBuilding(17,10,"Blacksmith",2,2, "add");
    analyser.updateBuilding(20,10,"House",2,2, "add");
    analyser.updateBuilding(22,10,"House",2,2, "add");
    analyser.updateBuilding(24,10,"House",2,2, "add");
    analyser.updateBuilding(26,10,"House",2,2, "add");
    analyser.updateBuilding(28,9,"House",2,2, "add");
    analyser.updateBuilding(29,7,"House",2,2, "add");
    // p3
    analyser.updateBuilding(16,16,"Barracks",3,1, "add");
    analyser.updateBuilding(17,14,"House",3,1, "add");
    analyser.updateBuilding(17,17,"House",3,1, "add");
    analyser.updateBuilding(16,18,"House",3,1, "add");
    analyser.updateBuilding(18,13,"Blacksmith",3,1, "add");

    // Print Boards for testing
    if (testing == true) {
        analyser.printPlayerBoards();
    }

    // Final display using OpenCV example
    cv::Mat final_output, final_output_team;
    std::cout << "Territory Analyser results" << std::endl;
    std::tie(final_output, final_output_team) = analyser.colour_pass();

    createDirectoryIfNotExists("output");
    if (final_output.empty()) {
        std::cerr << "Error: Could not create final output image." << std::endl;
        return 1;
    }
    if (final_output_team.empty()) {
        std::cerr << "Error: Could not create final team output image." << std::endl;
        return 1;
    }
    
    if (!cv::imwrite(output_directory + "Territory Analyser Results.png", final_output)) {
        std::cerr << "Error: Could not write final output image." << std::endl;
        return 1;
    }
    if (!cv::imwrite(output_directory + "Territory Analyser Results Team.png", final_output_team)) {
        std::cerr << "Error: Could not write final team output image." << std::endl;
        return 1;
    }
    std::cout << "Current path: " << std::filesystem::current_path() << std::endl;
    return 0;

}



