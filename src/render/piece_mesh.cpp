// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/piece_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "render/piece_icon.hpp"

namespace cb::render {
namespace {

constexpr int kSegments = 14;  // smooth enough at board scale, cheap enough to be free

/// The flat view draws its token circles with 24 segments; the shape view's token discs
/// use the same count so the two read as the same round object at the same angle
/// (M17.26).
constexpr int kDiscSegments = 24;

void pushTri(MeshLibrary& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  m.indices.push_back(static_cast<std::uint16_t>(a));
  m.indices.push_back(static_cast<std::uint16_t>(b));
  m.indices.push_back(static_cast<std::uint16_t>(c));
}

std::uint32_t pushVertexColor(MeshLibrary& m, float x, float y, float z, float nx,
                              float ny, float nz, float h, const float rgba[4]) {
  const auto index = static_cast<std::uint32_t>(m.vertices.size());
  m.vertices.push_back(
      MeshVertex{{x, y, z}, {nx, ny, nz}, h, {rgba[0], rgba[1], rgba[2], rgba[3]}});
  return index;
}

std::uint32_t pushVertex(MeshLibrary& m, float x, float y, float z, float nx, float ny,
                         float nz, float h) {
  const float white[4]{1.0f, 1.0f, 1.0f, 1.0f};
  return pushVertexColor(m, x, y, z, nx, ny, nz, h, white);
}

/// A truncated cone, which covers every round part of every piece: a disc when the
/// radii match, a spike when the top radius is zero.
void addCone(MeshLibrary& m, std::int32_t base, float z0, float z1, float r0, float r1,
             float totalHeight, bool capTop) {
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const float slope = std::atan2(r0 - r1, z1 - z0);
  const float nz = std::sin(slope);
  const float nr = std::cos(slope);

  for (int i = 0; i < kSegments; ++i) {
    const float a0 = tau * static_cast<float>(i) / kSegments;
    const float a1 = tau * static_cast<float>(i + 1) / kSegments;
    const float c0 = std::cos(a0);
    const float s0 = std::sin(a0);
    const float c1 = std::cos(a1);
    const float s1 = std::sin(a1);

    const std::uint32_t v0 =
        pushVertex(m, c0 * r0, s0 * r0, z0, c0 * nr, s0 * nr, nz, z0 / totalHeight);
    const std::uint32_t v1 =
        pushVertex(m, c1 * r0, s1 * r0, z0, c1 * nr, s1 * nr, nz, z0 / totalHeight);
    const std::uint32_t v2 =
        pushVertex(m, c1 * r1, s1 * r1, z1, c1 * nr, s1 * nr, nz, z1 / totalHeight);
    const std::uint32_t v3 =
        pushVertex(m, c0 * r1, s0 * r1, z1, c0 * nr, s0 * nr, nz, z1 / totalHeight);
    pushTri(m, v0 - static_cast<std::uint32_t>(base),
            v1 - static_cast<std::uint32_t>(base), v2 - static_cast<std::uint32_t>(base));
    pushTri(m, v0 - static_cast<std::uint32_t>(base),
            v2 - static_cast<std::uint32_t>(base), v3 - static_cast<std::uint32_t>(base));
  }

  if (capTop && r1 > 0.0f) {
    const std::uint32_t centre = pushVertex(m, 0, 0, z1, 0, 0, 1, z1 / totalHeight);
    for (int i = 0; i < kSegments; ++i) {
      const float a0 = tau * static_cast<float>(i) / kSegments;
      const float a1 = tau * static_cast<float>(i + 1) / kSegments;
      const std::uint32_t v0 = pushVertex(m, std::cos(a0) * r1, std::sin(a0) * r1, z1, 0,
                                          0, 1, z1 / totalHeight);
      const std::uint32_t v1 = pushVertex(m, std::cos(a1) * r1, std::sin(a1) * r1, z1, 0,
                                          0, 1, z1 / totalHeight);
      pushTri(m, centre - static_cast<std::uint32_t>(base),
              v0 - static_cast<std::uint32_t>(base),
              v1 - static_cast<std::uint32_t>(base));
    }
  }
}

void addBox(MeshLibrary& m, std::int32_t base, float cx, float cy, float cz, float hx,
            float hy, float hz, float totalHeight) {
  const float faces[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                             {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
  for (const auto& n : faces) {
    float a[3]{0, 0, 0};
    float b[3]{0, 0, 0};
    if (n[0] != 0) {
      a[1] = 1;
      b[2] = 1;
    } else if (n[1] != 0) {
      a[0] = 1;
      b[2] = 1;
    } else {
      a[0] = 1;
      b[1] = 1;
    }
    const float half[3]{hx, hy, hz};
    const float centre[3]{cx, cy, cz};
    const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    std::uint32_t quad[4]{};
    for (int k = 0; k < 4; ++k) {
      float p[3];
      for (int axis = 0; axis < 3; ++axis) {
        p[axis] = centre[axis] + n[axis] * half[axis] +
                  a[axis] * signs[k][0] * half[axis] + b[axis] * signs[k][1] * half[axis];
      }
      quad[k] = pushVertex(m, p[0], p[1], p[2], n[0], n[1], n[2], p[2] / totalHeight);
    }
    const auto off = static_cast<std::uint32_t>(base);
    pushTri(m, quad[0] - off, quad[1] - off, quad[2] - off);
    pushTri(m, quad[0] - off, quad[2] - off, quad[3] - off);
  }
}

/// A quarter of a disc in the XY plane, from +X round to +Y: the rounded corner that
/// bends a timeline's connector into its rail. One mesh serves all four quadrants, by
/// instance roll. Sized like the cell, so an accessory's scale is a wanted half-extent
/// over the cell's.
void addQuarterDisc(MeshLibrary& m, std::int32_t base, float r, float hz, int segments) {
  const auto off = static_cast<std::uint32_t>(base);
  const auto put = [&](float x, float y, float z, float nx, float ny, float nz) {
    // Height matched to the cell slab's, so an accessory shades with its rail
    // rather than a shade brighter.
    return pushVertex(m, x, y, z, nx, ny, nz, z);
  };
  const float pie = 0.5f * std::numbers::pi_v<float>;
  // The two flat faces: the top and the bottom of the quarter.
  for (int side = 0; side < 2; ++side) {
    const float z = side == 0 ? hz : -hz;
    const float nz = side == 0 ? 1.0f : -1.0f;
    const std::uint32_t centre = put(0, 0, z, 0, 0, nz);
    for (int i = 0; i < segments; ++i) {
      const float a0 = pie * static_cast<float>(i) / static_cast<float>(segments);
      const float a1 = pie * static_cast<float>(i + 1) / static_cast<float>(segments);
      const std::uint32_t v0 = put(r * std::cos(a0), r * std::sin(a0), z, 0, 0, nz);
      const std::uint32_t v1 = put(r * std::cos(a1), r * std::sin(a1), z, 0, 0, nz);
      if (side == 0) {
        pushTri(m, centre - off, v0 - off, v1 - off);
      } else {
        pushTri(m, centre - off, v1 - off, v0 - off);
      }
    }
  }
  // The curved rim.
  for (int i = 0; i < segments; ++i) {
    const float a0 = pie * static_cast<float>(i) / static_cast<float>(segments);
    const float a1 = pie * static_cast<float>(i + 1) / static_cast<float>(segments);
    const float c0 = std::cos(a0);
    const float s0 = std::sin(a0);
    const float c1 = std::cos(a1);
    const float s1 = std::sin(a1);
    const float nx = (c0 + c1) * 0.5f;
    const float ny = (s0 + s1) * 0.5f;
    const std::uint32_t v0 = put(c0 * r, s0 * r, hz, nx, ny, 0);
    const std::uint32_t v1 = put(c1 * r, s1 * r, hz, nx, ny, 0);
    const std::uint32_t v2 = put(c1 * r, s1 * r, -hz, nx, ny, 0);
    const std::uint32_t v3 = put(c0 * r, s0 * r, -hz, nx, ny, 0);
    pushTri(m, v0 - off, v1 - off, v2 - off);
    pushTri(m, v0 - off, v2 - off, v3 - off);
  }
  // The two radial walls, so the solid is closed.
  for (int side = 0; side < 2; ++side) {
    const float ex = side == 0 ? 1.0f : 0.0f;
    const float ey = side == 0 ? 0.0f : 1.0f;
    const float nx = side == 0 ? 0.0f : -1.0f;
    const float ny = side == 0 ? -1.0f : 0.0f;
    const std::uint32_t v0 = put(0, 0, hz, nx, ny, 0);
    const std::uint32_t v1 = put(ex * r, ey * r, hz, nx, ny, 0);
    const std::uint32_t v2 = put(ex * r, ey * r, -hz, nx, ny, 0);
    const std::uint32_t v3 = put(0, 0, -hz, nx, ny, 0);
    pushTri(m, v0 - off, v1 - off, v2 - off);
    pushTri(m, v0 - off, v2 - off, v3 - off);
  }
}

/// A flat disc whose vertex alpha holds near the centre and falls to nothing at the rim -
/// the contact shadow (M18.2). The falloff is data, not a shader branch: the existing
/// shader multiplies the vertex colour into the instance colour, the same channel the
/// geometry view's ghost board uses, so a soft edge costs no branch and no second
/// pipeline. Radius 0.5, like the portal cube, so an instance scale of 0.68 wants a world
/// radius of 0.34 - a touch wider than a piece's own foot (about 0.27), which is what
/// lets the contact show around the base instead of hiding under it.
void addShadowDisc(MeshLibrary& m, std::int32_t base) {
  const auto off = static_cast<std::uint32_t>(base);
  constexpr float kRadius = 0.5f;
  constexpr int kRings = 3;
  // Hold the decal nearly solid out to this fraction of the radius, then ease to zero, so
  // what a piece does not cover still reads as a shadow with a soft, not hard, edge.
  constexpr float kHold = 0.55f;
  const auto alphaAt = [](float u) {
    if (u <= kHold) return 1.0f;
    const float s = (u - kHold) / (1.0f - kHold);
    return 1.0f - s * s * (3.0f - 2.0f * s);
  };
  const auto put = [&](float x, float y, float a) {
    const float col[4]{1.0f, 1.0f, 1.0f, a};
    // Height 0: the shadow shades with the board's foot rather than a shade brighter.
    return pushVertexColor(m, x, y, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, col);
  };
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const std::uint32_t centre = put(0.0f, 0.0f, 1.0f);
  std::uint32_t prev[kSegments];
  for (int ring = 1; ring <= kRings; ++ring) {
    const float u = static_cast<float>(ring) / kRings;
    const float radius = kRadius * u;
    const float alpha = alphaAt(u);
    std::uint32_t cur[kSegments];
    for (int i = 0; i < kSegments; ++i) {
      const float a = tau * static_cast<float>(i) / kSegments;
      cur[i] = put(std::cos(a) * radius, std::sin(a) * radius, alpha);
    }
    for (int i = 0; i < kSegments; ++i) {
      const int j = (i + 1) % kSegments;
      if (ring == 1) {
        pushTri(m, centre - off, cur[i] - off, cur[j] - off);
      } else {
        pushTri(m, prev[i] - off, prev[j] - off, cur[j] - off);
        pushTri(m, prev[i] - off, cur[j] - off, cur[i] - off);
      }
    }
    for (int i = 0; i < kSegments; ++i) prev[i] = cur[i];
  }
}

void addTriPrism(MeshLibrary& m, std::int32_t base, float hx, float hy, float hz,
                 float totalHeight) {
  const float tri[3][2] = {{hx, 0.0f}, {-hx, hy}, {-hx, -hy}};
  const auto put = [&](float x, float y, float z, float nx, float ny, float nz) {
    // Height matched to the cell slab's, so an accessory shades with its rail
    // rather than a shade brighter.
    return pushVertex(m, x, y, z, nx, ny, nz, z);
  };
  const auto off = static_cast<std::uint32_t>(base);
  for (int side = 0; side < 2; ++side) {
    const float z = side == 0 ? hz : -hz;
    const float nz = side == 0 ? 1.0f : -1.0f;
    const std::uint32_t a = put(tri[0][0], tri[0][1], z, 0.0f, 0.0f, nz);
    const std::uint32_t b = put(tri[1][0], tri[1][1], z, 0.0f, 0.0f, nz);
    const std::uint32_t c = put(tri[2][0], tri[2][1], z, 0.0f, 0.0f, nz);
    if (side == 0) {
      pushTri(m, a - off, b - off, c - off);
    } else {
      pushTri(m, a - off, c - off, b - off);
    }
  }
  for (int i = 0; i < 3; ++i) {
    const int j = (i + 1) % 3;
    const float x0 = tri[i][0];
    const float y0 = tri[i][1];
    const float x1 = tri[j][0];
    const float y1 = tri[j][1];
    float nx = y1 - y0;
    float ny = -(x1 - x0);
    const float len = std::sqrt(nx * nx + ny * ny);
    if (len > 0) {
      nx /= len;
      ny /= len;
    }
    const std::uint32_t v0 = put(x0, y0, hz, nx, ny, 0.0f);
    const std::uint32_t v1 = put(x1, y1, hz, nx, ny, 0.0f);
    const std::uint32_t v2 = put(x1, y1, -hz, nx, ny, 0.0f);
    const std::uint32_t v3 = put(x0, y0, -hz, nx, ny, 0.0f);
    pushTri(m, v0 - off, v1 - off, v2 - off);
    pushTri(m, v0 - off, v2 - off, v3 - off);
  }
  (void)totalHeight;
}

/// Twice the signed area of a triangle, in the icon's own plane.
float cross2Icon(const IconPoint& o, const IconPoint& a, const IconPoint& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/// Ear-clip a simple polygon into triangles, indices into `pts`. The same algorithm as
/// `widgets::triangulate`, but on `IconPoint` so the mesh library keeps no ImGui
/// dependency. A fan from the first vertex is *not* enough here: the Primitive set is
/// mostly convex but not entirely - the crown's body and its zig-zag teeth are concave -
/// and a fan would fill their valleys and leave a flange where the rim does not.
std::vector<int> earClipIcon(const std::vector<IconPoint>& pts) {
  const int n = static_cast<int>(pts.size());
  std::vector<int> out;
  if (n < 3) return out;
  std::vector<int> poly(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) poly[static_cast<std::size_t>(i)] = i;
  // Work anticlockwise whichever way the outline was wound; an outline is a shape, not a
  // direction, and both windings have to fill the same - and the cap's own triangle
  // winding takes its normal from that choice.
  float area2 = 0.0f;
  for (int i = 0; i < n; ++i) {
    const IconPoint& a = pts[static_cast<std::size_t>(i)];
    const IconPoint& b = pts[static_cast<std::size_t>((i + 1) % n)];
    area2 += a.x * b.y - b.x * a.y;
  }
  if (area2 < 0.0f) std::reverse(poly.begin(), poly.end());

  out.reserve(static_cast<std::size_t>(n - 2) * 3);
  int guard = n * n;  // a self-intersecting outline has no ear; stop rather than spin
  while (poly.size() > 3 && guard-- > 0) {
    bool clipped = false;
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) {
      const int ia = poly[static_cast<std::size_t>((i + m - 1) % m)];
      const int ib = poly[static_cast<std::size_t>(i)];
      const int ic = poly[static_cast<std::size_t>((i + 1) % m)];
      if (cross2Icon(pts[static_cast<std::size_t>(ia)], pts[static_cast<std::size_t>(ib)],
                     pts[static_cast<std::size_t>(ic)]) <= 0.0f) {
        continue;  // a reflex corner is not an ear
      }
      bool contains = false;
      for (const int j : poly) {
        if (j == ia || j == ib || j == ic) continue;
        const float d1 = cross2Icon(pts[static_cast<std::size_t>(ia)],
                                    pts[static_cast<std::size_t>(ib)],
                                    pts[static_cast<std::size_t>(j)]);
        const float d2 = cross2Icon(pts[static_cast<std::size_t>(ib)],
                                    pts[static_cast<std::size_t>(ic)],
                                    pts[static_cast<std::size_t>(j)]);
        const float d3 = cross2Icon(pts[static_cast<std::size_t>(ic)],
                                    pts[static_cast<std::size_t>(ia)],
                                    pts[static_cast<std::size_t>(j)]);
        if (!((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0))) {
          contains = true;
          break;
        }
      }
      if (contains) continue;
      out.push_back(ia);
      out.push_back(ib);
      out.push_back(ic);
      poly.erase(poly.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) {
      out.clear();  // no ear anywhere: not a simple polygon
      return out;
    }
  }
  if (poly.size() == 3) {
    out.push_back(poly[0]);
    out.push_back(poly[1]);
    out.push_back(poly[2]);
  }
  return out;
}

/// The icon's own centroid and its furthest point from it, in icon units (0..100). The
/// puck recentres on the centroid and the disc is sized to that same centroid, so the two
/// stay concentric whichever way an outline is authored.
struct IconExtent {
  float cx{0.0f};
  float cy{0.0f};
  float radius{0.0f};  // furthest outline point from the centroid; 0 for an empty icon
};

IconExtent iconExtent(const PieceIcon& icon) {
  IconExtent e;
  std::size_t count = 0;
  for (const IconPoly& poly : icon.fills) {
    for (const IconPoint& p : poly) {
      e.cx += p.x;
      e.cy += p.y;
      ++count;
    }
  }
  if (count == 0) return e;
  e.cx /= static_cast<float>(count);
  e.cy /= static_cast<float>(count);
  float maxR2 = 0.0f;
  for (const IconPoly& poly : icon.fills) {
    for (const IconPoint& p : poly) {
      const float dx = p.x - e.cx;
      const float dy = p.y - e.cy;
      maxR2 = std::max(maxR2, dx * dx + dy * dy);
    }
  }
  e.radius = std::sqrt(maxR2);
  return e;
}

/// Extrude one `PieceIcon` into a two-sided puck (M17.25). Each sub-polygon becomes a
/// thin prism: the *same* outline stamped on both caps, so a flipped seat still reads as
/// the same piece, with a rim between them. The icon is the flat board's Primitive set,
/// whose outlines have no cut-outs - a hole-free outline extrudes as a solid without a
/// polygon-with-holes triangulator. Since M17.26 this is the *decal* on the token disc:
/// its footprint is shrunk by the flat view's glyph-to-circle ratio, and it stays a hair
/// thicker than the disc so its caps sit just proud of it.
void addIconCylinder(MeshLibrary& m, std::int32_t base, const PieceIcon& icon) {
  const IconExtent e = iconExtent(icon);
  if (e.radius <= 0.0f) return;
  const float cx = e.cx;
  const float cy = e.cy;

  const float s = kCylinderDecalScale;
  const float h = kCylinderHalfThick;
  const auto off = static_cast<std::uint32_t>(base);
  // The icon is authored x-right, y-down; local +y is up, so the glyph is not upside
  // down on a seat. Recentred on its own centroid and scaled into local units.
  const auto local = [&](const IconPoint& p) {
    return IconPoint{(p.x - cx) * s, (cy - p.y) * s};
  };

  for (const IconPoly& poly : icon.fills) {
    std::vector<IconPoint> pts;
    pts.reserve(poly.size());
    for (const IconPoint& p : poly) pts.push_back(local(p));
    if (pts.size() < 3) continue;
    const std::vector<int> tris = earClipIcon(pts);
    if (tris.empty()) continue;

    // Both caps carry the same points in the same order; the bottom is the top pushed
    // down, never a mirror. Separate vertices per cap so each face has a flat normal and
    // a hard edge against the rim.
    std::vector<std::uint32_t> top(pts.size());
    std::vector<std::uint32_t> bottom(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
      top[i] = pushVertex(m, pts[i].x, pts[i].y, h, 0.0f, 0.0f, 1.0f, 1.0f) - off;
      bottom[i] = pushVertex(m, pts[i].x, pts[i].y, -h, 0.0f, 0.0f, -1.0f, 1.0f) - off;
    }
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
      const auto a = static_cast<std::uint32_t>(tris[t]);
      const auto b = static_cast<std::uint32_t>(tris[t + 1]);
      const auto c = static_cast<std::uint32_t>(tris[t + 2]);
      pushTri(m, top[a], top[b], top[c]);           // anticlockwise in xy: +z
      pushTri(m, bottom[a], bottom[c], bottom[b]);  // reversed: -z
    }
    // The side wall. For an anticlockwise outline the outward normal of edge i->j is
    // (dy, -dx); each quad its own vertices, so the wall keeps a crisp silhouette rather
    // than smearing the face's shading round the corner.
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const std::size_t j = (i + 1) % pts.size();
      const float dx = pts[j].x - pts[i].x;
      const float dy = pts[j].y - pts[i].y;
      const float len = std::sqrt(dx * dx + dy * dy);
      if (len < 1e-9f) continue;
      const float nx = dy / len;
      const float ny = -dx / len;
      const std::uint32_t a =
          pushVertex(m, pts[i].x, pts[i].y, h, nx, ny, 0.0f, 1.0f) - off;
      const std::uint32_t b =
          pushVertex(m, pts[j].x, pts[j].y, h, nx, ny, 0.0f, 1.0f) - off;
      const std::uint32_t c =
          pushVertex(m, pts[j].x, pts[j].y, -h, nx, ny, 0.0f, 1.0f) - off;
      const std::uint32_t d =
          pushVertex(m, pts[i].x, pts[i].y, -h, nx, ny, 0.0f, 1.0f) - off;
      pushTri(m, a, c, b);
      pushTri(m, a, d, c);
    }
  }
}

/// The flat view's token circle as a solid (M17.26): a real cylinder - two round caps and
/// a round side wall - plain white (1,1,1,1) vertices exactly like the icon mesh's. Its
/// radius is the icon's own old footprint (`kCylinderIconScale`), so the shrunk decal
/// sits inside it at the flat view's glyph-to-circle ratio. Height is 1 on every vertex,
/// the same as the icon decal's, so the disc shades flat like the flat view's circle
/// instead of darkening toward its foot.
void addIconDisc(MeshLibrary& m, std::int32_t base, const PieceIcon& icon) {
  const IconExtent e = iconExtent(icon);
  if (e.radius <= 0.0f) return;
  const float r = e.radius * kCylinderIconScale;
  const float h = kCylinderDiscHalfThick;
  const auto off = static_cast<std::uint32_t>(base);
  const float tau = 2.0f * std::numbers::pi_v<float>;

  const std::uint32_t top = pushVertex(m, 0, 0, h, 0, 0, 1, 1.0f) - off;
  const std::uint32_t bottom = pushVertex(m, 0, 0, -h, 0, 0, -1, 1.0f) - off;
  for (int i = 0; i < kDiscSegments; ++i) {
    const float a0 = tau * static_cast<float>(i) / kDiscSegments;
    const float a1 = tau * static_cast<float>(i + 1) / kDiscSegments;
    const float c0 = std::cos(a0);
    const float s0 = std::sin(a0);
    const float c1 = std::cos(a1);
    const float s1 = std::sin(a1);

    const std::uint32_t t0 = pushVertex(m, c0 * r, s0 * r, h, 0, 0, 1, 1.0f) - off;
    const std::uint32_t t1 = pushVertex(m, c1 * r, s1 * r, h, 0, 0, 1, 1.0f) - off;
    pushTri(m, top, t0, t1);

    const std::uint32_t b0 = pushVertex(m, c0 * r, s0 * r, -h, 0, 0, -1, 1.0f) - off;
    const std::uint32_t b1 = pushVertex(m, c1 * r, s1 * r, -h, 0, 0, -1, 1.0f) - off;
    pushTri(m, bottom, b1, b0);

    // The wall, its own vertices per segment so its normals are radial and it keeps a
    // crisp silhouette against the caps' hard edge.
    const std::uint32_t w0 = pushVertex(m, c0 * r, s0 * r, h, c0, s0, 0, 1.0f) - off;
    const std::uint32_t w1 = pushVertex(m, c1 * r, s1 * r, h, c1, s1, 0, 1.0f) - off;
    const std::uint32_t w2 = pushVertex(m, c1 * r, s1 * r, -h, c1, s1, 0, 1.0f) - off;
    const std::uint32_t w3 = pushVertex(m, c0 * r, s0 * r, -h, c0, s0, 0, 1.0f) - off;
    pushTri(m, w0, w3, w2);
    pushTri(m, w0, w2, w1);
  }
}

/// Every piece stands on the same foot, which is what makes an army look like a set.
void addFoot(MeshLibrary& m, std::int32_t base, float totalHeight) {
  addCone(m, base, 0.00f, 0.09f, kPieceFootRadius, 0.30f, totalHeight, false);
  addCone(m, base, 0.09f, 0.14f, 0.30f, 0.22f, totalHeight, false);
}

void buildArchetype(MeshLibrary& m, Archetype which, std::int32_t base) {
  switch (which) {
    case Archetype::Cell:
      // The board cell: a thin slab, drawn by the same pipeline as everything else.
      addBox(m, base, 0, 0, 0, 0.46f, 0.46f, 0.055f, 1.0f);
      return;

    case Archetype::Dome: {  // pawn
      const float h = 0.62f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.34f, 0.20f, 0.17f, h, false);
      addCone(m, base, 0.34f, 0.44f, 0.24f, 0.20f, h, false);
      addCone(m, base, 0.44f, 0.62f, 0.20f, 0.02f, h, true);
      return;
    }

    case Archetype::Tower: {  // rook
      const float h = 0.86f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.66f, 0.24f, 0.26f, h, false);
      addCone(m, base, 0.66f, 0.72f, 0.31f, 0.31f, h, true);
      // Crenellations, which are what make a rook unmistakable in silhouette.
      for (int i = 0; i < 4; ++i) {
        const float angle = std::numbers::pi_v<float> * 0.5f * static_cast<float>(i);
        addBox(m, base, std::cos(angle) * 0.20f, std::sin(angle) * 0.20f, 0.80f, 0.09f,
               0.09f, 0.09f, h);
      }
      return;
    }

    case Archetype::Wedge: {  // knight
      const float h = 0.88f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.40f, 0.22f, 0.19f, h, false);
      // A head leaning forward: asymmetry is the whole point, so a knight can be told
      // from a bishop at a glance and from any angle.
      addBox(m, base, 0.00f, 0.02f, 0.56f, 0.15f, 0.20f, 0.17f, h);
      addBox(m, base, 0.00f, 0.16f, 0.72f, 0.12f, 0.16f, 0.12f, h);
      addBox(m, base, 0.00f, -0.10f, 0.78f, 0.07f, 0.09f, 0.10f, h);
      return;
    }

