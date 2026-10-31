// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/deco.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "app/shell.hpp"
#include "render/quintic.hpp"
#include "render/ui_widgets.hpp"
#include "view/seams.hpp"

namespace cb::render {
namespace {

/// A decoration is drawn twice while a screen changes - the one arriving and the one
/// leaving - so every colour it emits is scaled by one alpha, set by drawDeco. That
/// keeps a fade from having to be threaded through every polygon by hand.
float gDecoAlpha = 1.0f;

ImU32 u32(const view::Rgba& c, float a = 1.0f) {
  return widgets::u32(c, a * gDecoAlpha);
}

constexpr float kPi = 3.14159265358979f;

/// A deterministic generator. The shell has to draw the same frame twice - that is how
/// the headless capture tests see it at all - so nothing here may reach for a clock or
/// a global random source.
float nextFloat(std::uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return static_cast<float>((s >> 8) & 0xFFFFFF) / static_cast<float>(0x1000000);
}

struct Vec3 {
  float x{0}, y{0}, z{0};
};

Vec3 rotateY(Vec3 p, float a) {
  return {p.x * std::cos(a) + p.z * std::sin(a), p.y,
          -p.x * std::sin(a) + p.z * std::cos(a)};
}
Vec3 rotateX(Vec3 p, float a) {
  return {p.x, p.y * std::cos(a) - p.z * std::sin(a),
          p.y * std::sin(a) + p.z * std::cos(a)};
}

/// Where a scene point lands, plus the depth it landed from - the depth is the only
/// cue a flat drawing has, so it is carried rather than discarded.
struct Projected {
  ImVec2 at;
  float depth{0};
};

struct Camera {
  ImVec2 centre;
  float scale{1};
  float yaw{0}, pitch{0};
  float perspective{0.16f};

  [[nodiscard]] Projected operator()(Vec3 p) const {
    const Vec3 q = rotateX(rotateY(p, yaw), pitch);
    const float k = 1.0f / (1.0f + q.z * perspective);
    return {ImVec2(centre.x + q.x * scale * k, centre.y - q.y * scale * k), q.z};
  }
};

view::Rgba withAlpha(view::Rgba c, float a) {
  c.a = a;
  return c;
}

/// Draw one piece icon, filled and outlined, centred on a point.
void icon(ImDrawList* dl, IconStyle style, Archetype shape, ImVec2 centre, float size,
          ImU32 fill, ImU32 line) {
  const PieceIcon art = pieceIcon(style, shape);
  const float k = size / 100.0f;
  for (const IconPoly& poly : art.fills) {
    if (poly.size() < 3) continue;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(centre.x + (p.x - 50.0f) * k, centre.y + (p.y - 50.0f) * k));
    }
    // Stroke before fill would be lost under it; the outline is what keeps a pale piece
    // visible on a pale tile.
    const ImVector<ImVec2> path = dl->_Path;
    widgets::fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, fill);
    dl->_Path = path;
    dl->PathStroke(line, ImDrawFlags_Closed, std::max(1.0f, size * 0.022f));
  }
  // The cut-outs - a rook's crenellation gaps, a knight's eye. Without them the faceted
  // crown is a solid slab. They are painted in the outline colour, which is the token or
  // tile the figure stands on, so the gap reads as the thing behind the piece.
  for (const IconPoly& poly : art.cuts) {
    if (poly.size() < 3) continue;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(centre.x + (p.x - 50.0f) * k, centre.y + (p.y - 50.0f) * k));
    }
    widgets::fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, line);
    dl->PathClear();
  }
}

const Archetype kPieceShapes[]{Archetype::Tower, Archetype::Wedge, Archetype::Crown,
                               Archetype::Spire, Archetype::Dome,  Archetype::Monolith,
                               Archetype::Horn};

// ---------------------------------------------------------------------------
// The Calabi-Yau quintic cross-section.
//
// The formula itself lives in render/quintic.hpp, shared with the T6 overture, so the
// menu object and the shape that overture settles into cannot drift apart. This adapts
// its float triple to the local Vec3; the standard-picture comment is with the formula.
// ---------------------------------------------------------------------------
Vec3 quintic(int k1, int k2, float x, float y) {
  const std::array<float, 3> p = quinticPoint(k1, k2, x, y);
  return {p[0], p[1], p[2]};
}

