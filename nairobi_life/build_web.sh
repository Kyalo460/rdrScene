#!/bin/bash
# build_web.sh - Build Nairobi Life to WebAssembly (playable in browser).
#
# Self-contained: installs Emscripten if it is not already on PATH, so this
# works on any CI (GitHub Actions, Netlify, Vercel, local) without a prebuilt
# toolchain. raylib 5.5 source is cloned on the fly.
# Output: nairobi_life.html, nairobi_life.js, nairobi_life.wasm (in repo root)
set -e

PROJECT_ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$PROJECT_ROOT"

RAYLIB_VER=5.5
RAYLIB_SRC=${RAYLIB_SRC:-/tmp/raylib-src}

echo "== Nairobi Life Web Build =="
echo "Project root: $PROJECT_ROOT"

# ---- ensure Emscripten is available ----
if command -v emcc >/dev/null 2>&1; then
    echo "-- emcc found: $(emcc --version | head -1) --"
else
    echo "-- installing Emscripten 6.0.6 --"
    EMSDK_DIR=${EMSDK_DIR:-/tmp/emsdk}
    if [ ! -d "$EMSDK_DIR/upstream/emscripten" ]; then
        rm -rf "$EMSDK_DIR"
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
        "$EMSDK_DIR/emsdk" install 6.0.6
        "$EMSDK_DIR/emsdk" activate 6.0.6
    fi
    source "$EMSDK_DIR/emsdk_env.sh"
fi

# ---- raylib source ----
if [ ! -f "$RAYLIB_SRC/src/rcore.c" ]; then
    echo "-- cloning raylib $RAYLIB_VER source --"
    rm -rf "$RAYLIB_SRC"
    git clone --depth 1 --branch $RAYLIB_VER https://github.com/raysan5/raylib.git "$RAYLIB_SRC"
fi

# ---- compile ----
emcc --version | head -1
emcc \
  src/main.c src/nl_util.c src/nl_time.c src/nl_weather.c \
  src/nl_world.c src/nl_npc.c src/nl_econ.c src/nl_render.c src/nl_game.c \
  "$RAYLIB_SRC/src/rcore.c" "$RAYLIB_SRC/src/rshapes.c" \
  "$RAYLIB_SRC/src/rtextures.c" "$RAYLIB_SRC/src/rtext.c" \
  "$RAYLIB_SRC/src/rmodels.c" "$RAYLIB_SRC/src/raudio.c" \
  "$RAYLIB_SRC/src/utils.c" "$RAYLIB_SRC/src/rglfw.c" \
  -o nairobi_life.html \
  -std=gnu99 -Wall -Wextra -O2 \
  -I./src -Isrc -I"$RAYLIB_SRC/src" \
  -DPLATFORM_WEB=1 \
  -s USE_GLFW=3 \
  -s ASYNCIFY=1 \
  -s TOTAL_MEMORY=67108864 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s FORCE_FILESYSTEM=1 \
  -s MINIFY_HTML=0 \
  -s WARN_ON_UNDEFINED_SYMBOLS=0 \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -s EXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
  --shell-file "$PROJECT_ROOT/web_shell.html"

echo ""
echo "== Build complete =="
echo "Output: $PROJECT_ROOT/nairobi_life.html"

# Copy the WASM bundle to the repo root so the deploy config (which publishes
# the repository root) can serve it directly.
cp -f nairobi_life.html nairobi_life.js nairobi_life.wasm "$PROJECT_ROOT/"
ls -la nairobi_life.html nairobi_life.js nairobi_life.wasm
echo "Copied to repo root for deployment."