    case Archetype::Spire: {  // bishop
      const float h = 0.96f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.30f, 0.21f, 0.18f, h, false);
      addCone(m, base, 0.30f, 0.36f, 0.26f, 0.22f, h, false);
      addCone(m, base, 0.36f, 0.82f, 0.20f, 0.05f, h, false);
      addCone(m, base, 0.82f, 0.96f, 0.07f, 0.01f, h, true);
      return;
    }

    case Archetype::Crown: {  // queen
      const float h = 1.05f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.62f, 0.23f, 0.17f, h, false);
      addCone(m, base, 0.62f, 0.72f, 0.30f, 0.26f, h, false);
      for (int i = 0; i < 6; ++i) {
        const float angle =
            2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / 6.0f;
        addBox(m, base, std::cos(angle) * 0.19f, std::sin(angle) * 0.19f, 0.86f, 0.055f,
               0.055f, 0.14f, h);
      }
      addCone(m, base, 1.00f, 1.05f, 0.06f, 0.00f, h, true);
      return;
    }

    case Archetype::Monolith: {  // king
      const float h = 1.18f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.70f, 0.24f, 0.19f, h, false);
      addCone(m, base, 0.70f, 0.80f, 0.29f, 0.25f, h, false);
      // A cross: the one silhouette nobody mistakes for anything else.
      addBox(m, base, 0, 0, 0.99f, 0.06f, 0.06f, 0.19f, h);
      addBox(m, base, 0, 0, 1.02f, 0.16f, 0.06f, 0.06f, h);
      return;
    }

    case Archetype::Horn: {  // unicorn and friends
      const float h = 1.00f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.34f, 0.22f, 0.19f, h, false);
      // A long spike leaning off-axis, so a piece that only exists in three or more
      // dimensions looks like it belongs to a different geometry than the rest.
      addBox(m, base, 0.05f, 0.00f, 0.52f, 0.13f, 0.13f, 0.20f, h);
      addCone(m, base, 0.72f, 1.00f, 0.11f, 0.01f, h, true);
      return;
    }

    case Archetype::Portal:
      // A unit cube, not a slab: a portal takes the shape of the seam it belongs to, and
      // only a symmetric body can be scaled into a doorway standing on any axis.
      addBox(m, base, 0, 0, 0, 0.5f, 0.5f, 0.5f, 1.0f);
      return;

    case Archetype::Arrow:
      // Sized to the cell's own bounding box, so a head is placed the same way every
      // other accessory is: a wanted half-extent divided by `cellHalfExtent`.
      addTriPrism(m, base, 0.46f, 0.46f, 0.055f, 1.0f);
      return;

    case Archetype::Fillet:
      addQuarterDisc(m, base, 0.46f, 0.055f, 10);
      return;

    case Archetype::Disc:
      addShadowDisc(m, base);
      return;

    case Archetype::Count:
      return;
  }
}

}  // namespace