void drawManifold(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                  IconStyle iconStyle, float t, float yawTurn, float elevTurn) {
  Camera cam;
  cam.centre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  cam.scale = std::min(max.x - min.x, max.y - min.y) * 0.20f;
  cam.yaw = t * 0.09f + yawTurn;
  cam.pitch = std::clamp(0.45f + std::sin(t * 0.05f) * 0.22f + elevTurn, -1.35f, 1.35f);

  constexpr int kSteps = 6;
  struct Tile {
    ImVec2 p[4];
    float depth{0};
    bool dark{false};
  };
  std::vector<Tile> tiles;
  tiles.reserve(kQuinticN * kQuinticN * kSteps * kSteps);

  for (int k1 = 0; k1 < kQuinticN; ++k1) {
    for (int k2 = 0; k2 < kQuinticN; ++k2) {
      Projected grid[kSteps + 1][kSteps + 1];
      for (int i = 0; i <= kSteps; ++i) {
        for (int j = 0; j <= kSteps; ++j) {
          const float fx = static_cast<float>(i) / kSteps * (kPi * 0.5f);
          const float fy = -1.0f + 2.0f * static_cast<float>(j) / kSteps;
          grid[i][j] = cam(quintic(k1, k2, fx, fy));
        }
      }
      for (int i = 0; i < kSteps; ++i) {
        for (int j = 0; j < kSteps; ++j) {
          Tile tile;
          tile.p[0] = grid[i][j].at;
          tile.p[1] = grid[i + 1][j].at;
          tile.p[2] = grid[i + 1][j + 1].at;
          tile.p[3] = grid[i][j + 1].at;
          tile.depth = (grid[i][j].depth + grid[i + 1][j].depth +
                        grid[i + 1][j + 1].depth + grid[i][j + 1].depth) *
                       0.25f;
          tile.dark = (i + j + k1 + k2) % 2 == 0;
          tiles.push_back(tile);
        }
      }
    }
  }

  // Far to near: there is no depth buffer in a draw list, so the order is the depth.
  std::sort(tiles.begin(), tiles.end(),
            [](const Tile& a, const Tile& b) { return a.depth < b.depth; });

  // Drawn from the theme, so the cross-section follows the palette: a light page gets
  // ink and paper, and the candlelit page gets a muted tan over a near-black ground
  // rather than a bright cream that fights it.
  const view::Rgba dark = theme.light ? theme.bone : theme.soot;
  const view::Rgba pale = theme.light ? theme.panel : theme.boneDim;
  for (const Tile& tile : tiles) {
    const float near = std::clamp((tile.depth + 1.1f) / 2.2f, 0.0f, 1.0f);
    const float alpha = 0.13f + 0.26f * near;
    dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3],
                      u32(withAlpha(tile.dark ? dark : pale, alpha)));
    dl->AddQuad(tile.p[0], tile.p[1], tile.p[2], tile.p[3],
                u32(withAlpha(dark, 0.10f + 0.12f * near)), 1.0f);
  }

  // Both colours of piece, walking the surface's own parameter lines. Two-dimensional
  // pieces on a six-dimensional surface is the joke, and it is also the thesis.
  std::uint32_t seed = 0x5EED1234u;
  const float span = std::min(max.x - min.x, max.y - min.y);
  for (int i = 0; i < 14; ++i) {
    const int k1 = i % kQuinticN;
    const int k2 = (i * 2 + 1) % kQuinticN;
    const float du = (nextFloat(seed) - 0.5f) * 0.12f;
    const float dv = (nextFloat(seed) - 0.5f) * 0.12f;
    float u = nextFloat(seed) * 1.5f + t * du;
    float v = nextFloat(seed) * 2.0f - 1.0f + t * dv;
    u = std::fmod(std::fmod(u, kPi * 0.5f) + kPi * 0.5f, kPi * 0.5f);
    v = std::sin(v) * 0.85f;
    const Projected p = cam(quintic(k1, k2, u, v));
    const float size = span * (0.030f + 0.012f / (1.0f + std::abs(p.depth))) * 0.75f;
    const bool white = i % 2 == 0;
    icon(dl, iconStyle, kPieceShapes[static_cast<std::size_t>(i) % 7],
         ImVec2(p.at.x, p.at.y - size * 0.2f), size, u32(white ? pale : dark, 0.94f),
         u32(white ? dark : pale, 0.7f));
  }
}

