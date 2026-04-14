

#include "../include/territory_analyser.h"
// #include "grid.h"

// Constructor to initialize the dynamic 2D array
TerritoryAnalyser::TerritoryAnalyser(int givenSize, int numPlayers, int numTeams, std::map<int, int> teamAssignments, size_t threshold) 
    : size(givenSize), numPlayers(numPlayers), numTeams(numTeams), teamAssignments(teamAssignments), threshold(threshold), gaia_board(givenSize, std::vector<size_t>(givenSize, 0)),
    masterPlayerBoard(givenSize, std::vector<double>(givenSize, 0)), masterPlayerBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), masterPlayerBoardFill(givenSize, std::vector<size_t>(givenSize, 0)), 
    masterTeamBoard(givenSize, std::vector<double>(givenSize, 0)), masterTeamBoardEdges(givenSize, std::vector<size_t>(givenSize, 0)), masterTeamBoardFill(givenSize, std::vector<size_t>(givenSize, 0)),
    obstructionsBoard(givenSize, std::vector<bool>(givenSize, false)) {

    if (teamAssignments.size() != numPlayers) {
        std::cerr << "Error: teamAssignments size must match numPlayers." << std::endl;
        exit(1);
    }
    // Generate building_dict AK: -> should be a seperate file that you read into the program on a global level
    // radius, weighting, soft edge, width, height
    building_dict["House"]      = {0, 3, 2, 2, 2};
    building_dict["Barracks"]   = {2, 3, 3, 3, 3};
    building_dict["Blacksmith"] = {1, 3, 1, 3, 3};
    building_dict["Tree"]       = {0, 1, 0, 1, 1};
    building_dict["Gold Mine"]  = {0, 1, 0, 1, 1};
    building_dict["Cliff"]      = {0, 1, 0, 1, 1};

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

    size_t minRow, maxRow, minCol, maxCol;
    // AK: need to implement a try and except i.e. getRWeight() function or something
    BuildingInfo info;
    try {
        info = building_dict.at(building);
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: Building type '" << building << "' not found in building_dict." << std::endl;
        // Can set to automatically use a default value for r, such as the dark age vision of the building.
        return;
    }

    size_t r = info.influenceRadius;
    size_t weight = info.influenceWeight;
    size_t soft_edge = info.influenceSoftExpansion;
    size_t bw = info.width;
    size_t bh = info.height;

    // updates territory to both player and team grids
    if (mod == "add") {
        weight = weight;
    } else if (mod == "remove") {
        weight = -weight;
    } else {
        std::cerr << "Error: Invalid modification type specified. Use 'add' or 'remove'." << std::endl;
        return;
    }
    std::tuple bounds = updateTerritory(x,y,r,player,weight,soft_edge,bw,bh);

    // Update obstructions board for the building footprint
    for (size_t row = x; row < x + bw && row < size; ++row) {
        for (size_t col = y; col < y + bh && col < size; ++col) {
            obstructionsBoard[row][col] = (mod == "add");
        }
    }
    // std::cout << "Obstructions board after update:" << std::endl;
    // printBoard(obstructionsBoard); // For testing
    // std::cout << std::endl;

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

    // Set fill boards: currently using board edges, but will have to update to use buildings as obstructions. AK
    auto playerFillResult = analyseFill(this->getMasterBoard("player"), obstructionsBoard, static_cast<size_t>(numPlayers));
    this->playerGaps = playerFillResult.gaps;
    this->masterPlayerBoardFill = playerFillResult.fillBoard;

    auto teamFillResult = analyseFill(this->getMasterBoard("team"), obstructionsBoard, static_cast<size_t>(numTeams));
    this->teamGaps = teamFillResult.gaps;
    this->masterTeamBoardFill = teamFillResult.fillBoard;

   


}


