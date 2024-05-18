cmake_minimum_required(VERSION 3.15)

project(ztracing CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EMSCRIPTEN)
    add_compile_options(
        -Wno-c99-designator
        -pthread
        -sUSE_ZLIB=1
        -sUSE_SDL=2)

    add_link_options(
        -pthread
        -sPTHREAD_POOL_SIZE=2
        -sUSE_ZLIB=1
        -sUSE_SDL=2
        -sEXPORT_ES6
        -sEXPORTED_RUNTIME_METHODS=cwrap,FS,WORKERFS
        -sALLOW_BLOCKING_ON_MAIN_THREAD=1
        -sALLOW_MEMORY_GROWTH
        -lworkerfs.js)
else()
    set(SDL_SHARED OFF CACHE BOOL "Build a shared version of the SDL2")
    add_subdirectory(third_party/SDL2)
    add_subdirectory(third_party/zlib)
endif()

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

add_executable(
    ztracing
    src/main.cpp)

target_link_libraries(
    ztracing
    imgui
    imgui_impl_sdl2)

if (NOT EMSCRIPTEN)
    target_link_libraries(imgui_impl_sdl2 SDL2-static)
    target_link_libraries(ztracing zlibstatic SDL2-static)
endif()