void drawLattice(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                 IconStyle iconStyle, float t, const VariantSpec* variant, float yawTurn,
                 float elevTurn) {
  // How many boards, and how deep: read off the variant rather than hardcoded, so the
  // decoration is genuinely about the thing you are choosing.
  int across = 1, down = 1, layers = 1;
  int n = 8;
  if (variant != nullptr) {
    n = std::clamp<int>(variant->dims.extent(0), 3, 10);
    // Capped at two: the picture has to say "this variant has more boards than one",
    // and a three-by-three grid says it no better while making every cell too small to
    // read as a board at all.
    int extra = 0;
    for (std::uint8_t a = 2; a < variant->dims.dims(); ++a) {
      const int e = std::clamp<int>(variant->dims.extent(a), 1, 2);
      if (extra == 0) {
        across = e;
      } else if (extra == 1) {
        down = e;
      } else {
        layers = std::max(layers, e);
      }
      ++extra;
    }
    if (variant->dims.dims() == 3) {
      layers = std::clamp<int>(variant->dims.extent(2), 1, 3);
      across = 1;
    }
  }

  // Size the whole arrangement to the space it has, rather than picking a per-cell
  // scale and hoping: a nine-board lattice is nearly four times the width of one board,
  // and a constant that suits one of them runs the other off the edge of the pane.
  const float unitsX = static_cast<float>(n) +
                       static_cast<float>(across - 1) * (static_cast<float>(n) + 3);
  const float unitsY =
      static_cast<float>(n) + static_cast<float>(down - 1) * (static_cast<float>(n) + 3);
  Camera cam;
  cam.centre = ImVec2(0, 0);
  cam.scale = 1.0f;
  cam.yaw = t * 0.16f + yawTurn;
  cam.pitch = std::clamp(0.82f + std::sin(t * 0.09f) * 0.09f + elevTurn, -1.35f, 1.35f);
  cam.perspective = 0.06f;

  // Fit by measuring, not by guessing a constant: the arrangement turns, and a lattice
  // seen nearly edge-on is a very different shape on screen from the same lattice seen
  // flat. Projecting its eight corners at unit scale gives the exact extent to divide by.
  {
    const float zSpan = static_cast<float>(layers - 1) * 1.2f + 0.6f;
    float extX = 0.001f;
    float extY = 0.001f;
    for (const float sx : {-1.0f, 1.0f}) {
      for (const float sy : {-1.0f, 1.0f}) {
        for (const float sz : {-1.0f, 1.0f}) {
          const Projected c = cam({sx * unitsX * 0.5f, sy * unitsY * 0.5f, sz * zSpan});
          extX = std::max(extX, std::abs(c.at.x));
          extY = std::max(extY, std::abs(c.at.y));
        }
      }
    }
    cam.scale = std::min((max.x - min.x) * 0.46f / extX, (max.y - min.y) * 0.46f / extY);
    cam.centre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  }

  const view::Rgba dark = theme.light ? theme.bone : theme.ink;
  const view::Rgba pale = theme.light ? theme.panel : theme.bone;
  const float half = static_cast<float>(n) * 0.5f;

  // Every cell of every board, painted far to near. Without the sort, boards at
  // different depths overwrite one another wherever the projection overlaps them, which
  // is what put dark patches across a nine-board 5D lattice at some angles.
  struct Tile {
    ImVec2 p[4];
    float depth{0};
    bool light{false};
  };
  struct Token {
    ImVec2 at;
    float depth{0};
    int shape{0};
    bool white{false};
  };
  std::vector<Tile> tiles;
  std::vector<Token> tokens;
  tiles.reserve(static_cast<std::size_t>(across * down * layers * n * n));

  for (int bx = 0; bx < across; ++bx) {
    for (int by = 0; by < down; ++by) {
      for (int lz = 0; lz < layers; ++lz) {
        const float ox =
            (static_cast<float>(bx) - static_cast<float>(across - 1) * 0.5f) *
            (static_cast<float>(n) + 3);
        const float oy = (static_cast<float>(by) - static_cast<float>(down - 1) * 0.5f) *
                         (static_cast<float>(n) + 3);
        const float oz =
            (static_cast<float>(lz) - static_cast<float>(layers - 1) * 0.5f) * 2.4f;
        for (int i = 0; i < n; ++i) {
          for (int j = 0; j < n; ++j) {
            const float x = ox - half + static_cast<float>(i);
            const float y = oy - half + static_cast<float>(j);
            const Projected a = cam({x, y, oz});
            const Projected b = cam({x + 1, y, oz});
            const Projected c = cam({x + 1, y + 1, oz});
            const Projected d = cam({x, y + 1, oz});
            Tile tile;
            tile.p[0] = a.at;
            tile.p[1] = b.at;
            tile.p[2] = c.at;
            tile.p[3] = d.at;
            tile.depth = (a.depth + b.depth + c.depth + d.depth) * 0.25f;
            tile.light = (i + j) % 2 != 0;
            tiles.push_back(tile);
          }
        }
        for (int g = 0; g < 4; ++g) {
          const float x = ox - half + 0.5f + static_cast<float>(g) * 2.0f;
          const float y = oy - half + 0.5f + static_cast<float>((g + bx + by) % 3);
          const Projected at = cam({x, y, oz});
          // Just in front of the cell it stands on, so it is never sorted behind it.
          tokens.push_back(Token{at.at, at.depth - 0.01f, g, g % 2 == 0});
        }
      }
    }
  }

  std::sort(tiles.begin(), tiles.end(),
            [](const Tile& a, const Tile& b) { return a.depth < b.depth; });
  for (const Tile& tile : tiles) {
    dl->AddQuadFilled(
        tile.p[0], tile.p[1], tile.p[2], tile.p[3],
        u32(withAlpha(tile.light ? pale : dark, tile.light ? 0.20f : 0.34f)));
  }
  std::sort(tokens.begin(), tokens.end(),
            [](const Token& a, const Token& b) { return a.depth < b.depth; });
  for (const Token& tok : tokens) {
    icon(dl, iconStyle, kPieceShapes[static_cast<std::size_t>(tok.shape)],
         ImVec2(tok.at.x, tok.at.y - cam.scale * 0.2f), cam.scale * 0.92f,
         u32(tok.white ? pale : dark, 0.95f), u32(tok.white ? dark : pale, 0.6f));
  }
}

/// The 16 vertices of a 4-cube after its two-plane rotation and the fourth-axis
/// perspective divide. Shared by the Settings screen's tesseract object and the field's
/// own wireframe body, so the two cannot drift apart.
void tesseractVertices(Vec3 out[16], float a, float b) {
  for (int i = 0; i < 16; ++i) {
    const float x = static_cast<float>((i & 1) * 2 - 1);
    const float y = static_cast<float>(((i >> 1) & 1) * 2 - 1);
    const float z = static_cast<float>(((i >> 2) & 1) * 2 - 1);
    const float w = static_cast<float>(((i >> 3) & 1) * 2 - 1);
    // Turned in the xw and yz planes: the two rotations that make a 4-cube legible
    // rather than merely busy.
    const float nx = x * std::cos(a) - w * std::sin(a);
    const float nw = x * std::sin(a) + w * std::cos(a);
    const float ny = y * std::cos(b) - z * std::sin(b);
    const float nz = y * std::sin(b) + z * std::cos(b);
    const float k = 1.0f / (2.4f - nw * 0.85f);
    out[i] = {nx * k * 2.2f, ny * k * 2.2f, nz * k * 2.2f};
  }
}

