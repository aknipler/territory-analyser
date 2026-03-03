#include "aux/header.h"
// #include <opencv2/opencv.hpp>


int main() {
    // Setup Boards for Testing
    size_t size = 20, threshold = 3;
    Grid p1Board(size), p2Board(size); 

    // place some 'buildings'
    p1Board.addBuilding(4,4,"Barracks");
    p1Board.addBuilding(7,6,"House");
    p1Board.addBuilding(9,11,"Blacksmith");
    printBoard(p1Board.getDataTruth());
    std::cout << std::endl;

    printBoard(p1Board.getDataBool());
    std::cout << std::endl;

    // Find edges
    printBoard(findEdges(p1Board.getDataBool(), "threeBox"),0);

    return 0;

}