#!/bin/sh

rm -rf dist && \
  zig build -Dtarget=wasm32-wasi -Doptimize=ReleaseSafe && \
  npm run build