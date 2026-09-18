# Set the minimum version of CMake that can be used
# To find the cmake version run
# $ cmake --version
cmake_minimum_required(VERSION 3.16)

# Set the project name
project(HALS)

# Add sub directories
add_subdirectory(abc)
add_subdirectory(espresso)
add_subdirectory(resubals)