std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> TerritoryAnalyser::updateTerritory(size_t centerX, size_t centerY, size_t r, size_t player, size_t weighting, size_t soft_edge, size_t bw, size_t bh) {
    // adds territory influence around a rectangular building footprint
    // footprint spans [centerX, centerX+bh) x [centerY, centerY+bw)
    // influence radius r extends outward from the footprint edges
    
    size_t x, y;
    double dist;
    
    // Rectangle bounds (inclusive max edge for distance calc)
    double rectMinX = static_cast<double>(centerX);
    double rectMaxX = static_cast<double>(centerX + bh - 1);
    double rectMinY = static_cast<double>(centerY);
    double rectMaxY = static_cast<double>(centerY + bw - 1);
    
    // Determine the bounding box (rectangle + radius + soft_edge) to avoid checking the entire grid
    size_t minRow = std::max(0, static_cast<int>(centerX) - static_cast<int>(r) - static_cast<int>(soft_edge)),
           maxRow = std::min(static_cast<int>(size), static_cast<int>(centerX + bh + r + soft_edge)),
           minCol = std::max(0, static_cast<int>(centerY) - static_cast<int>(r) - static_cast<int>(soft_edge)),
           maxCol = std::min(static_cast<int>(size), static_cast<int>(centerY + bw + r + soft_edge));

    // if player is Gaia
    if (player == 0) {
        for (x = centerX; x < centerX + bh && x < size; ++x) {
            for (y = centerY; y < centerY + bw && y < size; ++y) {
                gaia_board[x][y] += sgn(weighting);
            }
        }
    } else {
        // iterate rows then cols
        for (x = minRow; x < maxRow; ++x) {
            // distance from cell to nearest edge of rectangle along each axis
            double dx = std::max(0.0, std::max(rectMinX - static_cast<double>(x), static_cast<double>(x) - rectMaxX));

            for (y = minCol; y < maxCol; ++y) {
                double dy = std::max(0.0, std::max(rectMinY - static_cast<double>(y), static_cast<double>(y) - rectMaxY));
                dist = std::sqrt(dx * dx + dy * dy);

                // update the player and team grids with the new territory values
                double value = (dist > r) ? std::max(0.0, (r + soft_edge + 1.0 - dist)/(soft_edge+1)*static_cast<double>(threshold)) : static_cast<double>(weighting);
                playerGrids.at(player).setValue(x, y, playerGrids.at(player).getValue(x, y) + value);
                teamGrids.at(teamAssignments.at(player)).setValue(x, y, teamGrids.at(teamAssignments.at(player)).getValue(x, y) + value);
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


    size_t k=0;
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
        bool existing_val = false, is_edge = false, is_building_territory = false;

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
                is_building_territory = false;
// ************** 1. GAIA: If the cell is unoccupied or occupied by gaia, skip **************** //
                if (gaia_board[i][j] == 1 || (fill_board[i][j] == 0 && edge_board[i][j] == 0 && board[i][j] == 0)) { 
                    mat.at<cv::Vec4b>(i, j) = pl_colours[0];
                    continue;
                }

// loop through all the players in reverse order
                for (size_t k=8; k>0; k--) {

// ************** 2. EDGES **************** // 

                    if (static_cast<int>(edge_board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) { // check if the cell is an edge for player k

                        // remove the highest power of 2 to find out if there are multiple players in the cell
                        edge_board[i][j] = static_cast<double>(static_cast<int>(edge_board[i][j]) % static_cast<int>(std::pow(2, k-1))); 

                        // if the cell is currently unoccupied, assign it to the player
                        if (!existing_val) { 
                            mat.at<cv::Vec4b>(i, j) = pl_colours[k];
                            mat.at<cv::Vec4b>(i, j)[3] = edgeOpacity; 
                            existing_val = true;
                            is_edge = true;

                        // if the cell is already occupied, mark it as contested
                        } else { 
                            mat.at<cv::Vec4b>(i, j) = pl_colours[4]; 
                            mat.at<cv::Vec4b>(i, j)[3] = edgeOpacity;
                            // no need to check further players since it's already contested 
                            break; 
                        }
                    }
                }
                
// loop through all the players in reverse order - must be separate from edges or there are 3+ player situations 
// where the cell might be marked as contested territory before it gets to the edge player
                if(!is_edge) {
                    for (size_t k=8; k>0; k--) {

    // ************** 3. TERRITORY FROM BUILDINGS **************** // 

                        if (static_cast<int>(board[i][j])/static_cast<int>(std::pow(2, k-1)) >= 1) {

                            // remove the highest power of 2 to find out if there are multiple players in the cell
                            board[i][j] = static_cast<double>(static_cast<int>(board[i][j]) % static_cast<int>(std::pow(2, k-1))); 
                            
                            // if the cell is currently unoccupied, assign it to the player
                            if (!existing_val) { 
                                mat.at<cv::Vec4b>(i, j) = pl_colours[k];
                                mat.at<cv::Vec4b>(i, j)[3] = territoryOpacity; 
                                existing_val = true;
                                is_building_territory = true;

                            // if the cell is already occupied, mark it as contested
                            } else { 
                                mat.at<cv::Vec4b>(i, j) = pl_colours[4]; 
                                mat.at<cv::Vec4b>(i, j)[3] = territoryOpacity; 
                                // no need to check further players since it's already contested
                                break; 
                            }
                        }
                    }
                }
// ************** 4. TERRITORY FROM FILL **************** // 
                if(!is_edge && !is_building_territory) { 

                    if (fill_board[i][j] == 0) { 
                        continue;
                    } else {
                        k = fill_board[i][j];
                        mat.at<cv::Vec4b>(i, j) = pl_colours[4]; // this should be k, not 4
                        mat.at<cv::Vec4b>(i, j)[3] = static_cast<int>(territoryOpacity*2); 
                        existing_val = true;
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


