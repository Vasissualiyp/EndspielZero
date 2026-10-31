// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/move_anim.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cb::view {
namespace {

/// A slider's ray has no length limit of its own; this bounds the search for a route.
/// A ray can legitimately be longer than the board is wide - a rook on a Moebius band
/// needs two circuits to come home - so it is generous rather than tight.
constexpr std::uint32_t kRouteCap = 512;

/// Would the next step stay inside the box? The same test the geometry's fast path
/// makes, repeated here because the tracer needs to *know* a seam was crossed, and
/// step() only reports whether the ray survived.
bool interiorNext(const DimSpec& dims, const Walker& w) {
  for (std::uint8_t k = 0; k < w.dir.nsup; ++k) {
    const std::uint8_t a = w.dir.sup[k];
    const int nv = w.coord.c[a] + w.dir.v[a];
    if (nv < 0 || nv >= dims.extent(a)) return false;
  }
  return true;
}

/// The first face a boundary step goes through, and what kind it is.
///
/// A boundary step is the same event whether the face glues or reflects - only the
/// declaration says which - and a leaper can hit a mirror without landing beside where
/// it started, so adjacency cannot stand in for the answer the way it can for a slider.
/// The crossed axis is also what orients the portal: a diagonal move can leave through
/// the file edge while its direction leans along the rank.
struct CrossedFace {
  std::uint8_t axis{0};
  Side side{Side::Max};
  BoundaryKind kind{BoundaryKind::Open};
};

CrossedFace firstCrossedFace(const VariantSpec& v, const Walker& before) {
  for (std::uint8_t k = 0; k < before.dir.nsup; ++k) {
    const std::uint8_t a = before.dir.sup[k];
    const int nv = before.coord.c[a] + before.dir.v[a];
    if (nv < 0 || nv >= v.dims.extent(a)) {
      const Side side = nv < 0 ? Side::Min : Side::Max;
      return CrossedFace{a, side, v.geom.boundaryKind(a, side)};
    }
  }
  return {};
}

/// The one easing every flourish uses: smoothstep, the same shape the move camera's shot
/// envelope is built from, so no second curve vocabulary appears in the animation.
float smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace

float captureFade(float progress) noexcept {
  // Whole through the first fifth of the move, gone four fifths of the way in - the last
  // stretch belongs to the capturing piece's arrival, not to the piece that is leaving.
  const float begin = 0.18f;
  const float end = 0.82f;
  if (progress <= begin) return 1.0f;
  if (progress >= end) return 0.0f;
  return 1.0f - smoothstep((progress - begin) / (end - begin));
}

float captureFlash(float progress) noexcept {
  // A short pulse that rises as the pieces close and is spent by the time the move lands.
  const float begin = 0.50f;
  const float peak = 0.80f;
  const float end = 1.0f;
  if (progress <= begin || progress >= end) return 0.0f;
  if (progress < peak) return smoothstep((progress - begin) / (peak - begin));
  return smoothstep((end - progress) / (end - peak));
}

MovePath tracePath(const VariantSpec& v, const Position& pos, PieceTypeId type,
                   Color side, const Move& m) {
  MovePath best;
  best.from = m.from;
  best.to = m.to;
  best.unexplained = true;
  // What the move took, recorded before the position is allowed to move on: the renderer
  // draws the captured piece's flourish from here, and it is `captureCell` (d5 for an en
  // passant) rather than `to` (d6) that says where it stood.
  best.captureCell = m.captureCell;
  if (m.captureCell != kInvalidCell) best.captured = pos.at(m.captureCell);

  if (type == kNoPiece || type >= v.pieces.size()) return best;
  const PieceTypeDef& piece = v.pieces[type];
  const auto c = static_cast<std::size_t>(side);

  for (const MoveAtom& atom : piece.atoms) {
    const std::uint32_t limit =
        atom.maxK == kUnlimited ? kRouteCap : std::min(atom.maxK, kRouteCap);
    for (std::uint32_t di = atom.dirBegin[c]; di < atom.dirEnd[c]; ++di) {
      if (di >= v.dirTable.size()) break;
      const Direction& d0 = v.dirTable[di];
      Walker w = v.geom.start(m.from, d0);
      std::vector<PathStep> steps;

      for (std::uint32_t k = 1; k <= limit; ++k) {
        const CellId before = w.cell;
        const bool wasInterior = interiorNext(v.dims, w);
        const Walker pre = w;
        if (!v.geom.step(w)) break;

        PathStep s;
        s.from = before;
        s.to = w.cell;
        s.dir = w.dir;
        // A boundary step is a bounce or a portal according to the face it crossed:
        // a reflecting wall turns the ray around, a gluing sends it somewhere else.
        // Asking the geometry is the only way to get a leaper right - a knight off a
        // mirror lands one or two cells away, not next door, and used to be drawn as a
        // portal opening in the middle of the board. The face is kept so the portal can
        // be oriented by the edge actually crossed rather than by the travel direction.
        if (wasInterior) {
          s.kind = StepKind::Interior;
        } else {
          const CrossedFace face = firstCrossedFace(v, pre);
          s.faceAxis = face.axis;
          s.faceSide = face.side;
          s.kind =
              face.kind == BoundaryKind::Mirror ? StepKind::Bounce : StepKind::Portal;
        }
        steps.push_back(s);

        if (w.cell == m.to && k >= atom.minK) {
          // The atom has to be allowed to make *this* move: a pawn's push cannot land on
          // an occupied square, and its capture must. Without this a pawn on a mirror
          // took the push that reached the same square the reflected capture did, and the
          // animation showed a straight step instead of a bounce.
          //
          // What is taken is `captureCell`, not `to`. Those are the same square for
          // every ordinary capture and different for exactly one move - en passant, where
          // the square landed on is empty. Reading `to` made the capture atom look
          // illegal, left the move unexplained, and handed the animation a route with no
          // steps in it.
          const CellId taken = m.captureCell != kInvalidCell ? m.captureCell : m.to;
          const bool occupied = !pos.at(taken).empty();
          const bool allowed = atom.capture == CapturePolicy::May ||
                               (atom.capture == CapturePolicy::Must && occupied) ||
                               (atom.capture == CapturePolicy::Cannot && !occupied);
          if (allowed && (best.unexplained || steps.size() < best.steps.size())) {
            best.unexplained = false;
            best.leap = atom.mode != MoveMode::Slide;
            best.startDir = d0;
            best.steps = steps;
          }
          break;
        }
        // A slide cannot pass through a piece: the move a player saw is the clear route,
        // and on a glued board there is usually one - the straight ray is blocked, so the
        // piece went the long way round through a seam. A leap never entered the cells
        // between, so occupancy does not apply to it.
        if (atom.mode == MoveMode::Slide && !pos.at(w.cell).empty()) break;
        // A ray that has come back to where it started has covered everything it can.
        if (w.cell == m.from && w.dir == d0) break;
      }
    }
  }
  return best;
}

void MoveAnimation::clear() {
  active_ = false;
  elapsed_ = 0;
  runs_.clear();
  runStart_.clear();
  runEnd_.clear();
  portals_.clear();
  to_ = kInvalidCell;
  captureCell_ = kInvalidCell;
  captured_ = Piece{};
}

void MoveAnimation::start(const ViewConfig& cfg, const std::vector<Placement>& placements,
                          const SeamMap& seams, const Theme& theme, const MovePath& path,
                          float secondsPerCell) {
  clear();
  if (path.from == kInvalidCell || path.to == kInvalidCell) return;
  if (secondsPerCell <= 0.0f) return;
  path_ = path;  // the route, for anything that samples it on another placement (M17.15)
  captureCell_ = path.captureCell;
  captured_ = path.captured;

  // Gather the world positions this route needs in one pass over the placements, rather
  // than building a cell-indexed table: a move touches a handful of cells and the
  // lattice can have millions.
  std::vector<CellId> wanted{path.from};
  for (const PathStep& s : path.steps) wanted.push_back(s.to);
  std::vector<Placement> found(wanted.size());
  std::vector<bool> got(wanted.size(), false);
  float minX = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float minY = minX;
  float maxY = maxX;
  for (const Placement& p : placements) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
    for (std::size_t i = 0; i < wanted.size(); ++i) {
      if (!got[i] && p.cell == wanted[i]) {
        found[i] = p;
        got[i] = true;
      }
    }
  }
  for (bool g : got) {
    if (!g) return;  // a cell the view does not draw; nothing sensible to animate
  }

