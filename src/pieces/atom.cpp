// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces/atom.hpp"

#include <algorithm>
#include <numeric>
#include <set>

namespace cb {
namespace {

/// Recursively assign each magnitude to an unused axis, then fan out over signs.
void enumerate(const SmallVec<std::int16_t, kMaxDims>& mags, std::uint8_t dims,
               std::size_t idx, std::array<std::int16_t, kMaxDims>& vec,
               std::uint32_t usedAxes,
               std::set<std::array<std::int16_t, kMaxDims>>& out) {
  if (idx == mags.size()) {
    out.insert(vec);
    return;
  }
  for (std::uint8_t a = 0; a < dims; ++a) {
    const std::uint32_t bit = 1u << a;
    if ((usedAxes & bit) != 0) continue;
    for (const int sign : {1, -1}) {
      vec[a] = static_cast<std::int16_t>(sign * mags[idx]);
      enumerate(mags, dims, idx + 1, vec, usedAxes | bit, out);
    }
    vec[a] = 0;
  }
}

std::uint64_t factorial(std::uint64_t n) {
  std::uint64_t f = 1;
  for (std::uint64_t i = 2; i <= n; ++i) f *= i;
  return f;
}

}  // namespace

Result<MoveAtom> MoveAtom::canonicalize(MoveAtom a) {
  // Pins: distinct axes, nonzero values, and not mixed with the half-space shorthand.
  for (std::size_t i = 0; i < a.pins.size(); ++i) {
    if (a.pins[i].value == 0) {
      return fail(ErrorCode::ValidationError,
                  "a pinned component needs a nonzero value (a zero is implied by omission)");
    }
    for (std::size_t j = i + 1; j < a.pins.size(); ++j) {
      if (a.pins[i].axis == a.pins[j].axis) {
        return fail(ErrorCode::ValidationError,
                    "a move pins the same axis twice");
      }
    }
  }
  // Pins in axis order, so two spellings of the same move are the same move.
  std::sort(a.pins.begin(), a.pins.end(),
            [](const Pin& l, const Pin& r) { return l.axis < r.axis; });
  if (!a.pins.empty() && a.oriented) {
    return fail(ErrorCode::ValidationError,
                "a move with pinned components says its direction itself; "
                "'forward' is for the half-space shorthand only");
  }
  if (a.kind != MoveKind::Extrapolated) {
    if (a.pins.empty()) {
      return fail(ErrorCode::ValidationError,
                  "an exact or expanded move needs a vector with a nonzero component");
    }
    if (!a.mags.empty()) {
      return fail(ErrorCode::ValidationError,
                  "an exact or expanded move is a literal vector; it has no free magnitudes");
    }
    if (a.oriented) {
      return fail(ErrorCode::ValidationError,
                  "'forward' applies to extrapolated moves; pin the orientation axis instead");
    }
    if (a.kind == MoveKind::Exact && (a.minK != 1 || a.maxK != 1)) {
      return fail(ErrorCode::ValidationError,
                  "an exact move is taken once; it has no step range");
    }
    if (a.kind == MoveKind::Exact) a.sign = StepSign::Both;
  } else if (a.mags.empty()) {
    return fail(ErrorCode::ValidationError, "a move atom needs at least one magnitude");
  }
  for (std::size_t i = 0; i < a.mags.size(); ++i) {
    if (a.mags[i] <= 0) {
      return fail(ErrorCode::ValidationError,
                  "move atom magnitudes must be positive; got " +
                      std::to_string(a.mags[i]) +
                      " (a zero component is implied by omission, not written)");
    }
  }
  std::sort(a.mags.begin(), a.mags.end());
  if (a.maxK == 0 || a.minK == 0) {
    return fail(ErrorCode::ValidationError,
                "a move atom with a zero step count can never move");
  }
  if (a.minK > a.maxK) {
    return fail(ErrorCode::ValidationError, "move atom has minK " +
                                                std::to_string(a.minK) + " above maxK " +
                                                std::to_string(a.maxK));
  }
  if (a.minK > 1 && a.mode == MoveMode::Leap) {
    return fail(
        ErrorCode::ValidationError,
        "an atom with minK > 1 must slide or hop; a leap has nothing to skip over");
  }
  if (a.mode == MoveMode::Hop && a.maxK != 1) {
    return fail(
        ErrorCode::Unsupported,
        "hop atoms are single-step for now; a repeating hopper needs its own rule");
  }
  return a;
}

std::string MoveAtom::toString() const {
  // The original spelling is kept byte for byte for the original kind, because the text is
  // what a variant's identity is hashed from; only the new forms say more.
  std::string prefix;
  if (kind != MoveKind::Extrapolated || !pins.empty()) {
    prefix = kind == MoveKind::Exact ? "exact" : kind == MoveKind::Expanded ? "expanded" : "pinned";
    prefix += '{';
    for (std::size_t i = 0; i < pins.size(); ++i) {
      if (i != 0) prefix += ',';
      prefix += std::to_string(pins[i].axis) + ':' + (pins[i].value > 0 ? "+" : "") +
                std::to_string(pins[i].value);
    }
    prefix += '}';
    if (kind == MoveKind::Expanded && sign != StepSign::Both) {
      prefix += sign == StepSign::Positive ? "+" : "-";
    }
    if (flip) prefix += " flip";
    prefix += ' ';
  }
  std::string out = prefix + "[";
  for (std::size_t i = 0; i < mags.size(); ++i) {
    if (i != 0) out += ',';
    out += std::to_string(mags[i]);
  }
  out += ",NULL]^";
  // minK appears only when it constrains, so the common notation stays the one
  // from the spec ("[1,2,NULL]^1"), but two atoms differing only in minK - a
  // pawn's push and its double step - never render identically.
  if (minK > 1) out += std::to_string(minK) + "..";
  out += maxK == kUnlimited ? "inf" : std::to_string(maxK);
  switch (mode) {
    case MoveMode::Slide:
      out += " slide";
      break;
    case MoveMode::Leap:
      out += " leap";
      break;
    case MoveMode::Hop:
      out += " hop";
      break;
  }
  if (capture == CapturePolicy::Must) out += " capture-only";
  if (capture == CapturePolicy::Cannot) out += " no-capture";
  if (oriented) out += " forward";
  if (fromRegion[0].active()) out += " from-home";
  if (unmoved) out += " unmoved";
  return out;
}

bool operator==(const MoveAtom& a, const MoveAtom& b) {
  return a.kind == b.kind && a.pins == b.pins && a.flip == b.flip && a.sign == b.sign &&
         a.mags == b.mags && a.minK == b.minK && a.maxK == b.maxK && a.mode == b.mode &&
         a.capture == b.capture && a.oriented == b.oriented &&
         a.fromRegion[0].axis == b.fromRegion[0].axis &&
         a.fromRegion[0].value == b.fromRegion[0].value &&
         a.leavesEnPassant == b.leavesEnPassant && a.unmoved == b.unmoved;
}

std::vector<Direction> expandAtom(const SmallVec<std::int16_t, kMaxDims>& mags,
                                  std::uint8_t dims) {
  std::vector<Direction> out;
  if (mags.size() > dims) return out;  // needs more axes than the board has

  std::set<std::array<std::int16_t, kMaxDims>> uniq;
  std::array<std::int16_t, kMaxDims> vec{};
  enumerate(mags, dims, 0, vec, 0, uniq);

  // std::set already orders lexicographically, which gives us determinism for
  // free - direction indices must not depend on the compiler or the platform.
  out.reserve(uniq.size());
  for (const auto& v : uniq) out.push_back(Direction::make(v, dims));
  return out;
}

namespace {

int gcdOf(int a, int b) {
  a = a < 0 ? -a : a;
  b = b < 0 ? -b : b;
  while (b != 0) {
    const int t = a % b;
    a = b;
    b = t;
  }
  return a;
}

}  // namespace

std::uint32_t exactSteps(const MoveAtom& atom) noexcept {
  if (atom.kind != MoveKind::Exact || atom.mode != MoveMode::Slide) return 1;
  int g = 0;
  for (const Pin& p : atom.pins) g = gcdOf(g, p.value);
  return static_cast<std::uint32_t>(std::max(1, g));
}

StepRange stepRange(const MoveAtom& atom) noexcept {
  if (atom.kind == MoveKind::Exact) {
    const std::uint32_t n = exactSteps(atom);
    return {n, n};
  }
  return {atom.minK, atom.maxK};
}

std::vector<Direction> expandAtomFor(const MoveAtom& atom, std::uint8_t dims,
                                     int orientAxis, Color color) {
  // The classic forms keep their classic expansion, so nothing about them can drift.
  if (atom.kind == MoveKind::Extrapolated && atom.pins.empty()) {
    if (atom.oriented && orientAxis >= 0) {
      return expandAtomOriented(atom.mags, dims, static_cast<std::uint8_t>(orientAxis), color);
    }
    return expandAtom(atom.mags, dims);
  }

  std::array<std::int16_t, kMaxDims> vec{};
  std::uint32_t used = 0;
  for (const Pin& p : atom.pins) {
    if (p.axis >= dims) return {};  // a pin on an axis the board does not have
    int value = p.value;
    // The other side's forward is this side's backward.
    if (atom.flip && color == Color::Black && static_cast<int>(p.axis) == orientAxis) {
      value = -value;
    }
    vec[p.axis] = static_cast<std::int16_t>(value);
    used |= 1u << p.axis;
  }

  std::set<std::array<std::int16_t, kMaxDims>> uniq;
  switch (atom.kind) {
    case MoveKind::Extrapolated:
      if (atom.mags.size() + atom.pins.size() > dims) return {};
      enumerate(atom.mags, dims, 0, vec, used, uniq);
      break;
    case MoveKind::Exact: {
      const int g = static_cast<int>(exactSteps(atom));
      for (std::uint8_t a = 0; a < dims; ++a) vec[a] = static_cast<std::int16_t>(vec[a] / g);
      uniq.insert(vec);
      break;
    }
    case MoveKind::Expanded: {
      if (atom.sign != StepSign::Negative) uniq.insert(vec);
      if (atom.sign != StepSign::Positive) {
        std::array<std::int16_t, kMaxDims> neg = vec;
        for (std::uint8_t a = 0; a < dims; ++a) neg[a] = static_cast<std::int16_t>(-neg[a]);
        uniq.insert(neg);
      }
      break;
    }
  }
  std::vector<Direction> out;
  out.reserve(uniq.size());
  for (const auto& v : uniq) out.push_back(Direction::make(v, dims));
  return out;
}

std::vector<Direction> expandAtomOriented(const SmallVec<std::int16_t, kMaxDims>& mags,
                                          std::uint8_t dims, std::uint8_t orientAxis,
                                          Color color) {
  std::vector<Direction> out;
  const int want = color == Color::White ? 1 : -1;
  for (const Direction& d : expandAtom(mags, dims)) {
    const int comp = d.v[orientAxis];
    if (comp == 0) continue;
    if ((comp > 0 ? 1 : -1) == want) out.push_back(d);
  }
  return out;
}

std::vector<MoveAtom> straightLineAtoms(std::uint32_t maxK, std::size_t maxAxes) {
  // One step wants a leap, more than one wants a slide so the path must be clear. A
  // single-step slide would behave identically but reads as a line it is not.
  const MoveMode mode = maxK <= 1 ? MoveMode::Leap : MoveMode::Slide;
  std::vector<MoveAtom> out;
  for (std::size_t r = 1; r <= maxAxes && r <= kMaxDims; ++r) {
    MoveAtom a;
    for (std::size_t i = 0; i < r; ++i) a.mags.push(1);
    a.maxK = maxK;
    a.mode = mode;
    out.push_back(a);
  }
  return out;
}

std::uint64_t expectedDirectionCount(const SmallVec<std::int16_t, kMaxDims>& mags,
                                     std::uint8_t dims) {
  const auto r = static_cast<std::uint64_t>(mags.size());
  if (r > dims) return 0;

  // Ordered placements of r magnitudes onto distinct axes: P(dims, r).
  std::uint64_t perms = 1;
  for (std::uint64_t i = 0; i < r; ++i) perms *= (static_cast<std::uint64_t>(dims) - i);

  // Equal magnitudes make some of those placements identical, so divide by the
  // factorial of each repeat group. Missing this is the classic over-count.
  std::vector<std::int16_t> sorted(mags.begin(), mags.end());
  std::sort(sorted.begin(), sorted.end());
  std::uint64_t divisor = 1;
  std::size_t i = 0;
  while (i < sorted.size()) {
    std::size_t j = i;
    while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
    divisor *= factorial(j - i);
    i = j;
  }

  return perms / divisor * (1ull << r);
}

}  // namespace cb