void drawTesseract(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                   float t, float yawTurn, float elevTurn) {
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  // A decoration beside a menu, not the subject: half the size it used to be.
  const float scale = std::min(max.x - min.x, max.y - min.y) * 0.13f;
  const float a = t * 0.14f + yawTurn, b = t * 0.09f;

  Vec3 cube[16];
  tesseractVertices(cube, a, b);
  ImVec2 pts[16];
  for (int i = 0; i < 16; ++i) {
    const Vec3 p = rotateX(cube[i], std::clamp(0.35f + elevTurn, -1.35f, 1.35f));
    const float pp = 1.0f / (1.0f + p.z * 0.2f);
    pts[i] = ImVec2(centre.x + p.x * scale * pp, centre.y - p.y * scale * pp);
  }

  const view::Rgba line = theme.light ? theme.bone : theme.boneDim;
  for (int i = 0; i < 16; ++i) {
    for (int d = 0; d < 4; ++d) {
      const int j = i ^ (1 << d);
      if (j < i) continue;
      // The fourth-axis edges are drawn fainter: they are the ones that are not really
      // there, and saying so is the whole reason a 4-cube is worth drawing.
      dl->AddLine(pts[i], pts[j], u32(withAlpha(line, d == 3 ? 0.20f : 0.42f)), 1.4f);
    }
  }
  for (const ImVec2& p : pts) {
    dl->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + 2, p.y + 2),
                      u32(withAlpha(line, 0.85f)));
  }
}

// ---------------------------------------------------------------------------
// The field's wireframe vocabulary.
//
// Five parametric geometries, each reduced to a handful of line segments in a unit-ish
// space and projected through the same local Camera the other decorations use. They are
// self-contained rather than read off the board's own surface functions: this is ambient
// 2-D decoration that never touches the Vulkan pipeline, and the board surfaces carry
// board-scale constants and gluing concerns a background does not need. The quintic is
// deliberately absent - it is one surface only when its patches are filled and checkered
// (`drawManifold`), and reads as noise as bare line art at clutter scale.
// ---------------------------------------------------------------------------
struct WireSeg {
  Vec3 a;
  Vec3 b;
};

Vec3 torusWirePoint(float u, float v) {
  constexpr float kRing = 1.0f;
  constexpr float kTube = 0.42f;
  const float rr = kRing + kTube * std::cos(v);
  return {rr * std::cos(u), kTube * std::sin(v), rr * std::sin(u)};
}

Vec3 kleinWirePoint(float u, float v) {
  // The figure-eight immersion: a circular cross-section cannot close this gluing, the
  // lemniscate can. Normalised to about a unit cube.
  constexpr float kA = 2.0f;
  const float r =
      kA + std::cos(u * 0.5f) * std::sin(v) - std::sin(u * 0.5f) * std::sin(2.0f * v);
  return {r * std::cos(u) * 0.30f,
          (std::sin(u * 0.5f) * std::sin(v) + std::cos(u * 0.5f) * std::sin(2.0f * v)) *
              0.42f,
          r * std::sin(u) * 0.30f};
}

Vec3 mobiusWirePoint(float u, float v) {
  const float r = 1.0f + 0.42f * v * std::cos(u * 0.5f);
  return {r * std::cos(u), 0.42f * v * std::sin(u * 0.5f), r * std::sin(u)};
}

void addGridBinding(std::vector<WireSeg>& out, Vec3 (*f)(float, float), int nu, int nv,
                    float v0, float v1) {
  for (int i = 0; i < nu; ++i) {
    const float u0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(nu);
    const float u1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(nu);
    for (int j = 0; j < nv; ++j) {
      const float va = v0 + (v1 - v0) * static_cast<float>(j) / static_cast<float>(nv);
      const float vb =
          v0 + (v1 - v0) * static_cast<float>(j + 1) / static_cast<float>(nv);
      out.push_back({f(u0, va), f(u1, va)});
      out.push_back({f(u0, va), f(u0, vb)});
    }
  }
}