  const auto worldDir = [&](const Direction& d) {
    float out[3]{0, 0, 0};
    for (std::size_t i = 0; i < cfg.screenAxes.size() && i < 3; ++i) {
      const float scale = i == 2 ? cfg.depthSpacing : 1.0f;
      out[i] = static_cast<float>(d.v[cfg.screenAxes[i]]) * scale;
    }
    const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 0) {
      out[0] /= len;
      out[1] /= len;
      out[2] /= len;
    }
    return std::array<float, 3>{out[0], out[1], out[2]};
  };

  // Where the piece's ray meets the edge of the board. A sliding step leaves from the
  // face of its own cell, but a leap crosses the boundary off to one side of the cell it
  // started on and a little higher or lower. Opening the portal at that crossing is what
  // keeps it on the edge, where the seam is, instead of in the middle of the board.
  const auto leavePoint = [&](float ox, float oy, const std::array<float, 3>& d) {
    float t = -1.0f;
    const auto consider = [&](float comp, float origin, float lo, float hi) {
      if (std::abs(comp) < 1e-4f) return;
      const float bound = comp > 0 ? hi + 0.5f : lo - 0.5f;
      const float ti = (bound - origin) / comp;
      if (ti > 0 && (t < 0 || ti < t)) t = ti;
    };
    consider(d[0], ox, minX, maxX);
    consider(d[1], oy, minY, maxY);
    if (t < 0) t = 0.5f;
    return std::array<float, 2>{ox + d[0] * t, oy + d[1] * t};
  };
  const auto enterPoint = [&](float ox, float oy, const std::array<float, 3>& d) {
    float t = -1.0f;
    const auto consider = [&](float comp, float origin, float lo, float hi) {
      if (std::abs(comp) < 1e-4f) return;
      const float bound = comp > 0 ? lo - 0.5f : hi + 0.5f;
      const float ti = (origin - bound) / comp;
      if (ti > 0 && (t < 0 || ti < t)) t = ti;
    };
    consider(d[0], ox, minX, maxX);
    consider(d[1], oy, minY, maxY);
    if (t < 0) t = 0.5f;
    return std::array<float, 2>{ox - d[0] * t, oy - d[1] * t};
  };

  // The drawn index of a board axis, so a portal can be put on the face the step
  // actually crossed rather than on whichever axis the travel direction leans along.
  const auto screenIndexOf = [&](std::uint8_t axis) -> int {
    for (std::size_t i = 0; i < cfg.screenAxes.size(); ++i) {
      if (cfg.screenAxes[i] == axis) return static_cast<int>(i);
    }
    return -1;
  };
  const float lo[2] = {minX, minY};
  const float hi[2] = {maxX, maxY};

  runs_.emplace_back();
  const auto append = [&](float x, float y, float z) {
    Run& r = runs_.back();
    r.x.push_back(x);
    r.y.push_back(y);
    r.z.push_back(z);
  };
  append(found[0].x, found[0].y, found[0].z);

  Direction incoming = path.startDir;
  for (std::size_t i = 0; i < path.steps.size(); ++i) {
    const PathStep& s = path.steps[i];
    const Placement& src = found[i];
    const Placement& dst = found[i + 1];
    const auto out = worldDir(incoming);

    if (s.kind == StepKind::Portal) {
      // Leave through the face, and open the far portal at the same instant: seeing
      // both at once is the only way a player learns the identification. The portal
      // faces the *crossed edge*, which for a diagonal move is not the axis the travel
      // direction leans along - a king leaving through the file edge opens a portal
      // across the file even if it is climbing faster than it is running.
      view::Rgba color = seamRampColor(theme, 0.5f);
      for (const SeamFace& f : seams.at(s.from)) {
        if (f.partner == s.to) color = f.color;
      }
      const int axisIndex = screenIndexOf(s.faceAxis);
      const bool haveAxis = axisIndex >= 0 && axisIndex < 3;
      const bool crossable = axisIndex >= 0 && axisIndex < 2;
      const auto si = static_cast<std::size_t>(axisIndex);

      const auto in = worldDir(s.dir);
      float exitN[3]{0, 0, 0};
      float entryN[3]{0, 0, 0};
      if (haveAxis) {
        exitN[si] = s.faceSide == Side::Max ? 1.0f : -1.0f;
        entryN[si] = -exitN[si];
      } else {
        exitN[0] = out[0];
        exitN[1] = out[1];
        exitN[2] = out[2];
        entryN[0] = in[0];
        entryN[1] = in[1];
        entryN[2] = in[2];
      }

      float ex;
      float ey;
      if (crossable && std::abs(out[si]) > 1e-4f) {
        const float ox = si == 0 ? src.x : src.y;
        const float plane = s.faceSide == Side::Max ? hi[si] + 0.5f : lo[si] - 0.5f;
        const float t = (plane - ox) / out[si];
        ex = src.x + out[0] * t;
        ey = src.y + out[1] * t;
      } else {
        const auto p = leavePoint(src.x, src.y, out);
        ex = p[0];
        ey = p[1];
      }
      const float ez = src.z + out[2] * 0.5f;
      append(ex, ey, ez);
      portals_.push_back(Portal{ex, ey, ez, exitN[0], exitN[1], exitN[2], color, 0, 0});

      float nx;
      float ny;
      if (crossable && std::abs(in[si]) > 1e-4f) {
        const float ox = si == 0 ? dst.x : dst.y;
        // The far end of the same identification: it re-enters from the opposite face.
        const float plane = s.faceSide == Side::Max ? lo[si] - 0.5f : hi[si] + 0.5f;
        const float t = (ox - plane) / in[si];
        nx = dst.x - in[0] * t;
        ny = dst.y - in[1] * t;
      } else {
        const auto p = enterPoint(dst.x, dst.y, in);
        nx = p[0];
        ny = p[1];
      }
      const float nz = dst.z - in[2] * 0.5f;
      portals_.push_back(
          Portal{nx, ny, nz, entryN[0], entryN[1], entryN[2], color, 0, 0});
      runs_.emplace_back();
      append(nx, ny, nz);
      append(dst.x, dst.y, dst.z);
    } else if (s.kind == StepKind::Bounce) {
      // The wall is invisible and exactly where the board stops. Reaching it and being
      // turned around is the only honest picture of what a reflection does - and for a
      // leap it is the one case where the piece visibly touches the edge. The turn is
      // put on the crossed face, so it lands on the wall even when the move leans away
      // from it.
      const int axisIndex = screenIndexOf(s.faceAxis);
      const auto u = static_cast<std::size_t>(axisIndex);
      float bx;
      float by;
      if (axisIndex >= 0 && axisIndex < 2 && std::abs(out[u]) > 1e-4f) {
        const float ox = u == 0 ? src.x : src.y;
        const float plane = s.faceSide == Side::Max ? hi[u] + 0.5f : lo[u] - 0.5f;
        const float t = (plane - ox) / out[u];
        bx = src.x + out[0] * t;
        by = src.y + out[1] * t;
      } else {
        bx = src.x + out[0] * 0.5f;
        by = src.y + out[1] * 0.5f;
      }
      append(bx, by, src.z + out[2] * 0.5f);
      append(dst.x, dst.y, dst.z);
    } else {
      append(dst.x, dst.y, dst.z);
    }
    incoming = s.dir;
  }

  // A route nothing explained - a castle, a rule effect, a move whose atom was edited
  // out from under a running game - still has to be drawn. It glides straight there.
  // Leaving it as the single starting point would hand sample() a polyline with nothing
  // to interpolate along, which is a crash rather than a still piece.
  if (runs_.size() == 1 && runs_.back().x.size() < 2) {
    const Placement& dst = found.back();
    append(dst.x, dst.y, dst.z);
  }
  for (const Run& run : runs_) {
    if (run.x.size() < 2) {
      clear();
      return;
    }
  }

  // Time is handed out by distance travelled, with a fixed pause for each crossing, so
  // a one-square step and a queen's run across the board move at the same speed.
  std::vector<float> length(runs_.size(), 0.0f);
  float total = 0;
  for (std::size_t r = 0; r < runs_.size(); ++r) {
    const Run& run = runs_[r];
    for (std::size_t i = 1; i < run.x.size(); ++i) {
      const float dx = run.x[i] - run.x[i - 1];
      const float dy = run.y[i] - run.y[i - 1];
      const float dz = run.z[i] - run.z[i - 1];
      length[r] += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    total += length[r];
  }
  leap_ = path.leap;
  float pause = 0.22f;
  const int crossings = static_cast<int>(runs_.size()) - 1;
  // Cap the total near the current duration: a long move shrinks its per-cell time and
  // its portal pauses together instead of running long (M23.9 D).
  const float raw =
      std::max(0.08f, total * secondsPerCell) + static_cast<float>(crossings) * pause;
  if (raw > kMaxMoveSeconds) {
    const float k = kMaxMoveSeconds / raw;
    secondsPerCell *= k;
    pause *= k;
  }
  duration_ =
      std::max(0.08f, total * secondsPerCell) + static_cast<float>(crossings) * pause;

  float at = 0;
  runStart_.resize(runs_.size());
  runEnd_.resize(runs_.size());
  std::size_t portalIndex = 0;
  for (std::size_t r = 0; r < runs_.size(); ++r) {
    runStart_[r] = at;
    at += total > 0
              ? (length[r] / total) * (duration_ - static_cast<float>(crossings) * pause)
              : (duration_ - static_cast<float>(crossings) * pause);
    runEnd_[r] = at;
    if (r + 1 < runs_.size()) {
      // Both ends of this crossing are fully open in the middle of the pause.
      if (portalIndex + 1 < portals_.size()) {
        portals_[portalIndex].openAt = at + pause * 0.5f;
        portals_[portalIndex + 1].openAt = at + pause * 0.5f;
        portalIndex += 2;
      }
      at += pause;
    }
  }
  to_ = path.to;
  active_ = true;
}

