#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run only the tests a change can touch.
#
# `ctest --preset dev` is the whole suite, and it rebuilds and runs the ~4-minute property
# sweep and the perft counts. That is for engine changes; tweaking a colour or a camera
# should not wait on it. This runs the Catch2 tag(s) directly, or the fast ctest labels.
#
#   tools/test.sh --build view app     # the [view] and [app] tags
#   tools/test.sh --build render       # draw lists, deco, overtures, the GUI frame gate
#   tools/test.sh quick                # every fast label (unit render golden arch)
#   tools/test.sh all                  # exactly `ctest --preset dev`
#
# Build inside the dev shell, from anywhere: the script cd's to the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."

build=0
if [[ "${1:-}" == "--build" ]]; then build=1; shift; fi
if [[ $# -eq 0 ]]; then
  echo "usage: tools/test.sh [--build] <tag...> | quick | all" >&2
  exit 2
fi

build_now() {
  if ((build)); then cmake --build build/dev; fi
}

if [[ "$1" == "all" ]]; then
  build_now
  exec ctest --preset dev
fi
if [[ "$1" == "quick" ]]; then
  build_now
  # One regex, because ctest -L repeated means AND, not OR. property and perft are the
  # slow labels and are left out.
  exec ctest --preset dev -L 'unit|render|golden|arch'
fi

# Tags become a Catch2 filter: `[view],[app]` is "view OR app". Build first if asked, so
# the binary tested is the one just compiled - a stale binary is how a change looks green.
build_now
filter=""
for tag in "$@"; do
  [[ -n "$filter" ]] && filter+=","
  filter+="[$tag]"
done
if [[ ! -x build/dev/tests/endspielzero_tests ]]; then
  echo "test.sh: build/dev/tests/endspielzero_tests not found; build first (cmake --preset dev && cmake --build build/dev)" >&2
  exit 2
fi
exec build/dev/tests/endspielzero_tests "$filter"