void buildWireShape(WireShape shape, std::vector<WireSeg>& out, float yaw, float pitch) {
  switch (shape) {
    case WireShape::Torus:
      addGridBinding(out, torusWirePoint, 14, 8, 0.0f, 2.0f * kPi);
      break;
    case WireShape::Klein:
      addGridBinding(out, kleinWirePoint, 16, 8, 0.0f, 2.0f * kPi);
      break;
    case WireShape::Mobius:
      addGridBinding(out, mobiusWirePoint, 20, 4, -1.0f, 1.0f);
      break;
    case WireShape::Cube: {
      Vec3 pts[8];
      for (int i = 0; i < 8; ++i) {
        pts[i] = {static_cast<float>((i & 1) * 2 - 1),
                  static_cast<float>(((i >> 1) & 1) * 2 - 1),
                  static_cast<float>(((i >> 2) & 1) * 2 - 1)};
      }
      for (int i = 0; i < 8; ++i) {
        for (int d = 0; d < 3; ++d) {
          const int j = i ^ (1 << d);
          if (j < i) continue;
          out.push_back({pts[i], pts[j]});
        }
      }
      break;
    }
    case WireShape::Tesseract: {
      // The two angles are the two independent 4-D rotation planes (`xw` and `yz`),
      // exactly as the Settings screen's `drawTesseract` turns a 4-cube - that is what
      // reads as a hypercube rather than a cube. The camera's own turn is fixed in
      // `drawWireShape`.
      Vec3 pts[16];
      tesseractVertices(pts, yaw, pitch);
      for (int i = 0; i < 16; ++i) {
        for (int d = 0; d < 4; ++d) {
          const int j = i ^ (1 << d);
          if (j < i) continue;
          out.push_back({pts[i], pts[j]});
        }
      }
      break;
    }
    case WireShape::Count:
      break;
  }
}

/// The field's line colour: a cold hue off the seam ramp, pulled towards the page's own
/// ground so a wireframe sits *under* the interface rather than glowing through it. On
/// the light page that is a pale tint; on the candlelit page it is a dark one, which is
/// what keeps the field from lighting up the dark ground it is supposed to disappear
/// into.
view::Rgba fieldHue(const view::Theme& theme, float slot) {
  const view::Rgba seam = view::seamRampColor(theme, slot);
  const view::Rgba ground = theme.light ? theme.soot : theme.ink;
  return widgets::mix(ground, seam, 0.34f);
}

void drawAtom(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float t) {
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float cell = std::min(max.x - min.x, max.y - min.y) * 0.055f;
  const view::Rgba line = theme.light ? theme.bone : theme.boneDim;

  for (int i = -5; i <= 5; ++i) {
    const float o = static_cast<float>(i) * cell;
    dl->AddLine(ImVec2(centre.x - 5 * cell, centre.y + o),
                ImVec2(centre.x + 5 * cell, centre.y + o), u32(withAlpha(line, 0.16f)));
    dl->AddLine(ImVec2(centre.x + o, centre.y - 5 * cell),
                ImVec2(centre.x + o, centre.y + 5 * cell), u32(withAlpha(line, 0.16f)));
  }

  // [1,2] on two axes: every signed permutation, arriving one after another, which is
  // exactly what the expansion does when a variant is loaded.
  int index = 0;
  for (const int pair : {0, 1}) {
    const int p = pair == 0 ? 1 : 2;
    const int q = pair == 0 ? 2 : 1;
    for (const int sx : {1, -1}) {
      for (const int sy : {1, -1}) {
        // Every direction is always there - that is the point of an atom - and the
        // sweep only says which one is being counted right now. A still frame of this
        // has to show the whole expansion, not two arrows and a lot of empty grid.
        const ImVec2 full(centre.x + static_cast<float>(p * sx) * cell,
                          centre.y - static_cast<float>(q * sy) * cell);
        dl->AddLine(centre, full, u32(withAlpha(line, 0.30f)), 1.4f);
        dl->AddCircleFilled(full, cell * 0.11f, u32(withAlpha(line, 0.45f)), 10);

        const float phase = std::fmod(t * 0.7f - static_cast<float>(index) * 0.34f, 5.6f);
        ++index;
        const float k = std::clamp(phase, 0.0f, 1.0f);
        if (k <= 0.0f) continue;
        const ImVec2 tip(centre.x + static_cast<float>(p * sx) * cell * k,
                         centre.y - static_cast<float>(q * sy) * cell * k);
        dl->AddLine(centre, tip, u32(withAlpha(theme.ember, 0.85f)), 2.2f);
        dl->AddCircleFilled(tip, cell * 0.16f, u32(theme.ember), 10);
      }
    }
  }
  icon(dl, iconStyle, Archetype::Wedge, centre, cell * 1.9f,
       u32(theme.light ? theme.bone : theme.whitePiece),
       u32(theme.light ? theme.panel : theme.ink));
}

constexpr float kZNear = 0.34f;
constexpr float kZFar = 4.6f;

}  // namespace

void drawWireShape(ImDrawList* dl, WireShape shape, ImVec2 centre, float scale, float yaw,
                   float pitch, const view::Theme& theme, float slot, float alpha) {
  if (alpha <= 0.0f) return;
  std::vector<WireSeg> segs;
  buildWireShape(shape, segs, yaw, pitch);
  if (segs.empty()) return;
  Camera cam;
  cam.centre = centre;
  cam.scale = scale;
  // For the tesseract the two angles already turned its two 4-D planes, so the camera
  // sits at the same fixed legible pose `drawTesseract` uses. Every other shape is turned
  // by the camera, and the two angles are independent yaw and pitch rather than one
  // shared spin.
  cam.yaw = shape == WireShape::Tesseract ? 0.0f : yaw;
  cam.pitch = shape == WireShape::Tesseract ? 0.35f : pitch;
  const view::Rgba line = fieldHue(theme, slot);
  for (const WireSeg& s : segs) {
    dl->AddLine(cam(s.a).at, cam(s.b).at, u32(withAlpha(line, alpha)), 1.2f);
  }
}

