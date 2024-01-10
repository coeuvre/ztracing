@echo off

cmake -S third_party/SDL -B third_party/SDL/build -DCMAKE_INSTALL_PREFIX=third_party/SDL/build/install -A x64 -DSDL_FORCE_STATIC_VCRT=ON -DSDL_SHARED=OFF -DSDL_STATIC=ON
cmake --build third_party/SDL/build --config Release --parallel
cmake --install third_party/SDL/build
