#!/bin/sh

cmake -S third_party/SDL -B third_party/SDL/build -G Ninja -DCMAKE_INSTALL_PREFIX=third_party/SDL/build/install -DSDL_SHARED=OFF -DSDL_STATIC=ON
cmake --build third_party/SDL/build --config Release --parallel
cmake --install third_party/SDL/build
