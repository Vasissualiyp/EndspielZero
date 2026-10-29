# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)
project(EndspielZero VERSION 0.0.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(EndspielZeroLayer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# We do not use C++20 modules; skipping the scanner keeps builds fast.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Nix's stdenv injects -O2 through NIX_CFLAGS_COMPILE, which is fine for release
# but makes a Debug build silently optimized - it even elides allocations, which
# defeats the allocation tripwire. Pin the Debug optimization level explicitly.
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

option(CB_WERROR "Treat warnings as errors" ON)
option(CB_BUILD_TESTS "Build the test suite" ON)
option(CB_BUILD_BENCH "Build benchmarks" ON)

add_library(cb_flags INTERFACE)
target_compile_options(cb_flags INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wold-style-cast -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion
  $<$<BOOL:${CB_WERROR}>:-Werror>)

# Determinism: the engine core must not contain floating-point arithmetic
# (ADR-0003 / ARCH section 12). -Wdouble-promotion above helps; tests/arch
# enforces the rest by scanning sources.

add_subdirectory(src)

# Collected after src/ so tests and benches link every layer without repeating
# the list (and without being able to smuggle in an undeclared one).
get_property(CB_ALL_LAYER_TARGETS GLOBAL PROPERTY CB_ALL_LAYERS)
if(CB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(CB_BUILD_BENCH)
  add_subdirectory(bench)
endif()

cb_write_layer_manifest(${CMAKE_BINARY_DIR}/layers.manifest)

# Variant definitions are data the binary needs at runtime, so they are installed
# alongside it. Without an install rule at all, `nix flake check` fails at the install
# phase even though every test passed - which is exactly what happened.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/variants/
        DESTINATION share/endspielzero/variants
        FILES_MATCHING PATTERN "*.toml")
# The bundled showcase games for the "Watch a game" action (M12.8) are content too: the
# menu reads them at startup, so an install that left them behind would ship a dead row.
install(DIRECTORY ${PROJECT_SOURCE_DIR}/games/
        DESTINATION share/endspielzero/games
        FILES_MATCHING PATTERN "*.cbgame")
