#!/bin/bash

RELEASE=false
CMAKE_CONFIG=Debug
CMAKE_GENERATOR="Ninja Multi-Config"

for arg in "$@"
do
    case "$arg" in
        --release)
            RELEASE=true
            ;;
    esac
done

if [ "$RELEASE" = true ] ; then
    CMAKE_CONFIG=Release
fi

cmake -S . -B build -G "$CMAKE_GENERATOR" && \
    cmake --build build --config "$CMAKE_CONFIG" --verbose --parallel
