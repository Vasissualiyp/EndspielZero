# SPDX-License-Identifier: GPL-3.0-or-later
cb_layer(endspielzero_cli LEVEL 110 DEPS endspielzero_io endspielzero_app)

add_executable(endspielzero main.cpp)
target_link_libraries(endspielzero PRIVATE endspielzero_io endspielzero_view endspielzero_app cb_flags)

install(TARGETS endspielzero RUNTIME DESTINATION bin)
