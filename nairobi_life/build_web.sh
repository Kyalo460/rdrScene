#!/bin/bash
# build_web.sh - Build Nairobi Life to WebAssembly (playable in browser).
#
# Requires a working Emscripten SDK (emsdk) on PATH as `emcc`.
# The raylib 5.5 source is cloned on the fly (needs network once).
# Output: nairobi_life.html, nairobi_life.js, nairobi_life.wasm, nairobi_life.data
set -e

PROJECT_ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$PROJECT_ROOT"

RAYLIB_VER=5.5
RAYLIB_SRC=${RAYLIB_SRC:-/tmp/raylib-src}

echo "== Nairobi Life Web Build =="
echo "Project root: $PROJECT_ROOT"

# ---- raylib source ----
if [ ! -f "$RAYLIB_SRC/src/rcore.c" ]; then
    echo "-- cloning raylib $RAYLIB_VER source --"
    rm -rf "$RAYLIB_SRC"
    git clone --depth 1 --branch $RAYLIB_VER https://github.com/raysan5/raylib.git "$RAYLIB_SRC"
fi

# ---- emcc present? ----
if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not found. Activate emsdk first (source emsdk_env.sh)." >&2
    exit 1
fi
emcc --version | head -1

# ---- compile ----
# raylib 5.5 uses GLFW 3 in web builds. We link raylib's sources directly so
# no prebuilt lib is needed. 64MB heap; asyncify keeps the loop smooth.
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
  -s FORCE_FILESYSTEM=1 \
  -s MINIFY_HTML=0 \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -s EXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
  --shell-file "$PROJECT_ROOT/web_shell.html"

echo ""
echo "== Build complete =="
echo "Output: $PROJECT_ROOT/nairobi_life.html"

# Copy the WASM bundle to the repo root so the existing Netlify/Vercel deploy
# (which publishes the repository root) can serve it directly.
cp -f nairobi_life.html nairobi_life.js nairobi_life.wasm "$PROJECT_ROOT/" 2>/dev/null || true
ls -la nairobi_life.html nairobi_life.js nairobi_life.wasm 2>/dev/null
echo "Copied to repo root for deployment."
