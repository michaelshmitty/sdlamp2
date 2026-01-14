#!/bin/sh

pushd build
gcc -o sdlamp2 -Wall -O0 -ggdb3 ../src/sdlamp2.c `sdl2-config --cflags --libs`
popd
