

#include "../include/territory_analyser.h"
// #include "grid.h"
#include <iostream>
#include <array>

// Constructor to initialize the dynamic 2D array
TerritoryAnalyser::TerritoryAnalyser(int givenSize, int numPlayers, int numTeams, std::map<int, int> teamAssignments) 
    : size(givenSize), masterPlayerBoard(givenSize, std::vector<double>(givenSize, 0)), masterPlayerBoardEdges(givenSize, std::vector<double>(givenSize, 0)), masterPlayerBoardFill(givenSize, std::vector<double>(givenSize, 0)), numPlayers(numPlayers), numTeams(numTeams), teamAssignments(teamAssignments) {

    if (teamAssignments.size() != numPlayers) {
        std::cerr << "Error: teamAssignments size must match numPlayers." << std::endl;
        exit(1);
    }
    // Generate building_dict AK: -> should be a seperate file that you read into the program on a global level
    building_dict["House"] = std::make_tuple(2,2);
    building_dict["Barracks"] = std::make_tuple(4,3);
    building_dict["Blacksmith"] = std::make_tuple(3,2);

    for (size_t i = 1; i <= numPlayers; ++i) {
        playerGrids.insert({i, Grid(size)});
    }
    for (size_t i = 1; i <= numTeams; ++i) {
        teamGrids.insert({i, Grid(size)});
    }
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {
            masterPlayerBoard[i][j] = 0;
            masterPlayerBoardEdges[i][j] = 0;
            masterPlayerBoardFill[i][j] = 0;
        }
    }
}


std::vector<std::vector<double>> TerritoryAnalyser::getMasterBoard(std::string type) const {
    if (type == "player") {
        return masterPlayerBoard;
    } else if (type == "player edges") {
        return masterPlayerBoardEdges;
    } else if (type == "player fill") {
        return masterPlayerBoardFill;
    } else if (type == "team") {
        return masterTeamBoard;
    } else if (type == "team edges") {
        return masterTeamBoardEdges;
    } else if (type == "team fill") {
        return masterTeamBoardFill;
    }
    return masterPlayerBoard;
}


void TerritoryAnalyser::addBuilding(size_t x, size_t y, std::string building, int player, int team) {

    size_t r, weight, minRow, maxRow, minCol, maxCol;
    // AK: need to implement a try and except i.e. getRWeight() function or something
    std::tie(r,weight) = building_dict[building];

    // adds territory to both player and team grids
    std::tuple bounds = addTerritory(x,y,r,player,weight);

    // update the boolean grids for the player and team based on the new territory values, only in the area of the new territory to save time
    playerGrids.at(player).terrainBoolPass(3, bounds);    
    teamGrids.at(team).terrainBoolPass(3, bounds);

}


std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> TerritoryAnalyser::addTerritory(size_t centerX, size_t centerY, size_t r, size_t player, size_t weighting, size_t soft_edge) {
    // adds a filled circle of values centered on coordinates x,y to the dataTruth array
    
    size_t x, y, d, threshold;
    double xDiff, yDiff, dist;
    
    r = r+soft_edge; // to merge territory smoothly

    // Determine the bounding box to avoid checking the entire grid
    size_t minRow = std::max(0, static_cast<int>(centerX - r)),
           maxRow = std::min(static_cast<int>(size - 1), static_cast<int>(centerX + r + 1)),
           minCol = std::max(0, static_cast<int>(centerY - r)),
           maxCol = std::min(static_cast<int>(size - 1), static_cast<int>(centerY + r + 1));

    threshold = weighting;
            
    // iterate rows (centerX) then cols (centerY) and index as dataTruth[row][col]
    for (x = minRow; x < maxRow; ++x) {
        double xDiff = double(x) - double(centerX);
        for (y = minCol; y < maxCol; ++y) {
            double yDiff = double(y) - double(centerY);
            dist = std::sqrt((xDiff * xDiff) + (yDiff * yDiff));

            // update the player and team grids with the new territory values
            playerGrids.at(player).setValue(x,y, playerGrids.at(player).getValue(x,y) + ((dist > r) ? 0 : std::min(double(r - dist), double(weighting))));
            teamGrids.at(teamAssignments.at(player)).setValue(x,y, teamGrids.at(teamAssignments.at(player)).getValue(x,y) + ((dist > r) ? 0 : std::min(double(r - dist), double(weighting))));
        }
    }

    return  std::make_tuple(minRow, maxRow, minCol, maxCol);
}


