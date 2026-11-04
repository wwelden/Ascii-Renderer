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
# Mirrors are tried in order; each download must match the pinned hash.
# (The slitaz mirror served this file for years, then 404ed mid-2026 —
# hence the list.)
WAD_URLS="
https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad
https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad
"
wad_ok() {
  if command -v sha256sum >/dev/null 2>&1; then
    echo "$WAD_SHA256  $1" | sha256sum -c - >/dev/null 2>&1
  else
    echo "$WAD_SHA256  $1" | shasum -a 256 -c - >/dev/null 2>&1
  fi
}
if [ ! -f doom1.wad ]; then
  for url in $WAD_URLS; do
    echo "fetching shareware doom1.wad from $url..."
    if curl -fsSL --retry 2 -o doom1.wad.tmp "$url" && wad_ok doom1.wad.tmp; then
      mv doom1.wad.tmp doom1.wad
      break
    fi
    rm -f doom1.wad.tmp
  done
  if [ ! -f doom1.wad ]; then
    echo "error: could not fetch doom1.wad from any mirror" >&2
    exit 1
  fi
fi

make web web-doom
