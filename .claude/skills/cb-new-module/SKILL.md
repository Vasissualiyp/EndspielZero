---
name: cb-new-module
description: Scaffold a new module (header/source pair) inside a EndspielZero architectural layer, with its CMake wiring, test file and docs row. Use when adding a new file to src/, or a whole new layer.
---

# Add a module to a layer

## 1. Pick the layer

From the layer map in `AGENTS.md`. If the module would need something from a
*higher* layer, it is in the wrong layer - `cb_layer()` will reject the
dependency at configure time, by design.

## 2. Files

```
src/<layer>/<module>.hpp      // SPDX header first line, #pragma once
src/<layer>/<module>.cpp      // only if it needs a TU; prefer header-only for
                              // small hot-path code so it can inline
tests/unit/<layer>/test_<module>.cpp
```

Every file starts with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
```

An arch test enforces this, and another enforces that core layers contain no
floating-point types.

## 3. CMake

Add the `.cpp` to the existing `SOURCES` list in `src/<layer>/CMakeLists.txt`.
Headers need no entry. Test files are globbed - no CMake change needed.

For a whole new layer:

```cmake
# src/<layer>/CMakeLists.txt
cb_layer(endspielzero_<layer> LEVEL <n>
  SOURCES <files>.cpp
  DEPS endspielzero_<lower> ...)
```

Levels are spaced out and **must be unique** - an arch test asserts it, because two
layers at the same level means the map no longer defines an order. Current levels:
base 0, diag 5, space 10, geometry 20, pieces 30, variant 35, position 40, movegen 50,
view 60, rules 65, temporal 70, game 80, io 90, app 95, render 100, cli 110. Add
`add_subdirectory(<layer>)` to `src/CMakeLists.txt` **in bottom-up order** -
`cb_layer()` requires dependencies to be declared before their users.

## 4. Docs

Add the row to the layer table in `AGENTS.md` in the same commit. If the module
introduces or changes an invariant, say so in `docs/ARCHITECTURE.md` and mark it
`**[INVARIANT]**`; if it embodies a decision, write an ADR (`cb-adr`).

## 5. Verify

```bash
cmake --preset dev && cmake --build build/dev && ctest --preset dev -L unit
ctest --preset dev -L arch     # SPDX, no-float, layer manifest
```
