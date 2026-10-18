// SPDX-License-Identifier: GPL-3.0-or-later
#include "assets/piece_model.hpp"

#include <algorithm>
#include <array>

namespace cb::assets {
namespace {

constexpr std::int16_t kBox = 1000;
constexpr std::uint8_t kMinSegments = 3;
constexpr std::uint8_t kMaxSegments = 64;

/// Twice the signed area of a triangle. Integers throughout: coordinates are at most
/// 1000, so a difference is at most 2000 and this product at most about four million -
/// comfortably inside 32 bits, which is the whole reason the authoring plane is
/// permille rather than a float.
std::int32_t cross(const ModelPoint& o, const ModelPoint& a, const ModelPoint& b) {
  const std::int32_t ax = a.x - o.x;
  const std::int32_t ay = a.y - o.y;
  const std::int32_t bx = b.x - o.x;
  const std::int32_t by = b.y - o.y;
  return ax * by - ay * bx;
}

int sign(std::int32_t v) {
  return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

bool onSegment(const ModelPoint& a, const ModelPoint& b, const ModelPoint& p) {
  return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) &&
         std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y);
}

/// Do two closed segments cross? Touching at a shared endpoint is handled by the caller,
/// which skips adjacent pairs - what is caught here is a genuine crossing.
bool segmentsCross(const ModelPoint& a, const ModelPoint& b, const ModelPoint& c,
                   const ModelPoint& d) {
  const int d1 = sign(cross(a, b, c));
  const int d2 = sign(cross(a, b, d));
  const int d3 = sign(cross(c, d, a));
  const int d4 = sign(cross(c, d, b));
  if (d1 != d2 && d3 != d4) return true;
  if (d1 == 0 && onSegment(a, b, c)) return true;
  if (d2 == 0 && onSegment(a, b, d)) return true;
  if (d3 == 0 && onSegment(c, d, a)) return true;
  if (d4 == 0 && onSegment(c, d, b)) return true;
  return false;
}

Result<void> checkOutline(const Outline& o, const char* what, bool allowNegativeX) {
  const std::size_t n = o.points.size();
  if (n < 3) {
    return fail(ErrorCode::ValidationError,
                std::string(what) + " needs at least three points");
  }
  for (const ModelPoint& p : o.points) {
    const std::int16_t lo = allowNegativeX ? static_cast<std::int16_t>(-kBox) : 0;
    if (p.x < lo || p.x > kBox || p.y < 0 || p.y > kBox) {
      return fail(ErrorCode::ValidationError, std::string(what) + " leaves its box at (" +
                                                  std::to_string(p.x) + ", " +
                                                  std::to_string(p.y) + ")");
    }
  }
  // Every pair of non-adjacent edges. A closed outline that crosses itself has no
  // inside, so there is nothing for the assembly step to sweep or fill.
  for (std::size_t i = 0; i < n; ++i) {
    const ModelPoint& a = o.points[i];
    const ModelPoint& b = o.points[(i + 1) % n];
    for (std::size_t j = i + 1; j < n; ++j) {
      if (j == i || (j + 1) % n == i || (i + 1) % n == j) continue;
      const ModelPoint& c = o.points[j];
      const ModelPoint& d = o.points[(j + 1) % n];
      if (segmentsCross(a, b, c, d)) {
        return fail(ErrorCode::ValidationError,
                    std::string(what) + " crosses itself between segments " +
                        std::to_string(i) + " and " + std::to_string(j));
      }
    }
  }
  // A zero-area outline is a line, and a line has no silhouette.
  std::int32_t twiceArea = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const ModelPoint& a = o.points[i];
    const ModelPoint& b = o.points[(i + 1) % n];
    twiceArea +=
        static_cast<std::int32_t>(a.x) * b.y - static_cast<std::int32_t>(b.x) * a.y;
  }
  if (twiceArea == 0) {
    return fail(ErrorCode::ValidationError, std::string(what) + " encloses no area");
  }
  return {};
}

}  // namespace