Archetype archetypeFromName(std::string_view name) {
  if (name == "dome") return Archetype::Dome;
  if (name == "tower") return Archetype::Tower;
  if (name == "wedge") return Archetype::Wedge;
  if (name == "spire") return Archetype::Spire;
  if (name == "crown") return Archetype::Crown;
  if (name == "monolith") return Archetype::Monolith;
  if (name == "horn") return Archetype::Horn;
  if (name == "portal") return Archetype::Portal;
  if (name == "arrow") return Archetype::Arrow;
  if (name == "fillet") return Archetype::Fillet;
  return Archetype::Tower;
}

std::string_view archetypeName(Archetype a) {
  switch (a) {
    case Archetype::Cell:
      return "cell";
    case Archetype::Dome:
      return "dome";
    case Archetype::Tower:
      return "tower";
    case Archetype::Wedge:
      return "wedge";
    case Archetype::Spire:
      return "spire";
    case Archetype::Crown:
      return "crown";
    case Archetype::Monolith:
      return "monolith";
    case Archetype::Horn:
      return "horn";
    case Archetype::Portal:
      return "portal";
    case Archetype::Arrow:
      return "arrow";
    case Archetype::Fillet:
      return "fillet";
    case Archetype::Disc:
      return "disc";
    case Archetype::Count:
      break;
  }
  return "tower";
}