Deco decoForScreen(int screen, bool boardBehind) noexcept {
  switch (static_cast<app::Screen>(screen)) {
    // The main menu carries the quintic cross-section itself - the one dedicated,
    // dominant object, drawn from `drawManifold`. It is not a clutter body: reduced to a
    // sparse wireframe among the field it reads as noise, which is why the field's
    // vocabulary stops at five.
    case app::Screen::Welcome:
      return Deco::Manifold;
    // While the attract sequence plays a real game behind the menu (M13.9), the board
    // replaces the manifold rather than sharing the screen with it.
    case app::Screen::MainMenu:
      return boardBehind ? Deco::None : Deco::Manifold;
    // The library's object is the selected variant's *overture*, which the screen draws
    // itself - see Ui::drawOvertureObject. The turning lattice it replaces said only how
    // many boards a variant had; an overture says what shape they are on.
    case app::Screen::NewGame:
      return Deco::None;
    case app::Screen::Settings:
      return Deco::Tesseract;
    case app::Screen::Editor:
      return Deco::Atom;
    // The pause section and the quit prompts sit over the position, which is their
    // object; there is nothing to draw beside them.
    case app::Screen::QuitConfirm:
    case app::Screen::PauseQuitConfirm:
    case app::Screen::Paused:
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
      return Deco::None;
    default:
      return Deco::None;
  }
}

DepthField::DepthField() : DepthField(false) {}

DepthField::DepthField(bool quiet) : quiet_(quiet) {
  // Enough bodies, spread through the whole depth, that the field is a presence rather
  // than a few specks: it is the background the menus are read against, so it has to be
  // visible without ever competing with the board. A wireframe reads as more complex than
  // a filled polygon at a glance, so there are fewer bodies than the old 120 - more
  // weight each, less confetti.
  const std::uint32_t seed = quiet ? 0x9E3779B9u : 0xC0FFEEu;
  std::uint32_t s = seed;
  bodies_.resize(quiet ? 24 : 56);
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    Body& b = bodies_[i];
    // A few of them are pieces rather than wireframes: the background is made out of the
    // game, which is cheaper than inventing a second vocabulary for it. The board screen
    // has real pieces to compete with, so its field is wireframes only.
    b.piece = !quiet && i % 4 == 3;
    respawn(b, s, false);
    b.z = kZNear + nextFloat(s) * (kZFar - kZNear);
  }
}

void DepthField::respawn(Body& b, std::uint32_t& seed, bool nearPlane) const {
  // Born on an annulus, so nothing rests over the centre. Both rings now start at 1.30 -
  // the menu's used to begin at 0.62, which put a body's average well inside the floor
  // the breathe could then cross. The quiet field's ring is only slightly wider because
  // the board, not empty page, sits behind it.
  b.angle = nextFloat(seed) * 2.0f * kPi;
  b.radius = quiet_ ? 1.30f + nextFloat(seed) * 1.00f : 1.30f + nextFloat(seed) * 1.20f;
  b.angleV =
      (nextFloat(seed) < 0.5f ? -1.0f : 1.0f) * (0.006f + nextFloat(seed) * 0.013f);
  b.radiusV = 0.02f + nextFloat(seed) * 0.05f;
  b.radiusPhase = nextFloat(seed) * 2.0f * kPi;
  // Two independent orientation velocities, not one shared spin: a body wanders through
  // orientation space instead of tracing a single ellipse.
  b.yaw = nextFloat(seed) * 2.0f * kPi;
  b.pitch = nextFloat(seed) * 2.0f * kPi;
  b.yawV = (nextFloat(seed) - 0.5f) * 0.10f;
  b.pitchV = (nextFloat(seed) - 0.5f) * 0.10f;
  b.size = b.piece ? 130.0f + nextFloat(seed) * 160.0f : 95.0f + nextFloat(seed) * 210.0f;
  b.zDrift = -0.010f - nextFloat(seed) * 0.025f;
  b.alphaPhase = nextFloat(seed) * 2.0f * kPi;
  b.alphaV = 0.18f + nextFloat(seed) * 0.24f;
  b.rampPhase = nextFloat(seed);
  b.rampV = 0.02f + nextFloat(seed) * 0.05f;
  b.wire = static_cast<std::uint8_t>(nextFloat(seed) * 5.0f) % 5;
  b.shape = static_cast<std::uint8_t>(nextFloat(seed) * 7.0f) % 7;
  b.z = nearPlane ? kZNear + 0.05f : kZFar;
}

void DepthField::advance(float dt) {
  clock_ += dt;
  // An impulse, not a speed: the field answers a menu move and then settles.
  velocity_ *= std::pow(0.055f, dt);
  if (std::abs(velocity_) < 0.002f) velocity_ = 0.0f;

  std::uint32_t seed = static_cast<std::uint32_t>(clock_ * 1000.0f) | 1u;
  for (Body& b : bodies_) {
    b.angle += b.angleV * dt;
    b.yaw += b.yawV * dt;
    b.pitch += b.pitchV * dt;
    b.z += (b.zDrift + velocity_ * (b.piece ? 0.72f : 1.0f)) * dt;
    if (b.z < kZNear) respawn(b, seed, false);
    if (b.z > kZFar + 0.6f) respawn(b, seed, true);
    const float breathe = std::sin(clock_ * b.radiusV + b.radiusPhase) * 0.10f;
    float px = std::cos(b.angle) * (b.radius + breathe);
    float py = std::sin(b.angle) * (b.radius + breathe) * 0.92f;
    // The breathe rides on the spawn radius, so at its trough it could carry a body back
    // inside the floor it was born outside of. Clamp the actual distance from centre, in
    // whichever direction it is at, so no phase of the cycle crosses the floor.
    const float distance = std::hypot(px, py);
    if (distance > 1e-5f && distance < kFieldMinRadius) {
      const float k = kFieldMinRadius / distance;
      px *= k;
      py *= k;
    }
    b.px = px;
    b.py = py;
  }
}