int copiesOf(const Symmetry& s) noexcept {
  switch (s.kind) {
    case SymmetryKind::Full:
      return 0;
    case SymmetryKind::KFold:
      return s.k < 2 ? 0 : static_cast<int>(s.k);
    case SymmetryKind::Mirror:
      return 2;
  }
  return 0;
}

Result<void> validate(const PieceModel& m) {
  // The profile lives in a half-plane: radius is a distance from the axis, and a
  // negative one would sweep the solid through itself.
  if (auto ok = checkOutline(m.profile, "the profile", false); !ok.has_value()) {
    return ok;
  }
  if (m.segments < kMinSegments || m.segments > kMaxSegments) {
    return fail(
        ErrorCode::ValidationError,
        "a revolve needs between 3 and 64 segments, not " + std::to_string(m.segments));
  }
  if (m.symmetry.kind == SymmetryKind::KFold && m.symmetry.k < 2) {
    return fail(ErrorCode::ValidationError, "a k-fold symmetry needs k of 2 or more");
  }
  // The foot has to touch the board, or the piece hovers over its square.
  std::int16_t lowest = kBox;
  for (const ModelPoint& p : m.profile.points) lowest = std::min(lowest, p.y);
  if (lowest != 0) {
    return fail(ErrorCode::ValidationError,
                "the profile's foot must sit on height 0, not " + std::to_string(lowest));
  }
  if (!m.elements.empty() && m.symmetry.kind == SymmetryKind::Full) {
    // Stated rather than silently dropped: it is a real thing the author asked for and
    // the reason it cannot happen is worth one sentence.
    return fail(ErrorCode::ValidationError,
                "a radially symmetric piece cannot carry an element - choose k-fold or "
                "mirror first");
  }
  for (std::size_t i = 0; i < m.elements.size(); ++i) {
    const std::string what = "element " + std::to_string(i + 1);
    if (auto ok = checkOutline(m.elements[i].outline, what.c_str(), true);
        !ok.has_value()) {
      return ok;
    }
    const Element& e = m.elements[i];
    if (e.radius < 0 || e.radius > kBox || e.height < 0 || e.height > kBox) {
      return fail(ErrorCode::ValidationError, what + " is placed outside the piece's box");
    }
    if (e.lateral < -kBox / 2 || e.lateral > kBox / 2) {
      return fail(ErrorCode::ValidationError, what + " is offset too far to one side");
    }
    if (e.thickness < 20 || e.thickness > kBox) {
      return fail(ErrorCode::ValidationError,
                  what + " must be between 20 and 1000 thick, not " +
                      std::to_string(e.thickness));
    }
  }
  return {};
}

Result<void> validate(const IconModel& m) {
  if (m.fills.empty()) {
    return fail(ErrorCode::ValidationError, "an icon needs at least one outline");
  }
  for (std::size_t i = 0; i < m.fills.size(); ++i) {
    const std::string what = "outline " + std::to_string(i + 1);
    if (auto ok = checkOutline(m.fills[i], what.c_str(), false); !ok.has_value()) {
      return ok;
    }
  }
  for (std::size_t i = 0; i < m.cuts.size(); ++i) {
    const std::string what = "cut-out " + std::to_string(i + 1);
    if (auto ok = checkOutline(m.cuts[i], what.c_str(), false); !ok.has_value()) {
      return ok;
    }
  }
  return {};
}

IconModel iconFromProfile(const PieceModel& m) {
  IconModel icon;
  Outline side;
  // The profile is one half of the silhouette. Walk it out along the right of the box
  // and back down the left, mirrored - which is the figure a player actually sees.
  // Icon space is the glyph box with y pointing *down*, so height inverts.
  const auto toIcon = [](std::int16_t r, std::int16_t h, int side01) {
    const int x = 500 + (side01 == 0 ? r : -r) / 2;
    return ModelPoint{static_cast<std::int16_t>(std::clamp(x, 0, 1000)),
                      static_cast<std::int16_t>(std::clamp(1000 - h, 0, 1000))};
  };
  // A point *on* the axis mirrors onto itself, so appending it twice would leave a
  // zero-length edge at the top and the foot - which reads, correctly, as an outline
  // touching itself and is refused by `validate`. Adjacent duplicates are dropped as
  // they are produced, including across the closing edge.
  const auto put = [&side](ModelPoint p) {
    if (!side.points.empty() && side.points.back() == p) return;
    side.points.push_back(p);
  };
  for (const ModelPoint& p : m.profile.points) put(toIcon(p.x, p.y, 0));
  for (auto it = m.profile.points.rbegin(); it != m.profile.points.rend(); ++it) {
    put(toIcon(it->x, it->y, 1));
  }
  while (side.points.size() > 3 && side.points.front() == side.points.back()) {
    side.points.pop_back();
  }
  icon.fills.push_back(std::move(side));
  return icon;
}

