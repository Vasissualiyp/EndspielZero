// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "variant/variant.hpp"

namespace cb {

/// Piece::flags bit: the piece's sense of "forward" is reversed.
constexpr std::uint8_t kPieceFlipped = 1u << 0;

/// What sits on a cell. Four bytes, so the board is a flat array with good cache
/// behaviour and can be uploaded to the GPU as-is (ARCH section 10).
struct Piece {
  PieceTypeId type{kNoPiece};
  std::uint8_t color{0};
  std::uint8_t flags{0};  ///< kPieceFlipped; the other bits are reserved

  [[nodiscard]] bool empty() const noexcept { return type == kNoPiece; }
  /// A directional piece that has crossed an orientation-reversing seam an odd number
  /// of times: its forward moves, home region and promotion region are the other
  /// colour's (see PieceTypeDef::reorients).
  [[nodiscard]] bool flipped() const noexcept { return (flags & kPieceFlipped) != 0; }
  [[nodiscard]] Color colorOf() const noexcept { return static_cast<Color>(color); }
  /// The colour whose forward directions this piece uses - its own, unless flipped.
  [[nodiscard]] Color orientation() const noexcept {
    return static_cast<Color>(color ^ (flags & kPieceFlipped));
  }

  friend bool operator==(const Piece& a, const Piece& b) {
    return a.type == b.type && a.color == b.color && a.flags == b.flags;
  }
};

static_assert(sizeof(Piece) == 4);
static_assert(std::is_trivially_copyable_v<Piece>);

/// Zobrist code for a (type, colour) pair. Type 0 is empty and has no code.
inline std::uint32_t pieceCode(PieceTypeId type, Color c) noexcept {
  return static_cast<std::uint32_t>(type) * kNumColors + static_cast<std::uint32_t>(c);
}

}  // namespace cb
