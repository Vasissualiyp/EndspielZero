# SPDX-License-Identifier: GPL-3.0-or-later
#
# The renderer is optional: the engine, the CLI and the whole test suite build and pass
# without Vulkan present. That is deliberate - a headless server or a CI runner with no
# graphics stack must still be able to build and verify the game.
find_package(Vulkan)
if(NOT Vulkan_FOUND)
  message(STATUS "Vulkan not found - skipping the renderer (engine and CLI are unaffected)")
  return()
endif()

find_program(CB_GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})
if(NOT CB_GLSLC)
  message(STATUS "glslc not found - skipping the renderer")
  return()
endif()

# Shaders are compiled to SPIR-V at build time and embedded as arrays: nothing is
# compiled at runtime and nothing has to be found on disk next to the binary.
set(CB_SPV_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders.hpp)
set(CB_SHADERS board.vert board.frag backdrop.vert backdrop.frag blur.vert blur.frag
               post_down.frag post_up.frag post_composite.frag
               imgui_field.vert imgui_field.frag)
set(CB_SPV_FILES "")
foreach(shader IN LISTS CB_SHADERS)
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
  set(spv ${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv)
  add_custom_command(OUTPUT ${spv}
    COMMAND ${CB_GLSLC} --target-env=vulkan1.3 -O -o ${spv} ${src}
    DEPENDS ${src}
    COMMENT "glslc ${shader}"
    VERBATIM)
  list(APPEND CB_SPV_FILES ${spv})
endforeach()

add_custom_command(OUTPUT ${CB_SPV_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${CB_SPV_HEADER}
          "-DSPVS=${CB_SPV_FILES}"
          -P ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
  DEPENDS ${CB_SPV_FILES} ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
  COMMENT "embedding SPIR-V"
  VERBATIM)

set(CB_RENDER_SOURCES
  vulkan_context.cpp offscreen_target.cpp board_renderer.cpp image_io.cpp
  piece_mesh.cpp piece_icon.cpp panel_response.cpp panel_alive.cpp board_glow.cpp
  post_params.cpp post_process.cpp board_fx.cpp
  ${CB_SPV_HEADER})

# SDL3 supplies the window and the surface; Dear ImGui supplies the panels. Without
# either, the renderer still builds and the headless tests still run - only the playable
# window is missing.
find_package(SDL3 CONFIG)
find_package(imgui CONFIG)
if(SDL3_FOUND)
  list(APPEND CB_RENDER_SOURCES window.cpp)
  if(imgui_FOUND)
    list(APPEND CB_RENDER_SOURCES ui.cpp ui_menus.cpp ui_widgets.cpp deco.cpp
                                 overture_scene.cpp play_surface.cpp ui_designer.cpp
                                 shape_sequence.cpp)
  endif()
endif()

cb_layer(endspielzero_render LEVEL 100
  SOURCES ${CB_RENDER_SOURCES}
  DEPS endspielzero_view endspielzero_app endspielzero_assets endspielzero_audio
  SYSTEM_DEPS Vulkan::Vulkan)
target_include_directories(endspielzero_render PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(SDL3_FOUND)
  target_link_libraries(endspielzero_render PUBLIC SDL3::SDL3)
  target_compile_definitions(endspielzero_render PUBLIC CB_HAVE_SDL=1)
endif()
if(SDL3_FOUND AND imgui_FOUND)
  target_link_libraries(endspielzero_render PUBLIC imgui::imgui)
  target_compile_definitions(endspielzero_render PUBLIC CB_HAVE_IMGUI=1)
endif()

# The game installs its own typefaces so it looks the same on every machine rather than
# inheriting whatever the system happens to have.
# The shell's three voices, copied beside the binary so the game looks the same from the
# build tree, from an install, or from a desktop entry with no sensible cwd. A missing
# font is not an error - the interface falls back and simply looks plainer.
set(CB_FONT_FILES "")
foreach(spec
    "CB_FONT_DISPLAY|ChakraPetch-Bold.ttf"
    "CB_FONT_DISPLAY|ChakraPetch-SemiBold.ttf"
    "CB_FONT_BODY|PublicSans-Regular.ttf"
    "CB_FONT_BODY|PublicSans-Light.ttf"
    "CB_FONT_MONO|JetBrainsMono-Regular.ttf")
  string(REPLACE "|" ";" parts "${spec}")
  list(GET parts 0 var)
  list(GET parts 1 file)
  if(DEFINED ENV{${var}} AND EXISTS "$ENV{${var}}/${file}")
    list(APPEND CB_FONT_FILES "$ENV{${var}}/${file}")
  endif()
endforeach()
if(CB_FONT_FILES)
  file(COPY ${CB_FONT_FILES} DESTINATION ${CMAKE_BINARY_DIR}/fonts)
  install(FILES ${CB_FONT_FILES} DESTINATION share/endspielzero/fonts)
endif()

set(CB_HAVE_VULKAN ON CACHE INTERNAL "")
set(CB_HAVE_GUI ${SDL3_FOUND} CACHE INTERNAL "")
