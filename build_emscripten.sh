#!/usr/bin/env bash

. /home/matias/emscripten/emsdk/emsdk_env.sh
emcc main.cpp -o main.html -O2 -sEXPORTED_RUNTIME_METHODS=callMain
