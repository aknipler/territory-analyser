#include "grid.h"


void Grid::setValue(size_t i, size_t j, double value) {
    if (i < size && j < size) {
        dataTruth[i][j] = value;
    }
}

double Grid::getValue(size_t i, size_t j) const {
    if (i < size && j < size) {
        return dataTruth[i][j];
    }
    return -1.0;  
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
            for (size_t j = 0; j < newData[i].size(); ++j) {
                dataTruth[i][j] = newData[i][j];
            }
        }
    } else if (type == "bool") {
        for (size_t i = 0; i < newData.size(); ++i) {
            for (size_t j = 0; j < newData[i].size(); ++j) {
                dataBool[i][j] = static_cast<bool>(newData[i][j]);
            }
        }
    }
}




// ---------- terrainBoolPass ----------

/**
 * @brief Converts dataTruth to dataBool within the given bounds: a cell is true when its truth
 *        value meets or exceeds threshold.  If bounds is the zero-tuple, the entire board is updated.
 */
void Grid::terrainBoolPass( size_t threshold, std::tuple<size_t, size_t, size_t, size_t> bounds) {
    // takes a board and transforms it to boolean based on a threshold
    // assumes that board is square

    // check if bounds is the default value, if so set it to cover the whole board, otherwise use the provided bounds
    size_t minX, maxX, minY, maxY;
    if (bounds == std::make_tuple(0,0,0,0)) {
        minX = 0;
        maxX = dataTruth.size();
        minY = 0;
        maxY = dataTruth.size();
    } else {
        std::tie(minX, maxX, minY, maxY) = bounds;
    }

    for (size_t i = minX; i < maxX; ++i) {
        for (size_t j = minY; j < maxY; ++j) {
            dataBool[i][j] = (dataTruth[i][j] < threshold) ? 0 : 1;
        }
    }
    
};


