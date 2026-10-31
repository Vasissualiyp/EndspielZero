// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace cb::view {

struct Rgba {
  float r{0}, g{0}, b{0}, a{1};

  static constexpr Rgba hex(std::uint32_t rgb, float alpha = 1.0f) {
    return Rgba{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
  }
  [[nodiscard]] constexpr Rgba withAlpha(float alpha) const {
    return Rgba{r, g, b, alpha};
  }
  [[nodiscard]] constexpr std::array<float, 4> array() const { return {r, g, b, a}; }
};

/// Surface shading chosen by the theme; shader ids remain private to the renderer.
enum class Material : std::uint8_t {
  Matte = 0,
  Metal = 1,
  Wood = 2,
  Glass = 3,
  Hologram = 4,
  Glow = 5,
  Led = 7
};

struct Materials {
  Material cells{Material::Matte};
  Material pieces{Material::Matte};
  Material plinth{Material::Matte};
};

/// Ambient decoration, shared by shell screens and the room behind the board.
enum class Backdrop : std::uint8_t { Confetti, Grid };
enum class PanelStyle : std::uint8_t { Plate, Brackets };

/// The game's palette, as data.
///
/// Two themes ship. `console()` is the default, a candlelit wooden set;
/// `manifold()` is the alternative.
///
/// The rule that survives both: **one accent, and geometry gets its own colours.**
/// `ember` is the accent - selection, headings, the primary action - and the seam ramp
/// is spent *only* where the board stops being flat. A player learns in one game that
/// those hues mean "this edge is not where it looks", and that only works if nothing
/// else ever uses them.
///
/// Being a struct rather than constants means a variant, or eventually a Workshop
/// package, can carry its own without anyone touching the renderer.
struct Theme {
  Materials materials{Material::Wood, Material::Wood, Material::Wood};
  Backdrop backdrop{Backdrop::Confetti};
  PanelStyle panelStyle{PanelStyle::Plate};
  bool monoLabels{false};
  // Ground and structure. Brown-biased near-black, never a neutral grey: the scene
  // should read as candlelight on worn wood rather than as a developer tool.
  Rgba ink{Rgba::hex(0x0D0B0A)};
  Rgba soot{Rgba::hex(0x17130F)};
  Rgba panel{Rgba::hex(0x1E1813)};
  Rgba panelHi{Rgba::hex(0x2A211A)};
  Rgba rule{Rgba::hex(0x3A2E24)};
  Rgba interfaceRule{Rgba::hex(0x3A2E24)};

  // Warm side.
  Rgba bone{Rgba::hex(0xE8DCC4)};
  Rgba boneDim{Rgba::hex(0xA99A80)};
  Rgba boneFaint{Rgba::hex(0x6F6353)};
  Rgba ember{Rgba::hex(0xF0A23C)};
  Rgba emberDeep{Rgba::hex(0xA5601B)};

  // Cold side - reserved.
  Rgba rift{Rgba::hex(0x5FE3E0)};
  Rgba riftDeep{Rgba::hex(0x1E6F70)};

  // Seams. A glued edge is coloured by the portal it belongs to, swept along a cold
  // arc - green-cyan, through blue and violet, to magenta - so that the two ends of one
  // identification share a colour and a twist shows up as a reversed ramp. Hue is in
  // turns; the arc deliberately never reaches the warm half of the wheel, which belongs
  // to the game rather than to the geometry.
  float seamHueBegin{0.42f};
  float seamHueEnd{0.88f};
  float seamSaturation{0.72f};
  float seamValue{0.95f};
  /// A reflecting wall has nothing on the other side, so it gets no hue at all: it is
  /// drawn as bright metal instead, a silvered mirror rather than a coloured portal.
  Rgba mirrorEdge{Rgba::hex(0xFFFFFF)};

  // Semantic, deliberately separate from the accent.
  Rgba moss{Rgba::hex(0x8FB65C)};
  Rgba blood{Rgba::hex(0xD4483B)};
  /// The Ctrl-peek destinations and the faint "defended by" hint; distinct from moss,
  /// blood and the selection colour `ember`.
  Rgba peek{Rgba::hex(0x9B7BEA)};

  // Difficulty, reserved for the library's entries: one hue per level, from the
  // reassuring green of a game anyone can sit down to, through amber and red, to the
  // purple that says a six-dimensional torus is not one you win. `other` is aqua - the
  // colour of a variant whose difficulty nobody has declared, where a Workshop package
  // lands until its author says otherwise. Kept the same in both themes on purpose:
  // the level a colour names should not change when the page does.
  Rgba diffEasy{Rgba::hex(0x3FB950)};
  Rgba diffMedium{Rgba::hex(0xE3B341)};
  Rgba diffHard{Rgba::hex(0xE5534B)};
  Rgba diffImpossible{Rgba::hex(0xA371F7)};
  Rgba diffOther{Rgba::hex(0x2DD4BF)};

