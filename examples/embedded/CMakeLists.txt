cmake_minimum_required(VERSION 3.21)
project(spatialjoin-embedded-example LANGUAGES CXX)

add_subdirectory(../.. spatialjoin)

add_executable(spatialjoin-embedded-example ../library/main.cpp)
target_link_libraries(spatialjoin-embedded-example PRIVATE spatialjoin::spatialjoin)