namespace {

/// A floor in perspective, evaluated directly from elapsed seconds. In particular it
/// reads none of the confetti bodies' integrated positions or transition impulses.
void drawGridFloor(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                   IconStyle iconStyle, float seconds, float opacity) {
  const float width = max.x - min.x;
  const float height = max.y - min.y;
  if (width <= 0.0f || height <= 0.0f || opacity <= 0.0f) return;
  const float horizon = min.y + height * 0.44f;
  const float floorHeight = height * 0.56f;
  const float centre = min.x + width * 0.5f;
  const float lineWidth = std::clamp(height / 720.0f, 0.65f, 2.0f);
  const auto tint = [&](float violet) {
    const auto mix = [violet](float cyan, float purple) {
      return cyan + (purple - cyan) * violet;
    };
    // Keep the ornament's colour as well as its opacity subdued. The bright seam
    // rails and real pieces must remain the strongest lights in the room.
    return view::Rgba{theme.soot.r * 0.74f + mix(theme.rift.r, theme.emberDeep.r) * 0.26f,
                      theme.soot.g * 0.74f + mix(theme.rift.g, theme.emberDeep.g) * 0.26f,
                      theme.soot.b * 0.74f + mix(theme.rift.b, theme.emberDeep.b) * 0.26f,
                      1.0f};
  };
  const auto point = [&](float x, float inverseDepth) {
    return ImVec2(centre + x * width * 0.105f * inverseDepth,
                  horizon + floorHeight * inverseDepth);
  };
  dl->PushClipRect(min, max, true);
  // Segment the receding lines so their light dies towards the vanishing point.
  for (int column = -10; column <= 10; ++column) {
    for (int segment = 0; segment < 12; ++segment) {
      const float near = static_cast<float>(segment + 1) / 12.0f;
      const float far = static_cast<float>(segment) / 12.0f;
      dl->AddLine(point(static_cast<float>(column), far),
                  point(static_cast<float>(column), near),
                  u32(tint(0.25f), opacity * near * 0.60f), lineWidth);
    }
  }
  const float flow = seconds * 0.16f - std::floor(seconds * 0.16f);
  for (int row = 1; row <= 28; ++row) {
    const float q = 1.0f / (1.0f + static_cast<float>(row) - flow);
    dl->AddLine(ImVec2(min.x, horizon + floorHeight * q),
                ImVec2(max.x, horizon + floorHeight * q),
                u32(tint(0.45f), opacity * q * 0.65f), lineWidth);
  }
  // Packets follow actual grid lanes, turning at two junctions. Sample the tail in
  // floor space before projecting, so its depth, width and corners belong to the floor.
  constexpr std::array<float, 4> lanes{-7.0f, -3.0f, 3.0f, 7.0f};
  for (std::size_t packet = 0; packet < lanes.size(); ++packet) {
    const float seed = static_cast<float>(packet);
    const float cycle = seconds * 0.065f + seed * 0.25f;
    const float progress = cycle - std::floor(cycle);
    const float distance = std::min(progress / 0.72f, 1.0f) * 8.0f;
    const float fade = std::clamp(progress / 0.06f, 0.0f, 1.0f) *
                       std::clamp((1.0f - progress) / 0.16f, 0.0f, 1.0f) * opacity;
    const float turn = packet < 2 ? -1.0f : 1.0f;
    const auto floorPoint = [&](float along) {
      if (along < 5.0f) return ImVec2(lanes[packet], 7.0f - along);
      if (along < 7.0f) return ImVec2(lanes[packet] + turn * (along - 5.0f), 2.0f);
      return ImVec2(lanes[packet] + turn * 2.0f, 9.0f - along);
    };
    const auto project = [&](ImVec2 p) { return point(p.x, 1.0f / (1.0f + p.y)); };
    const view::Rgba colour =
        widgets::shade(widgets::mix(theme.rift, theme.emberDeep, seed / 3.0f), 0.70f);
    constexpr std::array<float, 4> junctions{0.0f, 5.0f, 7.0f, 9.0f};
    for (int tail = 0; tail < 12; ++tail) {
      const float strength = static_cast<float>(tail + 1) / 12.0f;
      const float end = distance - static_cast<float>(11 - tail) * 0.14f;
      const float begin = end - 0.14f;
      for (std::size_t leg = 0; leg + 1 < junctions.size(); ++leg) {
        const float a = std::max(begin, junctions[leg]);
        const float b = std::min(end, junctions[leg + 1]);
        if (a >= b) continue;
        const ImVec2 from = project(floorPoint(a));
        const ImVec2 to = project(floorPoint(b));
        const float alpha = fade * strength * strength;
        dl->AddLine(from, to, u32(colour, alpha * 0.10f), lineWidth * 6.0f);
        dl->AddLine(from, to, u32(colour, alpha * 0.85f), lineWidth * 1.2f);
      }
    }
    // A completed route boots a small chess hologram at its terminal. It is an
    // ambient outline, not another opaque piece competing with the real game.
    const float reveal = std::clamp((progress - 0.72f) / 0.10f, 0.0f, 1.0f);
    if (reveal > 0.0f) {
      const ImVec2 at = project(floorPoint(8.0f));
      const float size = height * 0.055f;
      dl->PushClipRect(ImVec2(at.x - size * 0.65f, at.y - size * reveal),
                       ImVec2(at.x + size * 0.65f, at.y + size * 0.15f), true);
      icon(dl, iconStyle, kPieceShapes[packet], ImVec2(at.x, at.y - size * 0.5f), size,
           u32(colour, fade * reveal * 0.16f), u32(colour, fade * reveal * 0.72f));
      dl->PopClipRect();
    }
  }
  dl->PopClipRect();
}

}  // namespace

