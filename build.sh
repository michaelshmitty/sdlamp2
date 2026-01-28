#!/bin/sh

pushd build 2>&1 >/dev/null

gcc -Wall -Wno-unused -O0 -ggdb3 \
  -o sdlamp2 \
  `sdl2-config --cflags --libs` \
  `pkgconf --cflags --libs libavformat` \
  `pkgconf --cflags --libs libavcodec` \
  `pkgconf --cflags --libs libavutil` \
  `pkgconf --cflags --libs libswresample` \
  -lSDL2_image \
  ../src/sdlamp2.c

popd 2>&1 >/dev/null
