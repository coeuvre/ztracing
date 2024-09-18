#!/bin/bash
set -eu
cd "$(dirname "$0")"

# --- Unpack Arguments --------------------------------------------------------
flag_release=
has_target=
target_ztracing=

for arg in "$@"
do
    if [[ $arg = "--"* ]]; then
        declare "flag_${arg:2}"="1"
    else
        declare "target_$arg"="1"
    fi
done

if [ $target_ztracing ]; then has_target='1'; fi

# --- Compile/Link Line Definitions -------------------------------------------
clang_common='-I../src/ -fdiagnostics-absolute-paths -Wall'
clang_debug="clang -g -O0 -DBUILD_DEBUG=1 ${clang_common}"
clang_release="clang -g -O2 -DBUILD_DEBUG=0 ${clang_common}"
clang_link=""

# --- Per-Build Settings ------------------------------------------------------

# --- Choose Compile/Link Lines -----------------------------------------------
compile_debug=$clang_debug
compile_release=$clang_release
compile_link=$clang_link

if [ $flag_release ]; then compile=$compile_release && echo "[release mode]"; else compile=$compile_debug && echo "[debug mdoe]"; fi

# --- Prep Directories --------------------------------------------------------
mkdir -p build

# --- Build Everything (@build_targets) ---------------------------------------
cd build

if [ ! $has_target ] || [ $target_ztracing ]; then (set -x; $compile ../src/ztracing/ztracing_main.c $compile_link -o ztracing); fi

cd ..