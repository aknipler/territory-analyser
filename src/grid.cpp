#include "grid.h"


void Grid::setValue(size_t i, size_t j, int value) {
    if (i < size && j < size) {
        dataTruth[i][j] = value;
    }
}

int Grid::getValue(size_t i, size_t j) const {
    if (i < size && j < size) {
        return dataTruth[i][j];
    }
    return -1;  
}

std::vector<std::vector<double>> Grid::getDataTruth() const {
    return dataTruth; 
}
std::vector<std::vector<bool>> Grid::getDataBool() const {
    return dataBool; 
}

void Grid::setData(std::vector<std::vector<double>> newData, std::string type) {
    if (type == "truth") {
        for (size_t i = 0; i < newData.size(); ++i) {
            for (size_t j = 0; j < newData.size(); ++j) {
                dataTruth[i][j] = newData[i][j];
            }
        }
    } else if (type == "bool") {
        for (size_t i = 0; i < newData.size(); ++i) {
            for (size_t j = 0; j < newData.size(); ++j) {
                dataBool[i][j] = static_cast<bool>(newData[i][j]);
            }
        }
    }
}




void Grid::terrainBoolPass( size_t threshold, std::tuple<size_t, size_t, size_t, size_t> bounds) {
    // takes a board and transforms it to boolean based on a threshold
    // assumes that board is square

    // check if bounds is the default value, if so set it to cover the whole board, otherwise use the provided bounds
    size_t minRow, maxRow, minCol, maxCol;
    if (bounds == std::make_tuple(0,0,0,0)) {
        minRow = 0;
        maxRow = dataTruth.size();
        minCol = 0;
        maxCol = dataTruth.size();
    } else {
        std::tie(minRow, maxRow, minCol, maxCol) = bounds;
    }

    for (size_t i = minRow; i < maxRow; ++i) {
        for (size_t j = minCol; j < maxCol; ++j) {
            dataBool[i][j] = (dataTruth[i][j] < threshold) ? 0 : 1;
        }
    }
    
};


