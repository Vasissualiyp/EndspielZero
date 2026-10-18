// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/piece_mesh.hpp"

#include <algorithm>
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


// ---------------------------------------------------------------------------
// Authored bodies
// ---------------------------------------------------------------------------

namespace {

struct V3 {
  float x{0}, y{0}, z{0};
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross3(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Collects a model's triangles. A triangle's winding is decided here, from the normals
/// its vertices carry, so the callers can build in whatever handedness is convenient - a
/// mirrored copy is a reflection, which would otherwise turn every face inside out.
struct ModelBuilder {
  ModelMesh mesh;
  float totalHeight{1.0f};

  std::uint32_t vertex(V3 p, V3 n) {
    const float len = std::sqrt(dot3(n, n));
    if (len > 1e-8f) n = n * (1.0f / len);
    mesh.vertices.push_back(
        MeshVertex{{p.x, p.y, p.z}, {n.x, n.y, n.z}, p.z / totalHeight, {1, 1, 1, 1}});
    return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
  }
  void tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const auto& va = mesh.vertices[a];
    const auto& vb = mesh.vertices[b];
    const auto& vc = mesh.vertices[c];
    const V3 pa{va.pos[0], va.pos[1], va.pos[2]};
    const V3 pb{vb.pos[0], vb.pos[1], vb.pos[2]};
    const V3 pc{vc.pos[0], vc.pos[1], vc.pos[2]};
    const V3 geometric = cross3(pb - pa, pc - pa);
    if (dot3(geometric, geometric) < 1e-14f) return;  // a sliver on the axis: no area
    const V3 want{va.normal[0] + vb.normal[0] + vc.normal[0],
                  va.normal[1] + vb.normal[1] + vc.normal[1],
                  va.normal[2] + vb.normal[2] + vc.normal[2]};
    if (dot3(geometric, want) < 0.0f) std::swap(b, c);
    mesh.indices.push_back(static_cast<std::uint16_t>(a));
    mesh.indices.push_back(static_cast<std::uint16_t>(b));
    mesh.indices.push_back(static_cast<std::uint16_t>(c));
  }
};

float signedArea2(const std::vector<assets::ModelPoint>& pts) {
  float a = 0.0f;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const auto& p = pts[i];
    const auto& q = pts[(i + 1) % pts.size()];
    a += static_cast<float>(p.x) * static_cast<float>(q.y) -
         static_cast<float>(q.x) * static_cast<float>(p.y);
  }
  return a;
}

void revolve(ModelBuilder& b, const assets::PieceModel& model) {
  const auto& pts = model.profile.points;
  if (pts.size() < 3) return;
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const int segs = std::max<int>(3, model.segments);
  const float orient = signedArea2(pts) >= 0.0f ? 1.0f : -1.0f;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const auto& pa = pts[i];
    const auto& pb = pts[(i + 1) % pts.size()];
    if (pa.x <= 0 && pb.x <= 0) continue;  // along the axis: nothing to sweep
    const float ra = static_cast<float>(pa.x) / 1000.0f;
    const float za = static_cast<float>(pa.y) / 1000.0f;
    const float rb = static_cast<float>(pb.x) / 1000.0f;
    const float zb = static_cast<float>(pb.y) / 1000.0f;
    // Outward in the (radius, height) plane: to the right of the direction of travel for
    // an anticlockwise profile, the left for a clockwise one.
    const float dr = rb - ra;
    const float dz = zb - za;
    const float nr = orient * dz;
    const float nz = -orient * dr;
    for (int j = 0; j < segs; ++j) {
      const float a0 = tau * static_cast<float>(j) / static_cast<float>(segs);
      const float a1 = tau * static_cast<float>(j + 1) / static_cast<float>(segs);
      const float c0 = std::cos(a0), s0 = std::sin(a0);
      const float c1 = std::cos(a1), s1 = std::sin(a1);
      const std::uint32_t v0 = b.vertex({c0 * ra, s0 * ra, za}, {c0 * nr, s0 * nr, nz});
      const std::uint32_t v1 = b.vertex({c1 * ra, s1 * ra, za}, {c1 * nr, s1 * nr, nz});
      const std::uint32_t v2 = b.vertex({c1 * rb, s1 * rb, zb}, {c1 * nr, s1 * nr, nz});
      const std::uint32_t v3 = b.vertex({c0 * rb, s0 * rb, zb}, {c0 * nr, s0 * nr, nz});
      b.tri(v0, v1, v2);
      b.tri(v0, v2, v3);
    }
  }
}

