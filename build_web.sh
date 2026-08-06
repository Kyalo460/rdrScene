#!/bin/bash
set -e

# Clone raylib source
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib-src

# Install emsdk
git clone https://github.com/emscripten-core/emsdk.git /tmp/emsdk
cd /tmp/emsdk
./emsdk install latest
./emsdk activate latest

# Build
cd /tmp/emsdk
source ./emsdk_env.sh
cd $VERCEL_PROJECT_ROOT
/tmp/emsdk/upstream/emscripten/emcc \
  src/main.c src/world.c src/player.c src/enemy.c src/mission.c src/ui.c src/audio.c src/dialogue.c \
  /tmp/raylib-src/src/rcore.c /tmp/raylib-src/src/rshapes.c /tmp/raylib-src/src/rtextures.c \
  /tmp/raylib-src/src/rtext.c /tmp/raylib-src/src/rmodels.c /tmp/raylib-src/src/raudio.c \
  /tmp/raylib-src/src/utils.c /tmp/raylib-src/src/rglfw.c \
  -o nairobi_streets.html \
  -std=c99 -Wall -Wextra -O2 \
  -I./src -Isrc -I/tmp/raylib-src/src \
  -s USE_GLFW=3 -s ASYNCIFY -s TOTAL_MEMORY=67108864 \
  -s FORCE_FILESYSTEM=1 \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -s EXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
  --shell-file shell.html