void TerritoryAnalyser::printPlayerBoards() {
    int counter = 1;
    for (const auto& pair : playerGrids) {
        const Grid& board = pair.second;

        std::cout << "Player" << counter << " Board:" << std::endl;
        ::printBoard(board.getDataTruth());
        std::cout << std::endl;

        std::cout << "Player " << counter << " Board Bool:" << std::endl;
        ::printBoard(board.getDataBool());
        std::cout << std::endl;

        counter += 1;
    }
}
    

void TerritoryAnalyser::mapMergedTerritories() {

    Grid mergedPlayerBoard(size), mergedTeamBoard(size);
    int counter = 1;
    std::cout << "Merging player boards..." << std::endl;
    for (const auto& element : teamAssignments) {
        std::cout << element.first << ": " << element.second << std::endl;
    }
    for (const auto& pair : playerGrids) {
        mergedPlayerBoard.addBoard(pair.second.getDataBool(), counter);
        counter = counter * 2;
    }
    counter = 1;
    for (const auto& pair : teamGrids) {
        mergedTeamBoard.addBoard(pair.second.getDataBool(), counter);
        counter = counter * 2;
    }

    masterPlayerBoard = mergedPlayerBoard.getDataTruth();
    masterTeamBoard = mergedTeamBoard.getDataTruth();
}


std::tuple<cv::Mat, cv::Mat> TerritoryAnalyser::colour_pass() {


    // copy the board to avoid modifying the original one when we remove players from contested cells
    std::array<std::vector<std::vector<double>>, 2> boards = {masterPlayerBoard, masterTeamBoard};
    std::tuple<cv::Mat, cv::Mat> output;
    
    cv::Mat mat(size,size, CV_8UC3), matPlayer(size, size, CV_8UC3), matTeam(size, size, CV_8UC3);
    
    // create new board
    std::vector<std::vector<double>> board(size, std::vector<double>(size, 0));

    for (const auto& masterBoard : boards) {


        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                board[i][j] = masterBoard[i][j];
            }
        }
        bool existing_val = false;

        // For now, we make contested areas grey, the other idea was to have it be perpendicular lines of the two/multiple players, but that is more complex to implement and may not be worth the effort for the visualisation.
        std::map<int, cv::Scalar> pl_colours = {{0, cv::Scalar(0, 0, 0)}, // Unoccupied - black
                    {1, cv::Scalar(255, 0, 0)},   // Player 1 - blue
                    {2, cv::Scalar(0, 0, 255)},   // Player 2 - red
                    {3, cv::Scalar(0, 255, 0)},   // Player 3 - green
                    {4, cv::Scalar(170, 170, 170)}}; // Contested - grey

        for (size_t i = 0; i < board.size(); ++i) {
            for (size_t j = 0; j < board.size(); ++j) {
                existing_val = false;
                if (board[i][j] == 0) {
                    mat.at<cv::Vec3b>(i, j) = cv::Vec3b(pl_colours[0][0], pl_colours[0][1], pl_colours[0][2]);
                    continue;
                }
                for (size_t k=8; k>0; k--) {
                    if (static_cast<int>(board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) {
                        board[i][j] = static_cast<double>(static_cast<int>(board[i][j]) % static_cast<int>(std::pow(2, k-1))); // remove the highest power of 2 to find out if there are multiple players in the cell
                        if (!existing_val) { // if the cell is currently unoccupied, assign it to the player
                            mat.at<cv::Vec3b>(i, j) = cv::Vec3b(static_cast<uchar>(pl_colours[k][0]), static_cast<uchar>(pl_colours[k][1]), static_cast<uchar>(pl_colours[k][2]));
                            existing_val = true;
                        } else { // if the cell is already occupied, mark it as contested
                            mat.at<cv::Vec3b>(i, j) = cv::Vec3b(static_cast<uchar>(pl_colours[4][0]), static_cast<uchar>(pl_colours[4][1]), static_cast<uchar>(pl_colours[4][2])); // Contested - grey
                            break; // no need to check further players since it's already contested
                        }
                    }
                }
            }
        }

        if (masterBoard == masterPlayerBoard) {
            matPlayer = mat.clone();
        } else {
            matTeam = mat;
        }
    }

    return {matPlayer, matTeam};
}