Archetype archetypeFor(const PieceTypeDef& piece) {
  if (!piece.shape.empty()) return archetypeFromName(piece.shape);
  if (piece.royal) return Archetype::Monolith;

  // Read the movement. Order of tests matters: the more specific a piece's movement is,
  // the earlier it should be recognised.
  bool slides = false;
  bool leaps = false;
  bool oriented = false;
  std::size_t maxOrder = 0;
  std::size_t maxDiagonalOrder = 0;
  bool axisLine = false;  // a rook's family: a slide along one axis
  for (const MoveAtom& a : piece.atoms) {
    maxOrder = std::max(maxOrder, a.mags.size());
    if (a.mags.size() == 1 && a.mode == MoveMode::Slide && a.maxK > 1) axisLine = true;
    if (a.oriented) oriented = true;
    if (a.mode == MoveMode::Slide && a.maxK > 1) slides = true;
    if (a.mode == MoveMode::Leap || a.maxK == 1) leaps = true;
    bool uniform = true;
    for (std::size_t i = 1; i < a.mags.size(); ++i) {
      if (a.mags[i] != a.mags[0]) uniform = false;
    }
    if (uniform && a.mags.size() >= 2)
      maxDiagonalOrder = std::max(maxDiagonalOrder, a.mags.size());
  }

  if (oriented) return Archetype::Dome;  // moves only forward: a pawn
  // Three axes at once *and nothing else* is a unicorn. A queen rides the triagonals too,
  // but she also rides the axes, and that family is what makes her a queen.
  if (maxDiagonalOrder >= 3 && !axisLine) return Archetype::Horn;
  if (!slides && maxOrder >= 2) return Archetype::Wedge;  // a leaper with an offset
  if (slides && piece.atoms.size() >= 2)
    return Archetype::Crown;  // several sliding families
  if (slides && maxDiagonalOrder == 2) return Archetype::Spire;  // a diagonal slider
  if (slides) return Archetype::Tower;                           // an orthogonal slider
  (void)leaps;
  return Archetype::Dome;
}

