cmake_minimum_required(VERSION 3.15)

project(ztracing CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (EMSCRIPTEN)
    add_compile_options(
        -pthread
        -sUSE_ZLIB=1
        -sUSE_SDL=2)

    add_link_options(
        -pthread
        -sPTHREAD_POOL_SIZE=1
        -sUSE_ZLIB=1
        -sUSE_SDL=2
        -sMODULARIZE=1
        -sEXPORT_ES6
        -sENVIRONMENT=web,worker
        -sEXPORTED_RUNTIME_METHODS=cwrap
        -sALLOW_BLOCKING_ON_MAIN_THREAD=1
        -sALLOW_MEMORY_GROWTH
        -lworkerfs.js)
else()
    set(SDL_SHARED OFF CACHE BOOL "Build a shared version of the SDL2")
    set(SDL_STATIC_PIC ON CACHE BOOL "Static version of SDL2 should be built with Position Independent Code")
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

SET(ZTRACING_SRCS
    src/app.cpp
    src/channel.cpp
    src/document.cpp
    src/json.cpp
    src/memory.cpp
    src/os.cpp
    src/task.cpp)

if (EMSCRIPTEN)
    SET(ZTRACING_SRCS ${ZTRACING_SRCS} src/os_emscripten.cpp)
else()
    SET(ZTRACING_SRCS ${ZTRACING_SRCS} src/os_native.cpp)
endif()

add_executable(ztracing ${ZTRACING_SRCS})

target_link_libraries(
    ztracing
    imgui
    imgui_impl_sdl2)

if (NOT EMSCRIPTEN)
    target_link_libraries(imgui_impl_sdl2 SDL2-static)
    target_link_libraries(ztracing zlibstatic SDL2-static)
endif()