/// One copy of an element: the outline extruded about its plane. `frame` gives the plane's
/// two directions (`u` along the outline's x, up for its y) and the way it is extruded.
void prism(ModelBuilder& b, const assets::Element& e, V3 origin, V3 u, V3 extrude,
           float thickness) {
  const auto& pts = e.outline.points;
  if (pts.size() < 3) return;
  const V3 up{0, 0, 1};
  const auto at = [&](const assets::ModelPoint& p, float w) {
    return origin + u * (static_cast<float>(p.x) / 1000.0f) +
           up * (static_cast<float>(p.y) / 1000.0f) + extrude * w;
  };
  const float half = thickness * 0.5f;
  std::vector<IconPoint> flat;
  flat.reserve(pts.size());
  for (const auto& p : pts) {
    flat.push_back(IconPoint{static_cast<float>(p.x), static_cast<float>(p.y)});
  }
  const std::vector<int> tris = earClipIcon(flat);
  for (const float w : {half, -half}) {
    const V3 n = extrude * (w > 0 ? 1.0f : -1.0f);
    std::vector<std::uint32_t> ring;
    ring.reserve(pts.size());
    for (const auto& p : pts) ring.push_back(b.vertex(at(p, w), n));
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
      b.tri(ring[static_cast<std::size_t>(tris[t])],
            ring[static_cast<std::size_t>(tris[t + 1])],
            ring[static_cast<std::size_t>(tris[t + 2])]);
    }
  }
  const float orient = signedArea2(pts) >= 0.0f ? 1.0f : -1.0f;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const auto& pa = pts[i];
    const auto& pb = pts[(i + 1) % pts.size()];
    const float du = static_cast<float>(pb.x - pa.x);
    const float dv = static_cast<float>(pb.y - pa.y);
    const V3 n = u * (orient * dv) + up * (-orient * du);
    const std::uint32_t q0 = b.vertex(at(pa, half), n);
    const std::uint32_t q1 = b.vertex(at(pb, half), n);
    const std::uint32_t q2 = b.vertex(at(pb, -half), n);
    const std::uint32_t q3 = b.vertex(at(pa, -half), n);
    b.tri(q0, q1, q2);
    b.tri(q0, q2, q3);
  }
}

void place(ModelBuilder& b, const assets::PieceModel& model, const assets::Element& e) {
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const float radius = static_cast<float>(e.radius) / 1000.0f;
  const float lateral = static_cast<float>(e.lateral) / 1000.0f;
  const float height = static_cast<float>(e.height) / 1000.0f;
  const float thick = static_cast<float>(e.thickness) / 1000.0f;
  const auto copy = [&](float theta, bool mirrored) {
    const V3 radial{std::cos(theta), std::sin(theta), 0.0f};
    // A reflection across the radial plane turns the tangent round, and nothing else.
    const V3 tangent = V3{-std::sin(theta), std::cos(theta), 0.0f} * (mirrored ? -1.0f : 1.0f);
    const V3 origin = radial * radius + tangent * lateral + V3{0, 0, height};
    if (e.tangent) {
      prism(b, e, origin, tangent, radial, thick);
    } else {
      prism(b, e, origin, radial, tangent, thick);
    }
  };
  switch (model.symmetry.kind) {
    case assets::SymmetryKind::Full:
      break;  // refused by `validate`; nothing to place
    case assets::SymmetryKind::KFold: {
      const int k = std::max<int>(2, model.symmetry.k);
      for (int i = 0; i < k; ++i) copy(tau * static_cast<float>(i) / static_cast<float>(k), false);
      break;
    }
    case assets::SymmetryKind::Mirror:
      copy(0.0f, false);
      if (e.lateral != 0) copy(0.0f, true);
      break;
  }
}

}  // namespace

ModelMesh assembleModel(const assets::PieceModel& model) {
  ModelBuilder b;
  float top = 0.1f;
  for (const auto& p : model.profile.points) top = std::max(top, static_cast<float>(p.y) / 1000.0f);
  for (const auto& e : model.elements) {
    for (const auto& p : e.outline.points) {
      top = std::max(top, static_cast<float>(e.height + p.y) / 1000.0f);
    }
  }
  b.totalHeight = top;
  b.mesh.height = top;
  revolve(b, model);
  for (const assets::Element& e : model.elements) place(b, model, e);
  return std::move(b.mesh);
}

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

  if (oriented) return Archetype::Dome;                   // moves only forward: a pawn
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

MeshLibrary MeshLibrary::build(const std::vector<CustomBody>& custom) {
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
  // The authored bodies, after everything else so the archetypes' ranges are exactly what
  // they were without them.
  for (const CustomBody& c : custom) {
    if (c.slot >= kCustomShapes) continue;
    const std::size_t at = static_cast<std::size_t>(Archetype::Count) + c.slot;
    const ModelMesh mesh = assembleModel(c.body);
    if (mesh.vertices.empty()) continue;
    {
      const auto base = static_cast<std::int32_t>(m.vertices.size());
      MeshRange range;
      range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
      range.vertexOffset = base;
      m.vertices.insert(m.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
      m.indices.insert(m.indices.end(), mesh.indices.begin(), mesh.indices.end());
      range.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
      m.ranges[at] = range;
    }
    // Its puck and token take the authored icon, or the silhouette the body would draw.
    const std::optional<OwnedIcon> own =
        c.icon.has_value() ? std::optional<OwnedIcon>(std::in_place, *c.icon)
                           : std::nullopt;
    const PieceIcon icon =
        own.has_value() ? own->view() : pieceIcon(IconStyle::Primitive, c.fallback);
    {
      const auto base = static_cast<std::int32_t>(m.vertices.size());
      MeshRange range;
      range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
      range.vertexOffset = base;
      addIconCylinder(m, base, icon);
      range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
      m.cylinderRanges[at] = range;
    }
    {
      const auto base = static_cast<std::int32_t>(m.vertices.size());
      MeshRange range;
      range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
      range.vertexOffset = base;
      addIconDisc(m, base, icon);
      range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
      m.discRanges[at] = range;
    }
  }
  return m;
}

}  // namespace cb::render
