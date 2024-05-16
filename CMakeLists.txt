cmake_minimum_required(VERSION 3.15)

project(ztracing CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDL_SHARED OFF)
add_subdirectory(third_party/SDL2)

add_subdirectory(third_party/zlib)

add_library(
    imgui
    STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_widgets.cpp)

add_library(
    imgui_impl_sdl2
    STATIC
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp)

target_include_directories(
    imgui_impl_sdl2
    PUBLIC
    third_party/imgui
    third_party/imgui/backends)

target_link_libraries(
    imgui_impl_sdl2
    SDL2-static)

add_executable(
    ztracing
    src/main.cpp)

target_link_libraries(
    ztracing
    SDL2-static
    imgui
    imgui_impl_sdl2
    zlibstatic)
