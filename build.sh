#!/bin/bash

BUILD_DIR=build
BUILD_WEB=false
CONFIGURE_PREFIX=
RELEASE=false
CMAKE_CONFIG=Debug
CMAKE_GENERATOR="Ninja"

for arg in "$@"
do
    case "$arg" in
        --release)
            RELEASE=true
            ;;
        --web)
            BUILD_WEB=true
            ;;
    esac
done

if [ "$RELEASE" = true ] ; then
    CMAKE_CONFIG=Release
fi

if [ "$BUILD_WEB" = true ] ; then
    BUILD_DIR=build_web
    CONFIGURE_PREFIX=emcmake
fi

set -o xtrace

$CONFIGURE_PREFIX cmake -S . -B $BUILD_DIR -G "$CMAKE_GENERATOR" -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=$CMAKE_CONFIG && \
    cmake --build $BUILD_DIR --config "$CMAKE_CONFIG" --verbose --parallel