float heightFor(const PieceTypeDef& piece) {
  if (piece.heightPermille > 0) return static_cast<float>(piece.heightPermille) / 1000.0f;

  // The archetypes already differ in height - a dome is short, a monolith is tall - so
  // this is a multiplier *around* that, not a replacement for it. It reads how far and
  // how freely the piece moves, which is the engine's best evidence of what it is worth,
  // and spreads the result widely enough that an unfamiliar army has a visible hierarchy.
  float reach = 0.0f;
  std::size_t widest = 1;
  for (const MoveAtom& a : piece.atoms) {
    widest = std::max(widest, a.mags.size());
    const float atomReach =
        a.maxK == kUnlimited ? 1.0f : std::min(1.0f, static_cast<float>(a.maxK) / 3.0f);
    reach = std::max(reach, atomReach);
  }
  float scale = 0.92f + 0.30f * reach + 0.08f * static_cast<float>(widest - 1);
  // Several different ways of moving is itself a sign of a powerful piece.
  if (piece.atoms.size() >= 2) scale += 0.10f;
  if (piece.royal) scale = std::max(scale, 1.45f);
  return std::clamp(scale, 0.85f, 1.6f);
}

MeshLibrary MeshLibrary::build(const VariantSpec* v) {
  MeshLibrary m;
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    const auto base = static_cast<std::int32_t>(m.vertices.size());
    MeshRange range;
    range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
    range.vertexOffset = base;
    buildArchetype(m, static_cast<Archetype>(i), base);
    range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
    m.ranges[i] = range;
  }
  // The shape view's icon-faced pucks (M17.25), appended into the same buffers so the
  // draw loop is unchanged apart from which range it reads. Archetypes that are not
  // pieces have an empty icon and therefore an empty range.
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    const auto base = static_cast<std::int32_t>(m.vertices.size());
    MeshRange range;
    range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
    range.vertexOffset = base;
    addIconCylinder(m, base, pieceIcon(IconStyle::Primitive, static_cast<Archetype>(i)));
    range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
    m.cylinderRanges[i] = range;
  }
  // The token discs (M17.26), each built from its own archetype's icon footprint, in the
  // same buffers again. Plain white, so the draw loop is unchanged apart from which range
  // it reads and which instance colour the shader multiplies in.
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    const auto base = static_cast<std::int32_t>(m.vertices.size());
    MeshRange range;
    range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
    range.vertexOffset = base;
    addIconDisc(m, base, pieceIcon(IconStyle::Primitive, static_cast<Archetype>(i)));
    range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
    m.discRanges[i] = range;
  }
  // The variant's authored bodies (M7.3.4), each in the slot shapeSlots gives it. Its
  // puck and disc are its archetype's: the shape view's token is a fallback-quality
  // picture either way, and the Primitive icon set is what a puck is built to extrude.
  if (v != nullptr) {
    const std::vector<std::size_t> slots = shapeSlots(*v);
    for (std::size_t i = 1; i < v->pieces.size(); ++i) {
      const std::size_t slot = slots[i];
      if (slot < static_cast<std::size_t>(Archetype::Count)) continue;
      m.ranges[slot] = m.appendModel(*v->pieces[i].model);
      const auto fallback = static_cast<std::size_t>(archetypeFor(v->pieces[i]));
      m.cylinderRanges[slot] = m.cylinderRanges[fallback];
      m.discRanges[slot] = m.discRanges[fallback];
    }
  }
  return m;
}

