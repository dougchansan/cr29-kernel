cmake_minimum_required(VERSION 3.20)
project(cr29_rdna4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCL
find_package(OpenCL REQUIRED)

# Main executable
add_executable(cr29_miner
    src/cr29_miner.cpp
)

target_include_directories(cr29_miner PRIVATE
    ${OpenCL_INCLUDE_DIRS}
)

target_link_libraries(cr29_miner PRIVATE
    ${OpenCL_LIBRARIES}
)

# Copy kernel files to build directory
configure_file(src/siphash.cl ${CMAKE_BINARY_DIR}/src/siphash.cl COPYONLY)
configure_file(src/trimmer.cl ${CMAKE_BINARY_DIR}/src/trimmer.cl COPYONLY)

# Installation
install(TARGETS cr29_miner DESTINATION bin)
install(FILES src/siphash.cl src/trimmer.cl DESTINATION bin/src)
