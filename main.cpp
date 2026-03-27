#include "include/header.h"


int main() {


    // Initialise variables (some for testing purposes)
    bool testing = true;
    size_t size = 30, threshold = 3;

    std::map<int, int> teamAssignments = {{1, 1}, {2, 2}, {3, 1}, {4, 2}}; // player 1 and 3 are in team 1, player 2 is in team 2
    std::vector<std::vector<double>> complete_board(size, std::vector<double>(size, 0));
    TerritoryAnalyser analyser(size, 4, 2, teamAssignments); 

    // place some 'buildings'
    // p1
    analyser.addBuilding(6,11,"Barracks",1,1);
    analyser.addBuilding(5,10,"House",1,1);
    analyser.addBuilding(3,8,"House",1,1);
    analyser.addBuilding(1,6,"House",1,1);
    analyser.addBuilding(9,10,"Blacksmith",1,1);
    // p2
    analyser.addBuilding(15,7,"Barracks",2,2);
    analyser.addBuilding(14,5,"House",2,2);
    analyser.addBuilding(13,3,"House",2,2);
    analyser.addBuilding(13,1,"House",2,2);
    analyser.addBuilding(17,10,"Blacksmith",2,2);
    // p3
    analyser.addBuilding(16,16,"Barracks",3,1);
    analyser.addBuilding(17,14,"House",3,1);
    analyser.addBuilding(17,17,"House",3,1);
    analyser.addBuilding(16,18,"House",3,1);
    analyser.addBuilding(18,13,"Blacksmith",3,1);

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
    cv::imwrite("output/Territory Analyser Results.png", final_output);
    cv::imwrite("output/Territory Analyser Results Team.png", final_output_team);

    return 0;

}



