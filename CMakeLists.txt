cmake_minimum_required(VERSION 3.5)
project( Territory-Analyser )

find_package( OpenCV REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} src/)
add_executable( Territory-Analyser main.cpp src/territory_analyser.cpp src/grid.cpp src/aux.cpp )
target_link_libraries( Territory-Analyser ${OpenCV_LIBS} )
target_include_directories( Territory-Analyser PRIVATE "${CMAKE_SOURCE_DIR}/include" )

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)