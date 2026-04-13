

#include "../include/territory_analyser.h"
// #include "grid.h"

// Constructor to initialize the dynamic 2D array
TerritoryAnalyser::TerritoryAnalyser(int givenSize, int numPlayers, int numTeams, std::map<int, int> teamAssignments) 
    : size(givenSize), numPlayers(numPlayers), numTeams(numTeams), teamAssignments(teamAssignments), gaia_board(givenSize, std::vector<size_t>(givenSize, 0)),
    masterPlayerBoard(givenSize, std::vector<double>(givenSize, 0)), masterPlayerBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), masterPlayerBoardFill(givenSize, std::vector<size_t>(givenSize, 0)), 
    masterTeamBoard(givenSize, std::vector<double>(givenSize, 0)), masterTeamBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), masterTeamBoardFill(givenSize, std::vector<size_t>(givenSize, 0)) {

    if (teamAssignments.size() != numPlayers) {
        std::cerr << "Error: teamAssignments size must match numPlayers." << std::endl;
        exit(1);
    }
    // Generate building_dict AK: -> should be a seperate file that you read into the program on a global level
    building_dict["House"] = std::make_tuple(2,2);
    building_dict["Barracks"] = std::make_tuple(4,3);
    building_dict["Blacksmith"] = std::make_tuple(3,2);
    building_dict["Tree"] = std::make_tuple(1,1);
    building_dict["Gold Mine"] = std::make_tuple(1,1);
    building_dict["Cliff"] = std::make_tuple(1,1);

    for (size_t i = 1; i <= numPlayers; ++i) {
        playerGrids.insert({i, Grid(size)});
    }
    for (size_t i = 1; i <= numTeams; ++i) {
        teamGrids.insert({i, Grid(size)});
    }
    for (size_t i = 0; i < size; ++i) {
        for (size_t j = 0; j < size; ++j) {

            gaia_board[i][j] = 0; 

            masterPlayerBoard[i][j] = 0;
            masterPlayerBoardEdges[i][j] = 0;
            masterPlayerBoardFill[i][j] = 0;
            
            masterTeamBoard[i][j] = 0;
            masterTeamBoardEdges[i][j] = 0;
            masterTeamBoardFill[i][j] = 0;
        }
    }
}




void TerritoryAnalyser::updateBuilding(size_t x, size_t y, std::string building, int player, int team, std::string mod) {

    size_t r, weight, minRow, maxRow, minCol, maxCol;
    // AK: need to implement a try and except i.e. getRWeight() function or something
    try {
        std::tie(r,weight) = building_dict.at(building);
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: Building type '" << building << "' not found in building_dict." << std::endl;
        // Can set to automatically use a default value for r, such as the dark age vision of the building.
        return;
    }

    // updates territory to both player and team grids
    if (mod == "add") {
        weight = weight;
    } else if (mod == "remove") {
        weight = -weight;
    } else {
        std::cerr << "Error: Invalid modification type specified. Use 'add' or 'remove'." << std::endl;
        return;
    }
    std::tuple bounds = updateTerritory(x,y,r,player,weight);

    // update the boolean grids for the player and team based on the new territory values, only in the area of the new territory to save time
    if (player == 0) { // if player is Gaia, we don't need to update anything else 
        return;
    }
    
    playerGrids.at(player).terrainBoolPass(3, bounds);    
    teamGrids.at(team).terrainBoolPass(3, bounds);
    
    // merge the boards
    this->mapMergedTerritories();
    
    // Set edge boards
    this->masterPlayerBoardEdges = findEdges(this->getMasterBoard("player"), "threeBox");
    this->masterTeamBoardEdges = findEdges(this->getMasterBoard("team"), "threeBox");

    // Set fill boards
    auto playerFillResult = analyseFill(this->getMasterBoard("player"), this->getMasterBoardEdges("player"));
    this->playerGaps = playerFillResult.gaps;
    this->masterPlayerBoardFill = playerFillResult.fillBoard;

    auto teamFillResult = analyseFill(this->getMasterBoard("team"), this->getMasterBoardEdges("team"));
    this->teamGaps = teamFillResult.gaps;
    this->masterTeamBoardFill = teamFillResult.fillBoard;

    // print the fill boards for testing
    std::cout << "Player Fill Board:" << std::endl;
    for (size_t i = 0; i < playerFillResult.fills.size(); ++i) {
        std::cout << "Fill " << i+1 << " (Dominant Player: " << playerFillResult.fills[i].getDominantPlayer() << "):" << std::endl;
        for (const auto& cell : playerFillResult.fills[i].getCells()) {
            std::cout << "(" << cell.first << ", " << cell.second << ") ";
        }
        std::cout << std::endl;
    }


}


