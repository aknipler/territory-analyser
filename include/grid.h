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
#include <type_traits>

class Grid {
    private:
        std::vector<std::vector<double>> dataTruth;
        std::vector<std::vector<bool>> dataBool;
        size_t size;

        std::unordered_map <std::string, std::tuple <std::size_t,std::size_t>> buildingDict;
        
    public:
        // Constructor to initialize the dynamic 2D array
        Grid(int givenSize) 
            : size(givenSize), dataTruth(givenSize, std::vector<double>(givenSize, 0)), dataBool(givenSize, std::vector<bool>(givenSize, false)) {

            // Generate buildingDict AK: -> should be a seperate file that you read into the program on a global level
            buildingDict["House"] = std::make_tuple(2,2);
            buildingDict["Barracks"] = std::make_tuple(4,3);
            buildingDict["Blacksmith"] = std::make_tuple(3,2);
        }

        
    void setValue(size_t i, size_t j, double value);

    double getValue(size_t i, size_t j) const;
    
    std::vector<std::vector<double>> getDataTruth() const;
    std::vector<std::vector<bool>> getDataBool() const;

    void setData(std::vector<std::vector<double>> newData, std::string type = "truth");

    /** @brief Converts dataTruth to dataBool within bounds: a cell is true when its truth value >= threshold.
     *         If bounds is the zero-tuple, the entire board is recomputed. */
    void terrainBoolPass( size_t threshold=4, std::tuple<size_t, size_t, size_t, size_t> bounds = std::make_tuple(0,0,0,0));

};


// helper functions
/**
 * @brief Computes a cell-ownership edge board: a cell is included when its 2x2 neighbourhood
 *        contains more than one distinct owner value.  fillBoard (optional) is used as a fallback
 *        ownership source for cells where board has no owner.  Only the "fourBox" mode is implemented.
 */
template <typename T>
std::vector<std::vector<size_t>> findEdges(const std::vector<std::vector<T>>& board,
                                           const std::vector<std::vector<size_t>>* fillBoard=nullptr,
                                           std::string mode="fourBox") {

    std::vector<std::vector<size_t>> detEdges(board.size(), std::vector<size_t>(board.size(), 0));

    if (mode=="fourBox") {
        // Check 4 items: ij, the next in y, the next in x, and the next in the diagonal.
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
                } else if (fillBoard && i < fillBoard->size() && j < (*fillBoard)[i].size()) {
                    effective[i][j] = (*fillBoard)[i][j];
                }
            }
        }

        size_t val;
        int comparison_i, comparison_j;

        for(size_t i=0; i<board.size(); ++i) {
            for(size_t j=0; j<board.size(); ++j) {

                // At the final y/x edge, avoid indexing outside the array.
                if(i==board.size() - 1) {
                    comparison_i = -1;
                } else {comparison_i = 1; }

                if(j==board.size() - 1) {
                    comparison_j = -1;
                } else {comparison_j = 1; }

                size_t centerVal = effective[i][j];
                size_t yVal = effective[i+comparison_i][j];
                size_t xVal = effective[i][j+comparison_j];
                size_t diagVal = effective[i+comparison_i][j+comparison_j];

                if (centerVal != yVal || centerVal != xVal || centerVal != diagVal) {
                    // only apply edge pieces to non-empty effective ownership
                    detEdges[i][j] = centerVal;
                    detEdges[i+comparison_i][j] = yVal;
                    detEdges[i][j+comparison_j] = xVal;
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
std::vector<std::vector<size_t>> findEdges(
    const std::vector<std::vector<T>>& board,
    const std::vector<std::vector<size_t>>& fillBoard,   // binds to the temporary
    std::string mode = "fourBox") {
    return findEdges(board, &fillBoard, mode);            // passes pointer to main impl
}

template <typename T>
std::vector<std::vector<size_t>> findEdges(const std::vector<std::vector<T>>& board, std::string mode) {
    return findEdges(board, nullptr, mode);
}


template <typename T>
void printBoard(const std::vector<std::vector<T>>& board, size_t precision=1, bool skipNegativeOnes=false) {
    if (board.empty()) {
        std::cout << "(empty board)" << std::endl;
        return;
    }

    const size_t yCount = board.size();
    size_t xCount = 0;
    for (const auto& yValues : board) {
        xCount = std::max(xCount, yValues.size());
    }

    // Print x headers using the same width pattern as cell values.
    std::cout << std::setw(3) << " " << "   ";
    for (size_t i = 0; i < xCount; ++i) {
        std::cout << "-" << std::setw(2) << i << "-";
    }
    std::cout << std::endl;

    for (size_t i = 0; i < yCount; ++i) {
        // Print y lines with y headers.
        std::cout << std::setw(3) << std::fixed << std::setprecision(precision) << i << " | ";

        // Print values along x.
        for (size_t j = 0; j < xCount; ++j) {
            if (j >= board[i].size()) {
                std::cout << std::setw(3) << " " << " ";
                continue;
            }

            bool isNegativeOne = false;
            if constexpr (std::is_arithmetic_v<T>) {
                isNegativeOne = (board[i][j] == static_cast<T>(-1));
            }

            if (skipNegativeOnes && isNegativeOne) {
                std::cout << std::setw(3) << " " << " ";
            } else {
                std::cout << std::setw(3) << std::fixed << std::setprecision(precision) << board[i][j] << " ";
            }
        }

        // End of y line.
        std::cout << std::setw(3) << std::fixed << std::setprecision(precision) << " | " << i << std::endl;
    }
}

