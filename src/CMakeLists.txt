# SPDX-License-Identifier: GPL-3.0-or-later
# Layers are added bottom-up; cb_layer() rejects an upward dependency, so the
# layer map in docs/ARCHITECTURE.md is enforced at configure time.
#
# The windowed executable lives in gui/ and is added last, because it needs both the
# interaction layer and the renderer to have been declared.
add_subdirectory(base)
add_subdirectory(diag)
add_subdirectory(space)
add_subdirectory(geometry)
add_subdirectory(assets)
add_subdirectory(pieces)
add_subdirectory(variant)
add_subdirectory(position)
add_subdirectory(rules)
add_subdirectory(movegen)
add_subdirectory(view)
add_subdirectory(temporal)
add_subdirectory(game)
add_subdirectory(ai)
add_subdirectory(io)
add_subdirectory(audio)
add_subdirectory(app)
add_subdirectory(cli)
add_subdirectory(net)
add_subdirectory(render)
add_subdirectory(gui)
