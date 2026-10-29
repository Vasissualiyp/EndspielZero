# SPDX-License-Identifier: GPL-3.0-or-later
#
# The multiplayer front end: an authoritative server and thin clients over a framed
# protocol. It has no window and no sockets of its own - the transport is injected - so
# the protocol is tested in-process and the network is one implementation of many.
cb_layer(endspielzero_net LEVEL 105
  SOURCES protocol.cpp loopback.cpp server.cpp client.cpp socket.cpp
  DEPS endspielzero_game endspielzero_io endspielzero_movegen)
