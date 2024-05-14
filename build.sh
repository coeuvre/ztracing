#!/bin/bash

CC=clang
CFLAGS="-g -fdiagnostics-absolute-paths -Wall -Werror -fsanitize=undefined -fno-omit-frame-pointer"
CFLAGS_DEBUG="-fsanitize=address"
CFLAGS_RELEASE="-O2"
RELEASE=false

for arg in "$@"
do
    case "$arg" in
        --release)
            RELEASE=true
            ;;
    esac
done

if [ "$RELEASE" = true ] ; then
    CFLAGS="$CFLAGS $CFLAGS_RELEASE"
else
    CFLAGS="$CFLAGS $CFLAGS_DEBUG"
fi

mkdir -p build

set -x

$CC $CFLAGS src/main.cpp -o build/ztracing
