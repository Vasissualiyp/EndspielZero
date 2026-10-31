// SPDX-License-Identifier: GPL-3.0-or-later
//
// A palette is not a matter of taste alone: some of it is load-bearing. These pin the
// parts that are, so a later retheme cannot quietly make a piece invisible.
#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "view/theme.hpp"

using namespace cb::view;

namespace {

/// Relative luminance, WCAG's definition - the one contrast ratios are built on.
float luminance(const Rgba& c) {
  const auto lin = [](float v) {
    return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float contrast(const Rgba& a, const Rgba& b) {
  const float la = luminance(a), lb = luminance(b);
  const float hi = std::max(la, lb), lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

}  // namespace

TEST_CASE("themes choose their surface materials", "[unit][view]") {
  const Theme wood = Theme::console();
  CHECK(wood.materials.cells == Material::Wood);
  CHECK(wood.materials.pieces == Material::Wood);
  CHECK(wood.materials.plinth == Material::Wood);
  const Materials matte{};
  CHECK(matte.cells == Material::Matte);
  CHECK(matte.pieces == Material::Matte);
  CHECK(matte.plinth == Material::Matte);
}

TEST_CASE("themes select independent ambient backgrounds", "[unit][view]") {
  CHECK(Theme::console().backdrop == Backdrop::Confetti);
  CHECK(Theme::manifold().backdrop == Backdrop::Grid);
  CHECK(Theme::console().panelStyle == PanelStyle::Plate);
  CHECK_FALSE(Theme::console().monoLabels);
  CHECK(Theme::manifold().panelStyle == PanelStyle::Brackets);
  CHECK(Theme::manifold().monoLabels);
}

TEST_CASE("manifold is a dark, legible glass alternative", "[unit][view]") {
  const Theme t = Theme::manifold();
  CHECK_FALSE(t.light);
  CHECK(luminance(t.ink) < 0.02f);
  CHECK(t.materials.cells == Material::Led);
  CHECK(t.materials.pieces == Material::Hologram);
  CHECK(t.materials.plinth == Material::Glass);
}

TEST_CASE("the peek colour is not confusable with the other board marks",
          "[unit][view]") {
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    const auto far = [&](const Rgba& o) {
      return std::abs(t.peek.r - o.r) + std::abs(t.peek.g - o.g) +
                 std::abs(t.peek.b - o.b) >
             0.4f;
    };
    CHECK(far(t.moss));
    CHECK(far(t.blood));
    CHECK(far(t.ember));
  }
}

TEST_CASE("both piece colours read on both squares", "[unit][view]") {
  // This is the constraint that set the two board values, in both themes. A piece the
  // player cannot pick out of the square it stands on is not a style choice.
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    CHECK(contrast(t.whitePiece, t.boardLight) > 1.8f);
    CHECK(contrast(t.whitePiece, t.boardDark) > 1.8f);
    CHECK(contrast(t.blackPiece, t.boardLight) > 1.8f);
    CHECK(contrast(t.blackPiece, t.boardDark) > 1.8f);
  }
}

TEST_CASE("both piece colours read on the token they stand on", "[unit][view]") {
  // A flat board draws each piece as a figure on a disc, and the disc is the *same*
  // colour for both sides - so it is the one colour that has to work twice. If either
  // piece sinks into it the flat view loses the distinction the solid view carries by
  // shape and shadow.
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    CHECK(contrast(t.whitePiece, t.pieceToken) > 2.4f);
    CHECK(contrast(t.blackPiece, t.pieceToken) > 2.4f);
    // And the token itself has to be visible against the squares it sits on, or the
    // piece appears to float on nothing.
    // Only modestly, and that is not slack: clearing both piece colours confines the
    // token to a luminance band that overlaps the squares, so a token that stood well
    // clear of the board would have to sink one of the pieces.
    CHECK(contrast(t.pieceToken, t.boardLight) > 1.18f);
    CHECK(contrast(t.pieceToken, t.boardDark) > 1.18f);
  }
}

TEST_CASE("a contact shadow does not swallow a piece", "[unit][view]") {
  // M18.2: the shadow is drawn under every piece, so it becomes part of the surface the
  // piece is read against. Composited at its own alpha it must still leave both piece
  // colours legible on both squares, and it must darken the square - a shadow that
  // *lightens* the board (which is what `soot`/`ink` do on the light theme, because they
  // are background colours) is a glow, not a shadow.
  const auto over = [](const Rgba& under, const Rgba& shadow) {
    const float a = shadow.a;
    return Rgba{under.r * (1.0f - a) + shadow.r * a, under.g * (1.0f - a) + shadow.g * a,
                under.b * (1.0f - a) + shadow.b * a, 1.0f};
  };
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    for (const Rgba& square : {t.boardLight, t.boardDark}) {
      const Rgba shadowed = over(square, t.shadow);
      CHECK(luminance(shadowed) < luminance(square));
      CHECK(contrast(t.whitePiece, shadowed) > 1.8f);
      CHECK(contrast(t.blackPiece, shadowed) > 1.8f);
    }
  }
}

TEST_CASE("body text is legible on the ground it is drawn on", "[unit][view]") {
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    CHECK(contrast(t.bone, t.ink) > 7.0f);       // primary type
    CHECK(contrast(t.boneDim, t.panel) > 4.5f);  // secondary type on a plate
    CHECK(contrast(t.ember, t.ink) > 3.0f);      // the accent, against the ground
  }
}

TEST_CASE("the manifold board has distinct square tones and a darker rim",
          "[unit][view]") {
  const Theme t = Theme::manifold();
  CHECK_FALSE(t.light);
  CHECK(luminance(t.boardDark) < luminance(t.boardLight));
  CHECK(luminance(t.boardRim) < luminance(t.boardDark));
}

TEST_CASE("the candlelit theme is still dark, and unchanged", "[unit][view]") {
  const Theme t = Theme::console();
  CHECK_FALSE(t.light);
  CHECK(luminance(t.ink) < 0.02f);
  CHECK(luminance(t.bone) > luminance(t.ink));
}

TEST_CASE("a palette is chosen by name, and an unknown name is the shipped one",
          "[unit][view]") {
  CHECK(themeFromName("manifold").materials.cells == Material::Led);
  CHECK_FALSE(themeFromName("console").light);
  // A settings file from a newer build, or a typo, still produces a usable palette.
  CHECK(themeFromName("tesseract").light == themeFromName("console").light);
}