std::vector<std::size_t> shapeSlots(const VariantSpec& v) {
  std::vector<std::size_t> out(v.pieces.size(),
                               static_cast<std::size_t>(Archetype::Tower));
  std::size_t next = static_cast<std::size_t>(Archetype::Count);
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    const PieceTypeDef& def = v.pieces[i];
    if (def.model && next < kShapeSlots) {
      out[i] = next++;
    } else {
      out[i] = static_cast<std::size_t>(archetypeFor(def));
    }
  }
  return out;
}

std::uint64_t authoredKey(const VariantSpec& v) {
  // FNV-1a over every point, in piece order. Collisions only cost a stale mesh until the
  // next change, and the inputs are a few hundred small integers.
  std::uint64_t h = 0;
  bool any = false;
  const auto mix = [&](std::int64_t x) {
    h ^= static_cast<std::uint64_t>(x);
    h *= 0x100000001B3ULL;
  };
  const auto outline = [&](const assets::Outline& o) {
    mix(static_cast<std::int64_t>(o.points.size()));
    for (const assets::ModelPoint& p : o.points) {
      mix(p.x);
      mix(p.y);
    }
  };
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    const PieceTypeDef& def = v.pieces[i];
    if (!def.model) continue;
    if (!any) h = 0xCBF29CE484222325ULL;
    any = true;
    mix(static_cast<std::int64_t>(i));
    mix(def.model->segments);
    mix(static_cast<std::int64_t>(def.model->symmetry.kind));
    mix(def.model->symmetry.k);
    outline(def.model->profile);
    for (const assets::Element& e : def.model->elements) {
      mix(e.radius);
      mix(e.height);
      outline(e.outline);
    }
  }
  return h;
}

