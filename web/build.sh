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

make web
