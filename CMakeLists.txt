cmake_minimum_required(VERSION 3.5)
project( Territory-Analyser )

find_package( OpenCV REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} src/)
add_executable( Territory-Analyser main.cpp src/territory_analyser.cpp src/grid.cpp src/aux.cpp src/fill.cpp )
target_link_libraries( Territory-Analyser ${OpenCV_LIBS} )
target_include_directories( Territory-Analyser PRIVATE "${CMAKE_SOURCE_DIR}/include" )

# --- Tests ---
# gap-seam-test: unit test for the per-fill internal/external gap-seam classification.
add_executable( gap-seam-test tests/test_gap_seam.cpp src/territory_analyser.cpp src/grid.cpp src/aux.cpp src/fill.cpp )
target_link_libraries( gap-seam-test ${OpenCV_LIBS} )
target_include_directories( gap-seam-test PRIVATE "${CMAKE_SOURCE_DIR}/include" )

enable_testing()
add_test( NAME gap-seam-test COMMAND gap-seam-test )

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)