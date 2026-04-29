#include "include/header.h"

namespace {

bool outputForTests(const TerritoryAnalyser& analyser, const Config& config, std::string file_name = "2x3_Test") {
    auto toBgr = [](const cv::Mat& src) {
        if (src.channels() == 3) return src.clone();
        cv::Mat out;
        if (src.channels() == 4) {
            cv::cvtColor(src, out, cv::COLOR_BGRA2BGR);
        } else {
            cv::cvtColor(src, out, cv::COLOR_GRAY2BGR);
        }
        return out;
    };

    cv::Mat playerFinal = toBgr(analyser.getFinalTerritoryMap("player"));
    cv::Mat teamFinal = toBgr(analyser.getFinalTerritoryMap("team"));

    auto visualizeMaskBoard = [mapSize = config.mapSize](
        const std::vector<std::vector<double>>& board,
        const std::map<int, std::array<int, 4>>& palette) {
        cv::Mat visual(mapSize, mapSize, CV_8UC3, cv::Scalar(0, 0, 0));
        for (size_t i = 0; i < mapSize; ++i) {
            for (size_t j = 0; j < mapSize; ++j) {
                if (i >= board.size() || j >= board[i].size()) continue;
                const double raw = board[i][j];
                if (raw <= 0.0) continue;

                const size_t mask = static_cast<size_t>(std::llround(raw));
                if (mask == 0) continue;

                if ((mask & (mask - 1)) == 0) {
                    const int owner = static_cast<int>(std::log2(mask)) + 1;
                    auto it = palette.find(owner);
                    if (it != palette.end()) {
                        const auto& c = it->second;
                        visual.at<cv::Vec3b>(i, j) = cv::Vec3b(
                            static_cast<uchar>(c[0]),
                            static_cast<uchar>(c[1]),
                            static_cast<uchar>(c[2]));
                    }
                } else {
                    // Multi-bit ownership value: show as contested in gray.
                    visual.at<cv::Vec3b>(i, j) = cv::Vec3b(140, 140, 140);
                }
            }
        }
        return visual;
    };

    cv::Mat masterPlayerVisual = visualizeMaskBoard(analyser.getMasterBoard("player"), config.playerColours);
    cv::Mat masterTeamVisual = visualizeMaskBoard(analyser.getMasterBoard("team"), config.teamColours);

    auto visualizeGapCells = [mapSize = config.mapSize](
        const std::vector<std::vector<std::pair<size_t, size_t>>>& gaps,
        const std::map<int, std::array<int, 4>>& palette) {
        cv::Mat visual(mapSize, mapSize, CV_8UC3, cv::Scalar(0, 0, 0));
        for (size_t owner = 0; owner < gaps.size(); ++owner) {
            auto it = palette.find(static_cast<int>(owner));
            if (it == palette.end()) continue;
            const auto& c = it->second;
            const cv::Vec3b colour(
                static_cast<uchar>(c[0]),
                static_cast<uchar>(c[1]),
                static_cast<uchar>(c[2]));

            for (const auto& cell : gaps[owner]) {
                if (cell.first >= mapSize || cell.second >= mapSize) continue;
                visual.at<cv::Vec3b>(cell.first, cell.second) = colour;
            }
        }
        return visual;
    };

    cv::Mat playerGapCellsVisual = visualizeGapCells(analyser.getGaps("player"), config.playerColours);
    cv::Mat teamGapCellsVisual = visualizeGapCells(analyser.getGaps("team"), config.teamColours);

    const auto obstructionBoard = analyser.getMasterObstructionBoard("player");
    cv::Mat obstructionVisual(config.mapSize, config.mapSize, CV_8UC3, cv::Scalar(0, 0, 0));
    for (size_t i = 0; i < config.mapSize; ++i) {
        for (size_t j = 0; j < config.mapSize; ++j) {
            if (obstructionBoard[i][j] == -1) continue;
            if (obstructionBoard[i][j] == 0) {
                obstructionVisual.at<cv::Vec3b>(i, j) = cv::Vec3b(255, 255, 255);
                continue;
            }
            size_t num = static_cast<size_t>(std::log2(obstructionBoard[i][j])) + 1;
            cv::Vec3b colour(
                static_cast<uchar>(config.playerColours.at(num)[0]),
                static_cast<uchar>(config.playerColours.at(num)[1]),
                static_cast<uchar>(config.playerColours.at(num)[2]));
            obstructionVisual.at<cv::Vec3b>(i, j) = colour;
        }
    }

    const auto walkableBoard = analyser.getWalkableTerrainBoard();
    cv::Mat walkableVisual(config.mapSize, config.mapSize, CV_8UC3, cv::Scalar(255, 0, 0)); // blue for non-walkable
    for (size_t i = 0; i < config.mapSize; ++i) {
        for (size_t j = 0; j < config.mapSize; ++j) {
            if (walkableBoard[i][j]) {
                walkableVisual.at<cv::Vec3b>(i, j) = cv::Vec3b(0, 255, 0); // green for walkable
            }
        }
    }

    const int outputScale = 4;
    auto upscaleImage = [outputScale](const cv::Mat& src) {
        cv::Mat scaled;
        cv::resize(src, scaled, cv::Size(), outputScale, outputScale, cv::INTER_NEAREST);
        return scaled;
    };

    playerFinal = upscaleImage(playerFinal);
    teamFinal = upscaleImage(teamFinal);
    masterPlayerVisual = upscaleImage(masterPlayerVisual);
    masterTeamVisual = upscaleImage(masterTeamVisual);
    playerGapCellsVisual = upscaleImage(playerGapCellsVisual);
    teamGapCellsVisual = upscaleImage(teamGapCellsVisual);
    obstructionVisual = upscaleImage(obstructionVisual);
    walkableVisual = upscaleImage(walkableVisual);

    cv::Mat topRow, bottomRow, testGrid;
    cv::hconcat(std::vector<cv::Mat>{playerFinal, masterPlayerVisual, playerGapCellsVisual, obstructionVisual}, topRow);
    cv::hconcat(std::vector<cv::Mat>{teamFinal, masterTeamVisual, teamGapCellsVisual, walkableVisual}, bottomRow);
    cv::vconcat(topRow, bottomRow, testGrid);

    const int leftLabelWidth = 100;
    const int topLabelHeight = 55;
    const int rightPadding = 20;
    const int bottomPadding = 20;

    cv::Mat labeledGrid(
        testGrid.rows + topLabelHeight + bottomPadding,
        testGrid.cols + leftLabelWidth + rightPadding,
        CV_8UC3,
        cv::Scalar(30, 30, 30));

    testGrid.copyTo(labeledGrid(cv::Rect(leftLabelWidth, topLabelHeight, testGrid.cols, testGrid.rows)));

    const cv::Scalar separatorColor(255, 255, 255);
    const int separatorThickness = 1;
    const int cellSize = static_cast<int>(config.mapSize) * outputScale;

    for (int col = 0; col <= 4; ++col) {
        const int x = leftLabelWidth + (col * cellSize);
        cv::line(labeledGrid, cv::Point(x, 0), cv::Point(x, topLabelHeight + testGrid.rows), separatorColor, separatorThickness, cv::LINE_AA);
    }

    for (int row = 0; row <= 2; ++row) {
        const int y = topLabelHeight + (row * cellSize);
        cv::line(labeledGrid, cv::Point(0, y), cv::Point(leftLabelWidth + testGrid.cols, y), separatorColor, separatorThickness, cv::LINE_AA);
    }

    cv::rectangle(
        labeledGrid,
        cv::Rect(0, 0, leftLabelWidth + testGrid.cols, topLabelHeight + testGrid.rows),
        separatorColor,
        separatorThickness,
        cv::LINE_AA);

    const int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    const int thickness = 1;
    const cv::Scalar textColor(235, 235, 235);

    auto drawCenteredText = [&](const std::string& text, int centerX, int centerY, double fontScale = 0.75) {
        int baseline = 0;
        const cv::Size textSize = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        const int x = centerX - (textSize.width / 2);
        const int y = centerY + (textSize.height / 2);
        cv::putText(labeledGrid, text, cv::Point(x, y), fontFace, fontScale, textColor, thickness, cv::LINE_AA);
    };

    const std::vector<std::string> columnLabels = {
        "Final",
        "Raw terr",
        "Gaps",
        "Obs/Wlkable"
    };

    for (int col = 0; col < static_cast<int>(columnLabels.size()); ++col) {
        const int centerX = leftLabelWidth + (col * cellSize) + (cellSize / 2);
        drawCenteredText(columnLabels[col], centerX, topLabelHeight / 2, 0.55);
    }

    drawCenteredText("Player", leftLabelWidth / 2, topLabelHeight + (cellSize / 2));
    drawCenteredText("Team", leftLabelWidth / 2, topLabelHeight + cellSize + (cellSize / 2));

    return cv::imwrite(config.outputDirectory + file_name + ".png", labeledGrid);
}

} // namespace



