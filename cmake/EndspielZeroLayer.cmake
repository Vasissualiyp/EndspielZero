# SPDX-License-Identifier: GPL-3.0-or-later
#
# cb_layer(<name> LEVEL <n> [SOURCES ...] [DEPS ...] [SYSTEM_DEPS ...])
#
# Declares one architectural layer as a CMake target. The layer map in
# docs/ARCHITECTURE.md is enforced here: a layer may only depend on layers with a
# strictly lower LEVEL. Violating the map is therefore a *configure* error, not a
# review comment.
set(CB_LAYER_LEVELS "" CACHE INTERNAL "")

function(cb_layer name)
  cmake_parse_arguments(A "" "LEVEL" "SOURCES;DEPS;SYSTEM_DEPS" ${ARGN})
  if(NOT DEFINED A_LEVEL)
    message(FATAL_ERROR "cb_layer(${name}): LEVEL is required")
  endif()

  foreach(dep IN LISTS A_DEPS)
    get_property(dep_level GLOBAL PROPERTY CB_LEVEL_${dep})
    if(NOT DEFINED dep_level)
      message(FATAL_ERROR
        "cb_layer(${name}): depends on '${dep}', which is not a declared layer "
        "(or is declared later - layers must be added bottom-up).")
    endif()
    if(NOT dep_level LESS A_LEVEL)
      message(FATAL_ERROR
        "LAYER VIOLATION: ${name} (L${A_LEVEL}) may not depend on ${dep} "
        "(L${dep_level}). Dependencies point strictly down; see "
        "docs/ARCHITECTURE.md section 1.")
    endif()
  endforeach()

  if(A_SOURCES)
    add_library(${name} STATIC ${A_SOURCES})
  else()
    add_library(${name} INTERFACE)
  endif()

  set_property(GLOBAL PROPERTY CB_LEVEL_${name} ${A_LEVEL})
  set_property(GLOBAL APPEND PROPERTY CB_ALL_LAYERS ${name})

  if(A_SOURCES)
    target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC ${A_DEPS} ${A_SYSTEM_DEPS})
    target_compile_features(${name} PUBLIC cxx_std_23)
  else()
    target_include_directories(${name} INTERFACE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${name} INTERFACE ${A_DEPS} ${A_SYSTEM_DEPS})
    target_compile_features(${name} INTERFACE cxx_std_23)
  endif()
endfunction()

# Emit the declared layer graph so tests/arch can assert it matches AGENTS.md.
function(cb_write_layer_manifest path)
  get_property(layers GLOBAL PROPERTY CB_ALL_LAYERS)
  set(out "")
  foreach(l IN LISTS layers)
    get_property(lvl GLOBAL PROPERTY CB_LEVEL_${l})
    string(APPEND out "${l} ${lvl}\n")
  endforeach()
  file(WRITE ${path} ${out})
endfunction()
