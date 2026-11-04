#!/usr/bin/env bash
# Build the WebAssembly site into web/public. Used as Vercel's build command;
# also works locally. If emcc is not on PATH, a pinned Emscripten SDK is
# installed into .emsdk/ first.
set -euo pipefail

EMSDK_VERSION="${EMSDK_VERSION:-3.1.74}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v emcc >/dev/null 2>&1; then
  EMSDK_DIR="$ROOT/.emsdk"
  # Test for the tool, not the directory: Vercel's build cache can restore a
  # partial .emsdk/ (seen: directory present, emsdk script missing), and a
  # bare -d check then skips the clone and fails at the install step.
  if [ ! -x "$EMSDK_DIR/emsdk" ]; then
    rm -rf "$EMSDK_DIR"
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
  fi
  "$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"
  "$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"
  # shellcheck disable=SC1091
  source "$EMSDK_DIR/emsdk_env.sh"
fi

# The Doom build bakes the shareware IWAD into its preloaded filesystem.
# WADs are gitignored, so CI and Vercel builders fetch it; the file is the
# freely redistributable shareware episode, pinned by hash. A doom1.wad
# already on disk (a local checkout) is used as-is.
WAD_SHA256="1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
WAD_URL="https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad"
if [ ! -f doom1.wad ]; then
  echo "fetching shareware doom1.wad..."
  curl -fsSL --retry 3 -o doom1.wad.tmp "$WAD_URL"
  if command -v sha256sum >/dev/null 2>&1; then
    echo "$WAD_SHA256  doom1.wad.tmp" | sha256sum -c -
  else
    echo "$WAD_SHA256  doom1.wad.tmp" | shasum -a 256 -c -
  fi
  mv doom1.wad.tmp doom1.wad
fi

make web web-doom