namespace {

/// Ear clipping, for an element's face: a knight's head is not convex. Indices into
/// `pts`, wound as the outline is. Degenerate input yields whatever ears it can.
std::vector<std::array<std::uint32_t, 3>> triangulate(
    const std::vector<std::array<float, 2>>& pts) {
  std::vector<std::array<std::uint32_t, 3>> tris;
  const std::size_t n = pts.size();
  if (n < 3) return tris;
  float area = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const auto& a = pts[i];
    const auto& b = pts[(i + 1) % n];
    area += a[0] * b[1] - b[0] * a[1];
  }
  const float wind = area >= 0.0f ? 1.0f : -1.0f;
  std::vector<std::uint32_t> idx(n);
  for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
  const auto cross = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    return ((pts[b][0] - pts[a][0]) * (pts[c][1] - pts[a][1]) -
            (pts[b][1] - pts[a][1]) * (pts[c][0] - pts[a][0])) *
           wind;
  };
  std::size_t guard = n * n;
  while (idx.size() > 3 && guard-- > 0) {
    bool clipped = false;
    for (std::size_t i = 0; i < idx.size(); ++i) {
      const std::uint32_t a = idx[(i + idx.size() - 1) % idx.size()];
      const std::uint32_t b = idx[i];
      const std::uint32_t c = idx[(i + 1) % idx.size()];
      if (cross(a, b, c) <= 0.0f) continue;  // reflex: not an ear
      bool inside = false;
      for (const std::uint32_t q : idx) {
        if (q == a || q == b || q == c) continue;
        if (cross(a, b, q) >= 0.0f && cross(b, c, q) >= 0.0f && cross(c, a, q) >= 0.0f) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      tris.push_back({a, b, c});
      idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
  return tris;
}

}  // namespace

MeshRange MeshLibrary::appendModel(const assets::PieceModel& model) {
  constexpr float kUnit = 0.001f;  // permille to local units
  // An element is a slab this thick across the tangent: thin enough to read as a plate
  // - a crown's point, a knight's ear - and thick enough not to vanish edge-on.
  constexpr float kHalfThick = 0.045f;
  const float tau = 2.0f * std::numbers::pi_v<float>;
  MeshRange range;
  range.firstIndex = static_cast<std::uint32_t>(indices.size());
  range.vertexOffset = static_cast<std::int32_t>(vertices.size());
  const auto base = static_cast<std::uint32_t>(range.vertexOffset);

  const std::vector<assets::ModelPoint>& prof = model.profile.points;
  float top = 0.0f;
  for (const assets::ModelPoint& p : prof) top = std::max(top, static_cast<float>(p.y));
  for (const assets::Element& e : model.elements) {
    for (const assets::ModelPoint& p : e.outline.points) {
      top = std::max(top, static_cast<float>(e.height + p.y));
    }
  }
  const float totalHeight = std::max(top * kUnit, 0.05f);

  // ---- the revolve -----------------------------------------------------------------
  // Each profile edge sweeps a band of quads. The profile's own winding says which side
  // of an edge is outside, so the normals face out whichever way it was drawn.
  float area = 0.0f;
  for (std::size_t i = 0; i < prof.size(); ++i) {
    const assets::ModelPoint& a = prof[i];
    const assets::ModelPoint& b = prof[(i + 1) % prof.size()];
    area += static_cast<float>(a.x) * b.y - static_cast<float>(b.x) * a.y;
  }
  const float outward = area >= 0.0f ? 1.0f : -1.0f;
  const int segs = std::max<int>(3, model.segments);
  for (std::size_t i = 0; i < prof.size(); ++i) {
    const assets::ModelPoint& a = prof[i];
    const assets::ModelPoint& b = prof[(i + 1) % prof.size()];
    if (a.x == 0 && b.x == 0) continue;  // on the axis: sweeps to nothing
    const float ar = a.x * kUnit, az = a.y * kUnit;
    const float br = b.x * kUnit, bz = b.y * kUnit;
    float nr = (bz - az) * outward;
    float nz = -(br - ar) * outward;
    const float len = std::hypot(nr, nz);
    if (len <= 0.0f) continue;
    nr /= len;
    nz /= len;
    for (int j = 0; j < segs; ++j) {
      const float a0 = tau * static_cast<float>(j) / static_cast<float>(segs);
      const float a1 = tau * static_cast<float>(j + 1) / static_cast<float>(segs);
      const float c0 = std::cos(a0), s0 = std::sin(a0);
      const float c1 = std::cos(a1), s1 = std::sin(a1);
      // Faceted on purpose: one normal per facet, so a low segment count reads as a cut
      // stone rather than a badly smoothed cylinder.
      const float cm = std::cos((a0 + a1) * 0.5f), sm = std::sin((a0 + a1) * 0.5f);
      const auto put = [&](float r, float z, float c, float s) {
        return pushVertex(*this, c * r, s * r, z, cm * nr, sm * nr, nz, z / totalHeight);
      };
      const std::uint32_t v0 = put(ar, az, c0, s0);
      const std::uint32_t v1 = put(ar, az, c1, s1);
      const std::uint32_t v2 = put(br, bz, c1, s1);
      const std::uint32_t v3 = put(br, bz, c0, s0);
      pushTri(*this, v0 - base, v1 - base, v2 - base);
      pushTri(*this, v0 - base, v2 - base, v3 - base);
    }
  }

  // ---- the elements, placed by the symmetry group -----------------------------------
  // An element is drawn in the plane of (radial, up) at its copy's angle and extruded
  // across the tangent. k-fold puts copy i at i/k of a turn; mirror puts the second copy
  // half a turn round, which is the element reflected through the axis.
  const int copies = assets::copiesOf(model.symmetry);
  for (const assets::Element& e : model.elements) {
    std::vector<std::array<float, 2>> pts;  // (radial, up), local units
    for (const assets::ModelPoint& p : e.outline.points) {
      pts.push_back({(e.radius + p.x) * kUnit, (e.height + p.y) * kUnit});
    }
    const auto tris = triangulate(pts);
    float earea = 0.0f;
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const auto& a = pts[i];
      const auto& b = pts[(i + 1) % pts.size()];
      earea += a[0] * b[1] - b[0] * a[1];
    }
    const float eout = earea >= 0.0f ? 1.0f : -1.0f;
    for (int c = 0; c < copies; ++c) {
      const float phi = tau * static_cast<float>(c) / static_cast<float>(copies);
      const float rx = std::cos(phi), ry = std::sin(phi);  // radial
      const float tx = -ry, ty = rx;                       // tangent
      const auto put = [&](const std::array<float, 2>& q, float t, float nx, float ny,
                           float nz) {
        return pushVertex(*this, rx * q[0] + tx * t, ry * q[0] + ty * t, q[1], nx, ny, nz,
                          q[1] / totalHeight);
      };
      for (const float side : {1.0f, -1.0f}) {
        std::vector<std::uint32_t> ids;
        ids.reserve(pts.size());
        for (const auto& q : pts)
          ids.push_back(put(q, side * kHalfThick, tx * side, ty * side, 0));
        for (const auto& tri : tris) {
          pushTri(*this, ids[tri[0]] - base, ids[tri[1]] - base, ids[tri[2]] - base);
        }
      }
      for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        float nrad = (b[1] - a[1]) * eout;
        float nup = -(b[0] - a[0]) * eout;
        const float len = std::hypot(nrad, nup);
        if (len <= 0.0f) continue;
        nrad /= len;
        nup /= len;
        const std::uint32_t v0 = put(a, kHalfThick, rx * nrad, ry * nrad, nup);
        const std::uint32_t v1 = put(b, kHalfThick, rx * nrad, ry * nrad, nup);
        const std::uint32_t v2 = put(b, -kHalfThick, rx * nrad, ry * nrad, nup);
        const std::uint32_t v3 = put(a, -kHalfThick, rx * nrad, ry * nrad, nup);
        pushTri(*this, v0 - base, v1 - base, v2 - base);
        pushTri(*this, v0 - base, v2 - base, v3 - base);
      }
    }
  }
  range.indexCount = static_cast<std::uint32_t>(indices.size()) - range.firstIndex;
  return range;
}

}  // namespace cb::render