void DepthField::draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                      IconStyle iconStyle, bool withPieces, bool withShapes,
                      float opacity) const {
  if (theme.backdrop == view::Backdrop::Grid) {
    if (withShapes) drawGridFloor(dl, min, max, theme, iconStyle, clock_, opacity);
    return;
  }
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float rx = (max.x - min.x) * 0.46f;
  const float ry = (max.y - min.y) * 0.52f;

  // Painter's order, so something rushing past the camera passes in front of the rest.
  std::vector<const Body*> order;
  order.reserve(bodies_.size());
  for (const Body& b : bodies_) {
    if (b.piece ? withPieces : withShapes) order.push_back(&b);
  }
  std::sort(order.begin(), order.end(),
            [](const Body* a, const Body* b) { return a->z > b->z; });

  for (const Body* body : order) {
    const Body& b = *body;
    const float k = 1.0f / b.z;
    const ImVec2 at(centre.x + b.px * rx * k * 2.1f, centre.y + b.py * ry * k * 2.1f);
    const float scale = k * 1.35f;
    // Fade in from the far plane and out as it sweeps past, so nothing ever pops.
    const float fade = std::clamp((kZFar + 0.4f - b.z) / 1.1f, 0.0f, 1.0f) *
                       std::clamp((b.z - kZNear) / 0.5f, 0.0f, 1.0f);
    if (fade <= 0.0f) continue;

    // Where this body sits on the seam ramp right now. Slow, and per-body, so the field
    // is a web of related cold hues rather than one flat colour.
    const float slot = std::fmod(clock_ * b.rampV + b.rampPhase, 1.0f);

    if (b.piece) {
      const view::Rgba tint = fieldHue(theme, slot < 0.0f ? slot + 1.0f : slot);
      icon(dl, iconStyle, kPieceShapes[b.shape], at, b.size * scale,
           u32(withAlpha(tint, opacity * fade * 0.34f)),
           u32(withAlpha(tint, opacity * fade * 0.22f)));
      continue;
    }

    // A faint breathing alpha, so the web does not read as a fixed decal in the capture.
    const float alpha =
        opacity * fade *
        (0.18f + 0.24f * (std::sin(clock_ * b.alphaV + b.alphaPhase) * 0.5f + 0.5f));
    drawWireShape(dl, static_cast<WireShape>(b.wire), at, b.size * scale * 0.62f, b.yaw,
                  b.pitch, theme, slot < 0.0f ? slot + 1.0f : slot, alpha);
  }
}

std::vector<BodyState> DepthField::bodies() const {
  std::vector<BodyState> out;
  out.reserve(bodies_.size());
  for (const Body& b : bodies_) {
    out.push_back({b.yaw, b.pitch, std::hypot(b.px, b.py), static_cast<WireShape>(b.wire),
                   b.piece});
  }
  return out;
}

void drawDeco(ImDrawList* dl, Deco what, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float time, const VariantSpec* variant, float zoom,
              float alpha, float yawTurn, float elevTurn) {
  // A decoration at alpha 0 must emit nothing at all, or the leaving screen's object
  // would still be in the draw list and the headless capture would see it.
  if (alpha <= 0.0f) return;
  // Zoom about the rect's centre: this is the camera dolly as a 2-D draw list can show
  // it, and it changes the decoration's whole scale rather than any one part.
  const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const ImVec2 lo(c.x + (min.x - c.x) * zoom, c.y + (min.y - c.y) * zoom);
  const ImVec2 hi(c.x + (max.x - c.x) * zoom, c.y + (max.y - c.y) * zoom);
  gDecoAlpha = alpha;
  switch (what) {
    case Deco::Manifold:
      drawManifold(dl, lo, hi, theme, iconStyle, time, yawTurn, elevTurn);
      break;
    case Deco::Lattice:
      drawLattice(dl, lo, hi, theme, iconStyle, time, variant, yawTurn, elevTurn);
      break;
    case Deco::Tesseract:
      drawTesseract(dl, lo, hi, theme, time, yawTurn, elevTurn);
      break;
    case Deco::Atom:
      drawAtom(dl, lo, hi, theme, iconStyle, time);
      break;
    case Deco::None:
      break;
  }
  gDecoAlpha = 1.0f;
}

}  // namespace cb::render