  // Lower contrast than a real chessboard, so pieces and marks stay the loudest thing -
  // but not so low that a piece disappears into the square it stands on. The old pair
  // (D9C9A8 / 5E4A34) put a pale piece on a pale square at 1.2:1, which a contrast test
  // caught; these are the nearest values in the same family that clear 2:1 all four ways.
  Rgba boardLight{Rgba::hex(0xA98F68)};
  Rgba boardDark{Rgba::hex(0x77614A)};

  /// The edge of the board as an object. Drawn under the cells so the board reads as a
  /// thing standing on the page rather than as a pattern printed on it.
  Rgba boardRim{Rgba::hex(0x241C15)};

  /// The contact shadow under a piece (M18.2). It is always darker than what it falls on,
  /// in both themes - a decal that *lightens* the board is a glow, not a shadow. It
  /// cannot be drawn from `soot`/`ink`, because those are *background* colours and flip
  /// with the page: on the light theme they are the palest values in the palette, so a
  /// shadow from them would read as a halo. The alpha is part of the colour because it is
  /// the only thing that says how strong the contact is; `test_theme` holds both ends -
  /// it must darken the square and must leave both piece colours legible on it.
  Rgba shadow{Rgba::hex(0x0A0806, 0.17f)};

  /// The disc a flat board's piece stands on.
  ///
  /// One colour for *both* sides, deliberately. Drawing a white piece as a dark figure
  /// on a light token and a black piece as a light figure on a dark one makes the two
  /// tokens read as different objects - the eye sorts them by the disc, which is the
  /// largest shape, and the figure it is supposed to be reading becomes the background.
  /// With a single token behind both, the disc is furniture and the piece is the piece.
  /// It has to clear both piece colours, and that pins it into a narrow band: too light
  /// and the white piece sinks into it, too dark and the black one does. The band
  /// overlaps the two square colours by construction, so the token's separation from the
  /// board is modest and deliberate - it is furniture, not another piece. `test_theme`
  /// holds both ends of it.
  Rgba pieceToken{Rgba::hex(0x85714F)};

  Rgba whitePiece{Rgba::hex(0xE8DCC4)};
  // Lifted off the true near-black of the ground: a black piece standing on a dark
  // cell has to stay a piece, not a hole.
  Rgba blackPiece{Rgba::hex(0x342A20)};

  /// True when the ground is lighter than the type. Anything that has to pick a
  /// contrasting colour - an outline on a pale piece, a scrim, a shadow - asks this
  /// rather than comparing luminances and guessing.
  bool light{false};

  [[nodiscard]] static Theme console() { return Theme{}; }

  /// A dark, cool glass alternative to the default wooden set.
  [[nodiscard]] static Theme manifold() {
    Theme t;
    t.materials = Materials{Material::Led, Material::Hologram, Material::Glass};
    t.backdrop = Backdrop::Grid;
    t.panelStyle = PanelStyle::Brackets;
    t.monoLabels = true;
    t.ink = Rgba::hex(0x05070F);
    t.soot = Rgba::hex(0x0A0E1A);
    t.panel = Rgba::hex(0x10192A);
    t.panelHi = Rgba::hex(0x1A2940);
    t.rule = Rgba::hex(0x304460);

    t.bone = Rgba::hex(0xDDF4FF);
    t.boneDim = Rgba::hex(0x9FBBD2);
    t.boneFaint = Rgba::hex(0x7796B2);
    t.ember = Rgba::hex(0x35E0FF);
    t.emberDeep = Rgba::hex(0x7A5CFF);
    t.interfaceRule = t.ember.withAlpha(0.22f);

    t.rift = Rgba::hex(0x35B9DB);
    t.riftDeep = Rgba::hex(0x186782);
    t.seamHueBegin = 0.48f;
    t.seamHueEnd = 0.87f;
    t.seamValue = 1.0f;
    t.seamSaturation = 0.85f;
    t.mirrorEdge = Rgba::hex(0xFFFFFF);

    t.moss = Rgba::hex(0x39BB83);
    t.blood = Rgba::hex(0xEF5278);
    t.peek = Rgba::hex(0xE7A341);

    t.boardLight = Rgba::hex(0x1C2A44);
    t.boardDark = Rgba::hex(0x0F1830);
    t.boardRim = Rgba::hex(0x0A1123);
    t.shadow = Rgba::hex(0x030711, 0.26f);
    t.pieceToken = Rgba::hex(0x30436D);
    t.whitePiece = Rgba::hex(0xDDF4FF);
    // Lighter than an ordinary black piece: against two near-black glass squares a
    // deep indigo would disappear. Hue and faceting still distinguish the sides.
    t.blackPiece = Rgba::hex(0x657CC5);
    t.light = false;
    return t;
  }
};

/// The palette a settings file names. An unknown name is the shipped one, because a
/// settings file from a newer build - or a typo - must never stop the game starting.
[[nodiscard]] inline Theme themeFromName(std::string_view name) {
  if (name == "manifold") return Theme::manifold();
  return Theme::console();
}

}  // namespace cb::view