PieceModel archetypeModel(std::string_view name) {
  PieceModel m;
  m.segments = 20;
  // Radius outward, height up, both permille. These are the shipped archetypes read as
  // profiles: a foot, a waist, and whatever sits on top.
  const auto profile = [&](std::initializer_list<ModelPoint> pts) {
    m.profile.points.assign(pts);
  };
  if (name == "dome") {  // pawn
    profile({{0, 0},
             {330, 0},
             {300, 90},
             {170, 190},
             {150, 400},
             {260, 470},
             {250, 620},
             {120, 700},
             {0, 720}});
  } else if (name == "tower") {  // rook
    profile({{0, 0},
             {380, 0},
             {350, 100},
             {230, 200},
             {220, 620},
             {330, 700},
             {330, 760},
             {0, 760}});
    // The crenellations: four merlons round the rim, which is what makes a rook a rook.
    m.symmetry = Symmetry{SymmetryKind::KFold, 4};
    Element merlon;
    merlon.outline.points = {{-80, 0}, {80, 0}, {80, 120}, {-80, 120}};
    merlon.radius = 250;
    merlon.height = 760;
    merlon.tangent = true;
    merlon.thickness = 110;
    m.elements.push_back(merlon);
  } else if (name == "spire") {  // bishop
    profile({{0, 0},
             {350, 0},
             {320, 100},
             {190, 210},
             {180, 520},
             {290, 600},
             {150, 700},
             {90, 860},
             {0, 900}});
  } else if (name == "crown") {  // queen
    profile({{0, 0},
             {400, 0},
             {370, 110},
             {230, 230},
             {200, 560},
             {330, 660},
             {300, 810},
             {0, 860}});
    m.symmetry = Symmetry{SymmetryKind::KFold, 5};
    Element point;
    point.outline.points = {{0, 0}, {90, 130}, {0, 210}, {-90, 130}};
    point.radius = 250;
    point.height = 810;
    point.tangent = true;
    point.thickness = 90;
    m.elements.push_back(point);
  } else if (name == "monolith") {  // king
    profile({{0, 0},
             {400, 0},
             {370, 110},
             {240, 230},
             {220, 620},
             {330, 700},
             {330, 840},
             {0, 840}});
    m.symmetry = Symmetry{SymmetryKind::Mirror, 2};
    Element arm;
    arm.outline.points = {{0, 0}, {160, 0}, {160, 70}, {0, 70}};
    arm.radius = 0;
    arm.height = 900;
    m.elements.push_back(arm);
  } else if (name == "wedge") {  // knight
    profile({{0, 0}, {360, 0}, {330, 100}, {210, 210}, {190, 480}, {0, 520}});
    m.symmetry = Symmetry{SymmetryKind::Mirror, 2};
    Element head;
    head.outline.points = {{0, 0},     {250, 90},   {300, 300},
                           {120, 420}, {-120, 380}, {-60, 180}};
    head.radius = 0;
    head.height = 480;
    m.elements.push_back(head);
  } else if (name == "horn") {  // unicorn
    profile({{0, 0}, {340, 0}, {310, 100}, {180, 210}, {160, 480}, {70, 780}, {0, 940}});
  } else {  // the slab every unknown archetype falls back to
    profile({{0, 0}, {330, 0}, {300, 90}, {220, 180}, {220, 760}, {0, 800}});
  }
  return m;
}

}  // namespace cb::assets
