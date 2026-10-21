cmake_minimum_required(VERSION 3.21)
project(spatialjoin-library-example LANGUAGES CXX)

find_package(spatialjoin CONFIG REQUIRED)

add_executable(spatialjoin-library-example main.cpp)
target_link_libraries(spatialjoin-library-example PRIVATE spatialjoin::spatialjoin)