void MoveAnimation::advance(float dt) {
  if (!active_) return;
  elapsed_ += dt;
  if (elapsed_ >= duration_) clear();
}

void MoveAnimation::setProgress(float t) noexcept {
  if (!active_) return;
  const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  elapsed_ = clamped * duration_;
}

MoveAnimation::Sample MoveAnimation::sample() const {
  Sample out;
  if (!active_ || runs_.empty()) return out;
  out.moving = true;
  const float t = std::clamp(elapsed_, 0.0f, duration_);

  for (std::size_t r = 0; r < runs_.size(); ++r) {
    const Run& run = runs_[r];
    if (run.x.empty()) continue;
    const bool last = r + 1 == runs_.size();
    if (t > runEnd_[r] && !last) {
      if (t < runStart_[r + 1]) {
        // Inside a crossing: the piece sinks out of this end and rises from the other.
        const float span = std::max(1e-4f, runStart_[r + 1] - runEnd_[r]);
        const float u = (t - runEnd_[r]) / span;
        const Run& next = runs_[r + 1];
        const bool second = u > 0.5f;
        const float x = second ? next.x.front() : run.x.back();
        const float y = second ? next.y.front() : run.y.back();
        const float z = second ? next.z.front() : run.z.back();
        out.x = x;
        out.y = y;
        out.z = z;
        out.lift = -0.42f * (second ? (1.0f - (u - 0.5f) * 2.0f) : u * 2.0f);
        return out;
      }
      continue;
    }
    if (t > runEnd_[r] && last) {
      out.x = run.x.back();
      out.y = run.y.back();
      out.z = run.z.back();
      return out;
    }

    // Walk the polyline by arc length.
    if (run.x.size() < 2) {
      out.x = run.x.front();
      out.y = run.y.front();
      out.z = run.z.front();
      return out;
    }
    float len = 0;
    std::vector<float> cum(run.x.size(), 0.0f);
    for (std::size_t i = 1; i < run.x.size(); ++i) {
      const float dx = run.x[i] - run.x[i - 1];
      const float dy = run.y[i] - run.y[i - 1];
      const float dz = run.z[i] - run.z[i - 1];
      len += std::sqrt(dx * dx + dy * dy + dz * dz);
      cum[i] = len;
    }
    const float span = std::max(1e-4f, runEnd_[r] - runStart_[r]);
    const float u = std::clamp((t - runStart_[r]) / span, 0.0f, 1.0f);
    // Ease in and out: a piece that starts and stops abruptly reads as a teleport with
    // extra steps.
    const float eased = u * u * (3.0f - 2.0f * u);
    const float want = eased * len;
    std::size_t i = 1;
    while (i + 1 < cum.size() && cum[i] < want) ++i;
    const float segLen = std::max(1e-5f, cum[i] - cum[i - 1]);
    const float f = std::clamp((want - cum[i - 1]) / segLen, 0.0f, 1.0f);
    out.x = run.x[i - 1] + (run.x[i] - run.x[i - 1]) * f;
    out.y = run.y[i - 1] + (run.y[i] - run.y[i - 1]) * f;
    out.z = run.z[i - 1] + (run.z[i] - run.z[i - 1]) * f;
    if (leap_) {
      const float whole = std::clamp(elapsed_ / duration_, 0.0f, 1.0f);
      out.lift = std::sin(whole * 3.14159265f) * 0.65f;
    }
    return out;
  }
  return out;
}

