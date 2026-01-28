#!/bin/sh

pushd build 2>&1 >/dev/null

gcc -Wall -Wno-unused \
  -o sdlamp2 \
  `sdl2-config --cflags --libs` \
  -I/usr/include/aarch64-linux-gnu -L/usr/lib/aarch64-linux-gnu -lavformat \
  -I/usr/include/aarch64-linux-gnu -L/usr/lib/aarch64-linux-gnu -lavcodec \
  -I/usr/include/aarch64-linux-gnu -L/usr/lib/aarch64-linux-gnu -lavutil \
  -I/usr/include/aarch64-linux-gnu -L/usr/lib/aarch64-linux-gnu -lswresample \
  -lSDL2_image \
  ../src/sdlamp2.c

popd 2>&1 >/dev/null
