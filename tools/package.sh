#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a self-contained package and smoke-test it from its own root (M16.2).
#
# `cmake --install` lays a prefix out the way the game already searches: the binaries in
# bin/, the variants and fonts in share/endspielzero/. The binary resolves those relative to
# its working directory, so the package runs with no repository around it. The smoke test
# launches the packaged game headlessly (`--shot` needs no display) from the package root,
# which is the CI-shaped version of "a downloaded build launches, loads a variant and
# renders a frame".
#
#   tools/package.sh [PREFIX]        # default dist/endspielzero
#
# Run inside the gfx dev shell (the renderer needs SDL3 + Vulkan). Pass a build directory
# in `CB_BUILD_DIR` to reuse one; the default is a Release build.
set -euo pipefail
cd "$(dirname "$0")/.."

prefix="${1:-dist/endspielzero}"
build_dir="${CB_BUILD_DIR:-build/release}"

echo "== configure =="
if [[ "$build_dir" == "build/release" ]]; then
  cmake --preset release >/dev/null
fi

echo "== build =="
cmake --build "$build_dir" --target endspielzero_gui endspielzero

echo "== install into $prefix =="
rm -rf "$prefix"
cmake --install "$build_dir" --prefix "$prefix" >/dev/null

test -x "$prefix/bin/endspielzero_gui" || { echo "package: no gui binary" >&2; exit 1; }
test -d "$prefix/share/endspielzero/variants" || { echo "package: no variants" >&2; exit 1; }
# Fonts are optional at runtime - a missing one falls back - but the package should carry
# them or the interface looks plainer than it does in development.
if [[ ! -d "$prefix/share/endspielzero/fonts" ]]; then
  echo "package: warning - no fonts installed; the interface will fall back" >&2
fi

echo "== smoke test from the package root =="
smoke="$(mktemp -d)"
trap 'rm -rf "$smoke"' EXIT
if ! (
  cd "$prefix"
  SDL_VIDEO_DRIVER=dummy ./bin/endspielzero_gui standard --shot "$smoke/frame.ppm" \
    --screen newgame --t 1.0
) >/dev/null 2>&1; then
  echo "smoke: the packaged binary failed to render a frame" >&2
  exit 1
fi
test -s "$smoke/frame.ppm" || { echo "smoke: no frame was written" >&2; exit 1; }

echo "package: OK - $prefix (smoke frame: $smoke/frame.ppm)"
