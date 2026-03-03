#include "grid.h"

class Grid {
    private:
        std::vector<std::vector<double>> dataTruth;
        std::vector<std::vector<bool>> dataBool;
        size_t size;

        std::unordered_map <std::string, std::tuple <std::size_t,std::size_t>> building_dict;
        
    public:
        // Constructor to initialize the dynamic 2D array
        Grid(int givenSize) 
            : size(givenSize), dataTruth(givenSize, std::vector<double>(givenSize)), dataBool(givenSize, std::vector<bool>(givenSize)) {

            // Generate building_dict AK: -> should be a seperate file that you read into the program on a global level
            building_dict["House"] = std::make_tuple(2,2);
            building_dict["Barracks"] = std::make_tuple(3,3);
            building_dict["Blacksmith"] = std::make_tuple(3,2);

            for (size_t i = 0; i < size; ++i) {
                for (size_t j = 0; j < size; ++j) {
                    dataTruth[i][j] = 0;
                }
            }
        }

        
    void setValue(size_t i, size_t j, int value) {
        if (i < size && j < size) {
            dataTruth[i][j] = value;
        }
    }

    int getValue(size_t i, size_t j) const {
        if (i < size && j < size) {
            return dataTruth[i][j];
        }
        return -1;  
    }
    
    std::vector<std::vector<double>> getDataTruth() const {
        return dataTruth; 
    }
    std::vector<std::vector<bool>> getDataBool() const {
        return dataBool; 
    }

    void setData(std::vector<std::vector<double>> newData) {
        for (size_t i = 0; i < newData.size(); ++i) {
            for (size_t j = 0; j < newData.size(); ++j) {
                dataTruth[i][j] = newData[i][j];
            }
        }
    }


    void addBoard(std::vector<std::vector<double>> extBoard) {

        for (size_t i = 0; i < size; ++i) {
            for (size_t j = 0; j < size; ++j) {
                dataTruth[i][j] = dataTruth[i][j] + extBoard[i][j];
            }
        }
    }

    void addBuilding(size_t x, size_t y, std::string building) {

        size_t r, weight, minRow, maxRow, minCol, maxCol;
        // AK: need to implement a try and except i.e. getRWeight() function or something
        std::tie(r,weight) = building_dict[building];
        std::tie(minRow,maxRow,minCol,maxCol) = addTerritory(x,y,r,weight);
        dataBool = terrainBoolPass(3);
    }

    std::tuple <std::size_t,std::size_t,std::size_t,std::size_t> addTerritory(size_t centerX, size_t centerY, size_t r, size_t weighting=5) {
        // adds a filled circle of values centered on coordinates x,y to the dataTruth array
        
        size_t x, y, d, threshold, radiusSq;
        double xDiff, yDiff, dist;
        
        r = r+4; // +4 to give merging territory

        // Determine the bounding box to avoid checking the entire grid
        int minRow = std::max(0, static_cast<int>(centerX - r));
        int maxRow = std::min(static_cast<int>(size - 1), static_cast<int>(centerX + r + 1));
        int minCol = std::max(0, static_cast<int>(centerY - r));
        int maxCol = std::min(static_cast<int>(size - 1), static_cast<int>(centerY + r + 1));

        d = (r * 2) + 1;
        radiusSq = (d * d) / 4;
        threshold = weighting;
                
        for(y = minCol; y < maxCol; y++)
        {
            yDiff = double(y) - double(centerY);
            for(x = minRow; x < maxRow; x++)
            {
                xDiff = double(x) - double(centerX);
                dist = std::sqrt((xDiff * xDiff) + (yDiff * yDiff));
                
                dataTruth[y][x] = dataTruth[y][x] + ((dist > r) ? 0 : std::min(double(r-dist),double(weighting)));
            }
        }

        return  std::make_tuple(minRow, maxRow, minCol, maxCol);
    }

    
    std::vector<std::vector<bool>> terrainBoolPass( size_t threshold=4) {
        // takes a board and transforms it to boolean based on a threshold
        // assumes that board is square

        std::vector<std::vector<bool>> boolBoard(dataTruth.size(), std::vector<bool>(dataTruth.size(), 0));
        for (size_t i = 0; i < dataTruth.size(); ++i) {
            for (size_t j = 0; j < dataTruth.size(); ++j) {
                boolBoard[i][j] = (dataTruth[i][j] < threshold) ? 0 : 1;
            }
        }
        return boolBoard;
    };

};



// helper functions


std::vector<std::vector<bool>> findEdges(std::vector<std::vector<bool>> board, std::string mode="Canny") {

    std::vector<std::vector<bool>> detEdges(board.size(), std::vector<bool>(board.size(), 0));

    if (mode=="threeBox") {
        // we check 3 items, ij, the next in the row and the next in the column.
        // if they are all 0, they will add to 0. If they are all 1 they will add to 3.
        // otherwise, they are not all the same, and there is an edge piece.
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

                val = board[i][j] + board[i+comparison_i][j] + board[i][j+comparison_j]; 
                if (val > 0 && val < 3) {
                    // only apply edge pieces to the territory -> exclude 0s
                    detEdges[i][j] = board[i][j];
                    detEdges[i+comparison_i][j] = board[i+comparison_i][j];
                    detEdges[i][j+comparison_j] = board[i][j+comparison_j];
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
void printBoard(std::vector <std::vector<T>> board, size_t precision=1) {
    for (size_t i = 0; i < board.size(); ++i) {
        for (size_t j = 0; j < board.size(); ++j) {
            std::cout << std::setw(3) << std::fixed << std::setprecision(precision) << board[i][j] << " ";
        }
        std::cout << std::endl;
    }
}