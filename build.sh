#!/bin/bash

set -eu

ROOT=$(realpath $(dirname "$0"))
BUILD_DIR="$ROOT/build"

mkdir -p $BUILD_DIR
cd $BUILD_DIR

# TODO: Add flags for release build
COMMON_CFLAGS="
  -std=c17 -g3 -Wall -Wextra
  -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion
  -fsanitize=undefined
  -I$ROOT
"

# TODO: -Wconversion -Wdouble-promotion

case "$OSTYPE" in
linux*) COMMON_CFLAGS="$COMMON_CFLAGS -lm" ;;
esac

COMMON_SOURCES="
  $ROOT/src/channel.c
  $ROOT/src/flick.c
  $ROOT/src/json_trace_profile.c
  $ROOT/src/json.c
  $ROOT/src/string.c
  $ROOT/src/ztracing.c
"

# Build Native
# ------------------------------------------------------------------------------
NATIVE_SOURCES="
  $ROOT/src/canvas_sdl3.c
  $ROOT/src/log_sdl3.c
  $ROOT/src/platform_sdl3.c
  $ROOT/src/ztracing_sdl3.c
"

NATIVE_CFLAGS="-fsanitize-trap"

case "$OSTYPE" in
darwin*)
  NATIVE_CFLAGS="$NATIVE_CFLAGS -F$ROOT/third_party/SDL3/macos -framework SDL3 -rpath $ROOT/third_party/SDL3/macos"
  ;;
*)
  cp $ROOT/third_party/SDL3/windows/lib/SDL3.dll $BUILD_DIR/
  NATIVE_CFLAGS="$NATIVE_CFLAGS -I$ROOT/third_party/SDL3/windows/include -L$ROOT/third_party/SDL3/windows/lib -lSDL3"
  ;;
esac

clang $COMMON_CFLAGS $NATIVE_CFLAGS $COMMON_SOURCES $NATIVE_SOURCES -o $BUILD_DIR/ztracing.exe

# Build Web
# ------------------------------------------------------------------------------
mkdir -p $BUILD_DIR/web

WEB_SOURCES="
  $ROOT/src/flick_web.c
  $ROOT/src/platform_web.c
"

emcc -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4 \
  -sEXPORTED_FUNCTIONS=_malloc,_free,_main \
  --js-library $ROOT/src/flick.js \
  --js-library $ROOT/src/ztracing.js \
  $COMMON_CFLAGS $COMMON_SOURCES $WEB_SOURCES \
  -o $BUILD_DIR/web/ztracing.js

cp $ROOT/index.js $ROOT/index.html $ROOT/public/enable-threads.js $BUILD_DIR/web/
