# SPDX-License-Identifier: GPL-3.0-or-later
#
# The playable window. Added last because it needs both the interaction layer and the
# renderer to exist, and it is skipped entirely when there is no graphics stack.
if(TARGET endspielzero_render AND CB_HAVE_GUI)
  add_executable(endspielzero_gui main.cpp)
  target_link_libraries(endspielzero_gui PRIVATE endspielzero_render endspielzero_app endspielzero_audio
                                            cb_flags)
  install(TARGETS endspielzero_gui RUNTIME DESTINATION bin)
endif()