int main() {


    auto prog_start_time = std::chrono::high_resolution_clock::now();
    // Initialise Config structure from json file
    AppConfig::set(loadConfig("settings.json"));
    const Config& config = AppConfig::get();

    // Initialise analyser
    TerritoryAnalyser analyser(config.mapSize, config.numberOfPlayers, config.numberOfTeams, config.teamAssignments, config.rawTerritoryOwnershipThreshold); 

    // place some 'buildings'
    // gaia
    // forest1 
    analyser.updateBuilding(7,8,"Tree",0,0, "add");
    analyser.updateBuilding(7,9,"Tree",0,0, "add");
    analyser.updateBuilding(7,10,"Tree",0,0, "add");
    analyser.updateBuilding(8,8,"Tree",0,0, "add");
    analyser.updateBuilding(8,9,"Tree",0,0, "add");
    analyser.updateBuilding(8,10,"Tree",0,0, "add");
    analyser.updateBuilding(9,7,"Tree",0,0, "add");
    analyser.updateBuilding(9,8,"Tree",0,0, "add");
    analyser.updateBuilding(9,9,"Tree",0,0, "add");
    analyser.updateBuilding(10,7,"Tree",0,0, "add");
    analyser.updateBuilding(10,8,"Tree",0,0, "add");
    analyser.updateBuilding(10,9,"Tree",0,0, "add");
    analyser.updateBuilding(11,9,"Tree",0,0, "add");

    // forest 2
    analyser.updateBuilding(15,20,"Tree",0,0, "add");
    analyser.updateBuilding(15,21,"Tree",0,0, "add");
    analyser.updateBuilding(16,20,"Tree",0,0, "add");
    analyser.updateBuilding(16,21,"Tree",0,0, "add");
    analyser.updateBuilding(16,22,"Tree",0,0, "add");
    analyser.updateBuilding(17,20,"Tree",0,0, "add");
    analyser.updateBuilding(17,21,"Tree",0,0, "add");
    analyser.updateBuilding(17,22,"Tree",0,0, "add");
    analyser.updateBuilding(18,21,"Tree",0,0, "add");
    analyser.updateBuilding(18,22,"Tree",0,0, "add");

    // p2 rigor test gap cells
    analyser.updateBuilding(11,4,"Tree",0,0, "add");
    analyser.updateBuilding(11,5,"Tree",0,0, "add");
    analyser.updateBuilding(11,6,"Tree",0,0, "add");
    analyser.updateBuilding(11,7,"Tree",0,0, "add");
    analyser.updateBuilding(11,8,"Tree",0,0, "add");
    analyser.updateBuilding(12,6,"Tree",0,0, "add");
    analyser.updateBuilding(12,7,"Tree",0,0, "add");
    analyser.updateBuilding(12,8,"Tree",0,0, "add");
    analyser.updateBuilding(13,7,"Tree",0,0, "add");
    analyser.updateBuilding(13,8,"Tree",0,0, "add");


    // gold
    analyser.updateBuilding(14,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(15,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(15,11,"Gold Mine",0,0, "add");
    analyser.updateBuilding(16,10,"Gold Mine",0,0, "add");
    analyser.updateBuilding(16,11,"Gold Mine",0,0, "add");


    // p1
    analyser.updateBuilding(6,11,"Barracks",1,1, "add");
    analyser.updateBuilding(5,8,"House",1,1, "add");
    analyser.updateBuilding(3,8,"House",1,1, "add");
    analyser.updateBuilding(1,6,"House",1,1, "add");
    analyser.updateBuilding(9,10,"Blacksmith",1,1, "add");
    analyser.updateBuilding(12,13,"Barracks",1,1, "add");
    analyser.updateBuilding(15,13,"House",1,1, "add");
    
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
    analyser.updateBuilding(26,0,"House",2,2, "add");

    analyser.updateBuilding(27,4,"Barracks",2,2, "add");

    
    // p3
    analyser.updateBuilding(22,14,"Barracks",3,1, "add");
    analyser.updateBuilding(17,14,"House",3,1, "add");
    analyser.updateBuilding(17,17,"House",3,1, "add");
    analyser.updateBuilding(14,17,"House",3,1, "add");
    analyser.updateBuilding(19,13,"Blacksmith",3,1, "add");
    analyser.updateBuilding(20,16,"House",3,1, "add");
    analyser.updateBuilding(20,18,"House",3,1, "add");
    analyser.updateBuilding(20,20,"House",3,1, "add");
    analyser.updateBuilding(19,22,"House",3,1, "add");
    analyser.updateBuilding(19,24,"House",3,1, "add");
    analyser.updateBuilding(19,26,"House",3,1, "add");
    analyser.updateBuilding(21,28,"House",3,1, "add");

    // water test
    analyser.updateBuilding(25,25,"Barracks",3,1, "add");

    // fill test
    analyser.updateBuilding(4,20,"Barracks",3,1, "add");
    analyser.updateBuilding(4,25,"Barracks",3,1, "add");

    auto start_time = std::chrono::high_resolution_clock::now();
    analyser.updateBuilding(14,27,"Blacksmith",3,1, "add");
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Time taken to add one (final => slowest) building: " << duration.count() << " ms" << std::endl;
    
    start_time = std::chrono::high_resolution_clock::now();
    analyser.updateBuilding(14,27,"Blacksmith",3,1, "remove");
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Time taken to remove one (final => slowest) building: " << duration.count() << " ms" << std::endl;
    
    
    analyser.updateRender();
    
    if (config.testingMode) {
        if (!outputForTests(analyser, config, "pre_defeat")) {
            std::cerr << "Error: Could not write 2x3 testing image." << std::endl;
            return 1;
        }
    }

    // Test player defeated
    start_time = std::chrono::high_resolution_clock::now();
    analyser.playerDefeated(1);
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Time taken to defeat player: " << duration.count() << " ms" << std::endl;

    analyser.updateRender();


    //  for testing, write the obstructions board to an image
    // if(config.testingMode == true) {
    if (config.testingMode) {
        if (!outputForTests(analyser, config, "post_defeat")) {
            std::cerr << "Error: Could not write 2x3 testing image." << std::endl;
            return 1;
        }
    }

    analyser.updateBuilding(1,6,"House",1,1, "remove");
    
    
    start_time = std::chrono::high_resolution_clock::now();
    analyser.updateRender();
    end_time = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Time taken to update render: " << duration.count() << " ms" << std::endl;

    

    if (config.testingMode) {
        if (!outputForTests(analyser, config, "remove_post_defeat")) {
            std::cerr << "Error: Could not write 2x3 testing image." << std::endl;
            return 1;
        }
    }


    // Print Boards for testing
    if (config.testingMode == true && false) {
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
    

    std::vector<std::vector<int>> testGapsBoardPlayer(config.mapSize, std::vector<int>(config.mapSize, -1));
    const auto& gapBoardPlayer = analyser.getGaps("player");
    for(int player = 0; player < gapBoardPlayer.size(); ++player) {
        for(int cell = 0; cell < gapBoardPlayer[player].size(); ++cell) {
            // Process each gap cell
            // std::cout << "Player " << player << " has a gap at cell (" << cell << ") with coordinates (" << gapBoardPlayer[player][cell].first << ", " << gapBoardPlayer[player][cell].second << ")" << std::endl;
            const auto& gap = gapBoardPlayer[player][cell];
            testGapsBoardPlayer[gap.first][gap.second] = player; // Mark the gap cell with the player number (or any other value you want)
        }
    } 
    // printBoard(testGapsBoardPlayer, 0, true);
    
    std::vector<std::vector<int>> testGapsBoardTeam(config.mapSize, std::vector<int>(config.mapSize, -1));
    const auto& gapBoardTeam = analyser.getGaps("team");
    for(int team = 0; team < gapBoardTeam.size(); ++team) {
        for(int cell = 0; cell < gapBoardTeam[team].size(); ++cell) {
            // Process each gap cell
            // std::cout << "Team " << team << " has a gap at cell (" << cell << ") with coordinates (" << gapBoardTeam[team][cell].first << ", " << gapBoardTeam[team][cell].second << ")" << std::endl;
            const auto& gap = gapBoardTeam[team][cell];
            testGapsBoardTeam[gap.first][gap.second] = team; // Mark the gap cell with the team number (or any other value you want)
        }
    }  
    // printBoard(testGapsBoardTeam, 0);

    if(config.contestedTerritoryMethod == "flash") {
    while(true) {
        analyser.updateContestedFlash();
        cv::imwrite(config.outputDirectory + "Territory Analyser Results.png", analyser.getFinalTerritoryMap("player"));
        cv::imwrite(config.outputDirectory + "Territory Analyser Results Team.png", analyser.getFinalTerritoryMap("team"));
    }
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



