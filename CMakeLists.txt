cmake_minimum_required(VERSION 3.15)

project(ztracing CXX)

add_subdirectory(third_party/SDL2)

add_executable(
    ztracing
    src/main.cpp)
target_link_libraries(
    ztracing
    SDL2-static)
