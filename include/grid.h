#pragma once


#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>

#include <iomanip>
#include <map>
#include <unordered_map>
#include <tuple>
#include <string>
#include <any>

class Grid {
    private:
        std::vector<std::vector<double>> dataTruth;
        std::vector<std::vector<bool>> dataBool;
        size_t size;

        std::unordered_map <std::string, std::tuple <std::size_t,std::size_t>> building_dict;
        
    public:
        // Constructor to initialize the dynamic 2D array
        Grid(int givenSize) 
            : size(givenSize), dataTruth(givenSize, std::vector<double>(givenSize, 0)), dataBool(givenSize, std::vector<bool>(givenSize, false)) {

            // Generate building_dict AK: -> should be a seperate file that you read into the program on a global level
            building_dict["House"] = std::make_tuple(2,2);
            building_dict["Barracks"] = std::make_tuple(4,3);
            building_dict["Blacksmith"] = std::make_tuple(3,2);
        }

        
    void setValue(size_t i, size_t j, int value);

    int getValue(size_t i, size_t j) const;
    
    std::vector<std::vector<double>> getDataTruth() const;
    std::vector<std::vector<bool>> getDataBool() const;

    void setData(std::vector<std::vector<double>> newData, std::string type = "truth");

    void terrainBoolPass( size_t threshold=4, std::tuple<size_t, size_t, size_t, size_t> bounds = std::make_tuple(0,0,0,0));

};


// helper functions
template <typename T>
std::vector<std::vector<size_t>> findEdges(std::vector<std::vector<T>> board,
                                           const std::vector<std::vector<size_t>>& fillBoard,
                                           std::string mode="fourBox") {

    std::vector<std::vector<size_t>> detEdges(board.size(), std::vector<size_t>(board.size(), 0));

    if (mode=="fourBox") {
        // we check 4 items, ij, the next in the row, the next in the column and the next in the diagonal.
        // if they are all the same, then there is no edge piece. 
        // Otherwise there is an edge piece.

        // Build an effective ownership map that treats fill ownership as territory ownership
        // when the base board has no owner for a cell.
        std::vector<std::vector<size_t>> effective(board.size(), std::vector<size_t>(board.size(), 0));

        for (size_t i = 0; i < board.size(); ++i) {
            for (size_t j = 0; j < board.size(); ++j) {
                size_t boardVal = static_cast<size_t>(board[i][j]);
                if (boardVal != 0) {
                    effective[i][j] = boardVal;
                } else if (!fillBoard.empty() && i < fillBoard.size() && j < fillBoard[i].size()) {
                    effective[i][j] = fillBoard[i][j];
                }
            }
        }

        size_t val;
        int comparison_i, comparison_j;

        for(size_t i=0; i<board.size(); ++i) {
            for(size_t j=0; j<board.size(); ++j) {

                // in the final row and column, we don't want to index outside the array
                if(i==board.size() - 1) {
                    comparison_i = -1;
                } else {comparison_i = 1; }

                if(j==board.size() - 1) {
                    comparison_j = -1;
                } else {comparison_j = 1; }

                size_t centerVal = effective[i][j];
                size_t rowVal = effective[i+comparison_i][j];
                size_t colVal = effective[i][j+comparison_j];
                size_t diagVal = effective[i+comparison_i][j+comparison_j];

                if (centerVal != rowVal || centerVal != colVal || centerVal != diagVal) {
                    // only apply edge pieces to non-empty effective ownership
                    detEdges[i][j] = centerVal;
                    detEdges[i+comparison_i][j] = rowVal;
                    detEdges[i][j+comparison_j] = colVal;
                    detEdges[i+comparison_i][j+comparison_j] = diagVal;
                }
            }
        }
    }
    else if (mode=="Canny") {
        // 2. Apply the Canny edge detector
        // Recommended ratio for thresholds is 2:1 or 3:1
        int lowThreshold = 50;
        int highThreshold = lowThreshold * 3;
        // Canny(board, detEdges, lowThreshold, highThreshold, 3);
    }
    return detEdges;
}

template <typename T>
std::vector<std::vector<size_t>> findEdges(std::vector<std::vector<T>> board, std::string mode="fourBox") {
    return findEdges(board, std::vector<std::vector<size_t>>{}, mode);
}


template <typename T>
void printBoard(std::vector <std::vector<T>> board, size_t precision=1) {
    for (size_t i = 0; i < board.size(); ++i) {
        for (size_t j = 0; j < board.size(); ++j) {
            std::cout << std::setw(3) << std::fixed << std::setprecision(precision) << board[i][j] << " ";
        }
        std::cout << std::endl;
    }
}

