cmake_minimum_required(VERSION 3.20)
project(xtm_sha3x_miner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCL
find_package(OpenCL REQUIRED)

# SHA3X Pool Miner executable
add_executable(sha3x_miner
    src/sha3x_pool_miner.cpp
)

target_include_directories(sha3x_miner PRIVATE
    ${OpenCL_INCLUDE_DIRS}
    src/
)

target_link_libraries(sha3x_miner PRIVATE
    ${OpenCL_LIBRARIES}
)

# Legacy CR29 executables (keep for reference)
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
configure_file(src/sha3x_kernel.cl ${CMAKE_BINARY_DIR}/src/sha3x_kernel.cl COPYONLY)
configure_file(src/siphash.cl ${CMAKE_BINARY_DIR}/src/siphash.cl COPYONLY)
configure_file(src/trimmer.cl ${CMAKE_BINARY_DIR}/src/trimmer.cl COPYONLY)

# Installation
install(TARGETS sha3x_miner DESTINATION bin)
install(TARGETS cr29_miner DESTINATION bin)
install(FILES src/sha3x_kernel.cl src/siphash.cl src/trimmer.cl DESTINATION bin/src)