std::vector<MoveAnimation::Portal> MoveAnimation::openPortals() const {
  std::vector<Portal> out;
  if (!active_) return out;
  for (const Portal& p : portals_) {
    const float d = std::abs(elapsed_ - p.openAt);
    const float window = 0.34f;
    if (d >= window) continue;
    Portal live = p;
    live.intensity = 1.0f - d / window;
    out.push_back(live);
  }
  return out;
}

namespace {
float smooth01(float a, float b, float x) {
  if (b <= a) return x >= b ? 1.0f : 0.0f;
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
}  // namespace

HandoffState handoffState(const MovePath& path, float progress, float intensity) {
  HandoffState out;
  progress = std::clamp(progress, 0.0f, 1.0f);
  std::vector<CellId> route;
  route.push_back(path.from);
  const bool direct =
      path.leap || path.unexplained || path.steps.empty() || intensity <= 0.0f;
  if (!direct) {
    for (const PathStep& s : path.steps)
      if (s.to != route.back()) route.push_back(s.to);
  }
  if (route.back() != path.to) route.push_back(path.to);
  const auto hops = static_cast<float>(route.size() - 1);
  if (hops < 1.0f) return out;
  const float h = kHandoffSpan / hops;  // progress per hop
  const float rise = std::min(kHandoffMaxRise, 0.25f * h);
  // Solid head, ghosts behind: at every moment exactly one copy is solid - the head, on
  // the newest cell reached. Copy `j` takes over at `a_j` with a very short rise (well
  // under a hop); the copy it left drops to ghost level and decays over about two hops.
  // Nothing is ever drawn ahead of the head, and the destination is solid on landing.
  for (std::size_t j = 0; j < route.size(); ++j) {
    const float a = h * static_cast<float>(j);  // the head lands here
    const bool origin = j == 0;
    const bool dest = j + 1 == route.size();
    HandoffCopy copy;
    copy.cell = route[j];
    copy.destination = dest;
    copy.opacity = origin ? 1.0f : smooth01(a, a + rise, progress);
    if (!dest) {
      // The next head takes over at a + h; this copy leaves after that crossfade starts.
      const float left = a + h + 0.5f * rise;
      const float drop = smooth01(left, left + rise, progress);
      const float decay =
          smooth01(left + rise, std::min(left + 2.0f * h, 1.0f), progress);
      copy.opacity *= (1.0f - (1.0f - kHandoffGhostOpacity) * drop) * (1.0f - decay);
      copy.scan = kHandoffGhostScan * drop;
    }
    if (copy.opacity > 0.002f) out.copies.push_back(copy);

    // Heat: warms from two hops ahead of the head to full half a hop ahead, then (behind
    // it) cools with an afterglow, always back to nothing - or the footprint - by
    // progress
    // 1. It rises ahead of the solid piece: the base warms, then the piece appears.
    float heat;
    if (origin) {
      heat =
          kFootprintHeat + (0.70f - kFootprintHeat) * smooth01(0.0f, 0.5f * h, progress);
      heat *= 1.0f - smooth01(0.5f * h, std::min(a + 2.5f * h, 1.0f), progress);
    } else if (dest) {
      heat = smooth01(std::max(a - 2.0f * h, 0.0f), a - 0.6f * h, progress);
      heat = heat + (kFootprintHeat - heat) * smooth01(a, 1.0f, progress);
    } else {
      heat = smooth01(std::max(a - 2.0f * h, 0.0f), a - 0.6f * h, progress) *
             (1.0f - smooth01(a + 0.5f * h, std::min(a + 2.5f * h, 1.0f), progress));
    }
    if (heat > 0.002f) out.heat.push_back({route[j], std::min(heat, 1.0f)});
  }
  return out;
}

CapturePulse capturePulse(float progress, float durationSeconds) noexcept {
  CapturePulse out;
  if (progress < kCaptureContact) return out;
  out.active = true;
  out.held = progress < kCaptureRelease;
  const float since = progress - (out.held ? kCaptureContact : kCaptureRelease);
  out.age = std::max(0.0f, since * durationSeconds * kCapturePulseSpeed);
  return out;
}

}  // namespace cb::view