std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> TerritoryAnalyser::updateTerritory(size_t centerX, size_t centerY, size_t r, size_t player, size_t weighting, size_t soft_edge) {
    // adds a filled circle of values centered on coordinates x,y to the dataTruth array
    
    size_t x, y, d;
    double xDiff, yDiff, dist;
    
    r = r+soft_edge; // to merge territory smoothly

    // Determine the bounding box to avoid checking the entire grid
    size_t minRow = std::max(0, static_cast<int>(centerX - r)),
           maxRow = std::min(static_cast<int>(size), static_cast<int>(centerX + r + 1)),
           minCol = std::max(0, static_cast<int>(centerY - r)),
           maxCol = std::min(static_cast<int>(size), static_cast<int>(centerY + r + 1));

    // if player is Gaia
    if (player == 0) {
        gaia_board[centerX][centerY] += sgn(weighting); 
    } else {
        // iterate rows (centerX) then cols (centerY) and index as dataTruth[row][col]
        for (x = minRow; x < maxRow; ++x) {

            xDiff = double(x) - double(centerX);

            for (y = minCol; y < maxCol; ++y) {

                yDiff = double(y) - double(centerY);
                dist = std::sqrt((xDiff * xDiff) + (yDiff * yDiff));

                // update the player and team grids with the new territory values
                playerGrids.at(player).setValue(x,y, playerGrids.at(player).getValue(x,y) + ((dist > r) ? 0 : std::min(double(r - dist), double(weighting))));
                teamGrids.at(teamAssignments.at(player)).setValue(x,y, teamGrids.at(teamAssignments.at(player)).getValue(x,y) + ((dist > r) ? 0 : std::min(double(r - dist), double(weighting))));
            }
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
    std::cout << "Gaia Board:" << std::endl;
    ::printBoard(gaia_board);
    std::cout << std::endl;
}
    

void TerritoryAnalyser::mapMergedTerritories() {

    Grid mergedPlayerBoard(size), mergedTeamBoard(size);
    int counter = 1;
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


std::vector<std::vector<double>> TerritoryAnalyser::getMasterBoard(std::string type) const {
    if (type == "player") {
        return masterPlayerBoard;
    } else if (type == "team") {
        return masterTeamBoard;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player board by default." << std::endl;
        return masterPlayerBoard;
    }
}

std::vector<std::vector<size_t>> TerritoryAnalyser::getMasterBoardEdges(std::string type) const {
    if (type == "player") {
        return masterPlayerBoardEdges;
    } else if (type == "team") {
        return masterTeamBoardEdges;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player edges board by default." << std::endl;
        return masterPlayerBoardEdges;
    }
}


std::vector<std::vector<size_t>> TerritoryAnalyser::getMasterBoardFill(std::string type) const {
    if (type == "player") {
        return masterPlayerBoardFill;
    } else if (type == "team") {
        return masterTeamBoardFill;
    } else {
        std::cerr << "Error: Invalid board type specified (" << type << "). Returning player fill board by default." << std::endl;
        return masterPlayerBoardFill;
    }
}


std::tuple<cv::Mat, cv::Mat> TerritoryAnalyser::colour_pass() {


    // copy the board to avoid modifying the original one when we remove players from contested cells
    std::array<std::vector<std::vector<double>>, 2> boards = {masterPlayerBoard, masterTeamBoard};
    std::tuple<cv::Mat, cv::Mat> output;
    
    cv::Mat mat(size,size, CV_8UC4), matPlayer(size, size, CV_8UC4), matTeam(size, size, CV_8UC4);
    
    // create new board
    std::vector<std::vector<double>> board(size, std::vector<double>(size, 0));
    std::vector<std::vector<size_t>> edge_board(size, std::vector<size_t>(size, 0)), fill_board(size, std::vector<size_t>(size, 0));

    for (const auto& masterBoard : boards) {


        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                board[i][j] = masterBoard[i][j];
            }
        }
        if (masterBoard == masterPlayerBoard) {
            edge_board = masterPlayerBoardEdges;
            fill_board = masterPlayerBoardFill;
        } else {
            edge_board = masterTeamBoardEdges;
            fill_board = masterTeamBoardFill;
        }
        bool existing_val = false, is_edge = false;

        // For now, we make contested areas grey, the other idea was to have it be perpendicular lines of the two/multiple players, but that is more complex to implement and may not be worth the effort for the visualisation.
        std::size_t edgeOpacity = 255, territoryOpacity = 111;
        std::map<int, cv::Vec4b> pl_colours = {
                    {0, cv::Vec4b(0, 0, 0, 255)}, // Unoccupied or gaia - black
                    {1, cv::Vec4b(255, 0, 0, 255)},   // Player 1 - blue
                    {2, cv::Vec4b(0, 0, 255, 255)},   // Player 2 - red
                    {3, cv::Vec4b(0, 255, 0, 255)},   // Player 3 - green
                    {4, cv::Vec4b(170, 170, 170, 255)}}; // Contested - grey

        for (size_t i = 0; i < board.size(); ++i) {
            for (size_t j = 0; j < board.size(); ++j) {
                existing_val = false;
                is_edge = false;
                if (board[i][j] == 0 || gaia_board[i][j] == 1) { // if the cell is unoccupied or occupied by gaia, mark it as black
                    mat.at<cv::Vec4b>(i, j) = pl_colours[0];
                    continue;
                }
                for (size_t k=8; k>0; k--) {
                    
                    if (static_cast<int>(edge_board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) { // check if the cell is an edge for player k
                        edge_board[i][j] = static_cast<double>(static_cast<int>(edge_board[i][j]) % static_cast<int>(std::pow(2, k-1))); // remove the highest power of 2 to find out if there are multiple players in the cell
                        if (!existing_val) { // if the cell is currently unoccupied, assign it to the player
                            mat.at<cv::Vec4b>(i, j) = pl_colours[k];
                            mat.at<cv::Vec4b>(i, j)[3] = edgeOpacity; // Set the alpha channel for territory opacity
                            existing_val = true;
                            is_edge = true;
                        } else { // if the cell is already occupied, mark it as contested
                            mat.at<cv::Vec4b>(i, j) = pl_colours[4]; // Contested - grey
                            mat.at<cv::Vec4b>(i, j)[3] = edgeOpacity; // Set the alpha channel for territory opacity
                            break; // no need to check further players since it's already contested
                        }
                    }
                    else if (!is_edge && static_cast<int>(board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) {
                        board[i][j] = static_cast<double>(static_cast<int>(board[i][j]) % static_cast<int>(std::pow(2, k-1))); // remove the highest power of 2 to find out if there are multiple players in the cell
                        if (!existing_val) { // if the cell is currently unoccupied, assign it to the player
                            mat.at<cv::Vec4b>(i, j) = pl_colours[k];
                            mat.at<cv::Vec4b>(i, j)[3] = territoryOpacity; // Set the alpha channel for territory opacity
                            existing_val = true;
                        } else { // if the cell is already occupied, mark it as contested
                            mat.at<cv::Vec4b>(i, j) = pl_colours[4]; // Contested - grey
                            mat.at<cv::Vec4b>(i, j)[3] = territoryOpacity; // Set the alpha channel for territory opacity
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


