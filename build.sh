#!/bin/sh

pushd build

gcc -Wall -O0 -ggdb3 \
  -o sdlamp2 \
  `sdl2-config --cflags --libs` \
  -lSDL2_image \
  ../src/sdlamp2.c

popd
