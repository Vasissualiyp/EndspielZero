// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "base/rng.hpp"

namespace cb {

/// Incremental position hashing. Keys are derived from (seed, variantId) so a
/// hash is reproducible across runs and hosts - multiplayer desync detection and
/// the replay corpus both rely on that (ARCH section 11).
class Zobrist {
 public:
  void init(std::uint64_t seed, std::uint32_t cellCount, std::uint32_t pieceCodes,
            std::uint32_t extraSlots);

  [[nodiscard]] std::uint64_t piece(std::uint32_t cell, std::uint32_t code) const {
    return pieceKeys_[static_cast<std::size_t>(cell) * pieceCodes_ + code];
  }
  [[nodiscard]] std::uint64_t side() const { return sideKey_; }
  /// Key for "en-passant target is this cell". Index cellCount means "none".
  [[nodiscard]] std::uint64_t epTarget(std::uint32_t cellOrNone) const {
    return epKeys_[cellOrNone];
  }
  [[nodiscard]] std::uint64_t castleRights(std::uint32_t mask) const {
    return castleKeys_[mask & 0xFFu];
  }
  /// Key for "the piece on this cell has moved" - the last extra slot, so it can never
  /// collide with a field's. Contributes only when the bit is set.
  [[nodiscard]] std::uint64_t moved(std::uint32_t cell) const {
    std::uint64_t z = extraKeys_.back() ^ (static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  /// Key for "this cell carries a mark of this tag with this many plies left" - the
  /// second-to-last extra slot. A position with no marks contributes nothing.
  [[nodiscard]] std::uint64_t mark(std::uint32_t cell, std::uint32_t tag, std::uint32_t ttl) const {
    std::uint64_t z = extraKeys_[extraKeys_.size() - 2] ^
                      (static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ULL) ^
                      (static_cast<std::uint64_t>(tag * 65536u + ttl) * 0xBF58476D1CE4E5B9ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  /// Generic slot, used by hashed custom field columns (M5).
  [[nodiscard]] std::uint64_t extra(std::uint32_t slot) const { return extraKeys_[slot]; }

  /// Key for "custom field `slot` on `cell` holds `value`".
  ///
  /// Derived rather than tabulated: a field can hold any 32-bit value, so a table would
  /// be unbounded. Mixing is SplitMix64, which is deterministic everywhere and has no
  /// structure a position could accidentally line up with.
  [[nodiscard]] std::uint64_t field(std::uint32_t slot, std::uint32_t cell,
                                    std::int32_t value) const {
    std::uint64_t z = extraKeys_[slot % extraKeys_.size()];
    z ^= static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ULL;
    z ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)) *
         0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// Key for "the piece on `cell` is flipped" (Piece::flipped). Derived like field():
  /// most variants never flip a piece, so a table would be dead weight.
  [[nodiscard]] std::uint64_t flipped(std::uint32_t cell) const {
    std::uint64_t z =
        sideKey_ ^ (static_cast<std::uint64_t>(cell) + 1) * 0xD6E8FEB86659FD93ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  [[nodiscard]] bool initialized() const noexcept { return pieceCodes_ != 0; }
  [[nodiscard]] std::uint32_t pieceCodes() const noexcept { return pieceCodes_; }

 private:
  std::vector<std::uint64_t> pieceKeys_;
  std::vector<std::uint64_t> epKeys_;
  std::vector<std::uint64_t> castleKeys_;
  std::vector<std::uint64_t> extraKeys_;
  std::uint64_t sideKey_{0};
  std::uint32_t pieceCodes_{0};
};

}  // namespace cb
