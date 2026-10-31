// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "render/piece_icon.hpp"
#include "variant/variant.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// The objects the shell puts beside its menus.
///
/// Every screen shows one, and each is a real thing rather than an ornament: the
/// surface people draw when they explain string theory, the lattice the chosen variant
/// actually plays on, a four-dimensional cube, a move atom expanding into the
/// directions it stands for. They are projected on the CPU and drawn as polygons
/// through the interface's own draw list, so they cost a pipeline of nothing and can be
/// captured headlessly like everything else.
enum class Deco : std::uint8_t {
  None,
  /// A Calabi-Yau quintic cross-section, tiled in semi-transparent black and white: a
  /// board wrapped onto a shape a board cannot be.
  Manifold,
  /// The selected variant's own boards, as a turning grid.
  Lattice,
  /// A 4-cube, turning in two planes at once.
  Tesseract,
  /// One move atom, expanding into every direction its magnitudes permit.
  Atom,
};

/// Which object belongs beside which screen. A free function so the mapping is stated
/// once and can be read without opening the menu code.
///
/// `boardBehind` is true when a live board is being drawn behind this screen - the main
/// menu during its attract sequence (M13.9), or a screen over a running game. The main
/// menu is the only screen whose object would otherwise draw *alongside* the board; there
/// the object gives way, because the board replaces it rather than joining it. Every
/// other screen is unchanged whichever way this is set.
[[nodiscard]] Deco decoForScreen(int screen, bool boardBehind = false) noexcept;

/// The five geometries the background field is built from: the game's own exotic shapes,
/// drawn as thin wireframes rather than filled polygons. The quintic is deliberately not
/// here - it is the main menu's one dedicated `Deco::Manifold` object, not a clutter body
/// among many.
enum class WireShape : std::uint8_t {
  Torus,
  Klein,
  Mobius,
  Cube,
  Tesseract,
  Count,
};

/// Draw one wireframe shape, centred on `centre` and `scale` pixels across, turned by
/// `yaw`/`pitch`, coloured off the theme's cold seam ramp. Exposed so the shape
/// vocabulary can be smoke-tested headlessly - this is 2-D draw-list decoration, not
/// board state.
void drawWireShape(ImDrawList* dl, WireShape shape, ImVec2 centre, float scale, float yaw,
                   float pitch, const view::Theme& theme, float slot, float alpha);

/// A read-only snapshot of one drifting body, for the tests. The field's containment
/// floor and the independence of its two orientation rates are properties of the body
/// state, and the projected draw list cannot show either (depth and the camera are folded
/// in).
struct BodyState {
  float yaw{0};
  float pitch{0};
  float centreDistance{0};  ///< from the field's centre, after the breathing offset
  WireShape wire{WireShape::Torus};
  bool piece{false};
};

/// The nearest a body may drift to the field's centre, in the field's own units.
/// `advance` clamps the breathed radius to this, so no phase of the cycle can carry a
/// body inside it.
inline constexpr float kFieldMinRadius = 1.20f;

/// The body field behind everything, and its memory of the camera.
///
/// The bodies have a real depth and are projected, rather than being sprites that
/// happen to drift: that is what lets them answer a menu move. `push` adds an impulse
/// to the whole field's z velocity, and the field swells and sweeps outward past the
/// frame as the camera goes in. Most bodies are now a wireframe rendition of one of the
/// game's own geometries, in the theme's cold seam palette; a quarter are still piece
/// icons, because the background is made of the game rather than of decoration.
class DepthField {
 public:
  DepthField();
  /// A quieter field for behind the board: fewer bodies, no piece icons (a second set of
  /// pieces drifting behind the real ones competes with the thing being played), and the
  /// bodies pushed further out so they read as a frame around the board rather than a
  /// wash over it.
  explicit DepthField(bool quiet);

  /// Move the field on. `dt` is seconds.
  void advance(float dt);
  /// Dolly the field. Positive pushes it away from the viewer - smaller, nearer the
  /// vanishing point - and negative pulls it towards the camera, which is what a menu
  /// move into a deeper screen looks like: the field rushes past as you go in.
  void push(float amount) noexcept { velocity_ += amount; }

  /// Draw it into `dl`, filling the given rectangle. The theme selects the drifting
  /// bodies or a clock-driven perspective floor; `withShapes` gates the floor.
  ///
  /// `iconStyle` is the flat-piece set, because the drifting pieces are the same shapes
  /// the board uses - the background is made of the game, not of decoration. `opacity`
  /// scales every body's alpha, which is how the game screen draws the same field much
  /// more faintly than the menus.
  void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
            IconStyle iconStyle, bool withPieces, bool withShapes,
            float opacity = 1.0f) const;

  /// A snapshot of every body's orientation and centre distance, for the tests. Only
  /// meaningful after `advance` has run at least once (the projected position is computed
  /// there); the drawing code never reads it.
  [[nodiscard]] std::vector<BodyState> bodies() const;

 private:
  struct Body {
    float px{0}, py{0};  ///< position on the plane, in units of half the frame
    float z{1};          ///< distance from the camera
    float zDrift{0};
    float angle{0}, angleV{0};
    float radius{0}, radiusV{0}, radiusPhase{0};
    /// Two independent orientation velocities, so a body tumbles through orientation
    /// space rather than retracing the single ellipse one shared `spin` used to trace.
    float yaw{0}, yawV{0}, pitch{0}, pitchV{0};
    float size{0};
    float alphaPhase{0}, alphaV{0};
    float rampPhase{0}, rampV{0};  ///< where this body sits on the seam hue ramp
    std::uint8_t wire{0};          ///< which WireShape, for the wireframe bodies
    std::uint8_t shape{0};  ///< index into the piece archetypes, for the mesh bodies
    bool piece{false};
  };
  void respawn(Body& b, std::uint32_t& seed, bool nearPlane) const;

  std::vector<Body> bodies_;
  float velocity_{0};
  float clock_{0};
  bool quiet_{false};
};

/// Draw one decorative object into a rectangle. `variant` may be null; only the lattice
/// uses it, and with nothing loaded it falls back to a single board.
///
/// `zoom` scales the object about the rect's centre (the camera dolly, as a 2-D draw list
/// can show it) and `alpha` fades every colour it emits. At `alpha` 0 it draws nothing at
/// all, so a screen on its way out leaves no geometry behind.
///
/// `yawTurn` and `elevTurn` are the viewer's own, from dragging the object: added to
/// whatever turn the object was already making rather than replacing it, so a decoration
/// someone has taken hold of keeps drifting instead of freezing under the cursor. The
/// atom is flat and ignores both.
void drawDeco(ImDrawList* dl, Deco what, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float time, const VariantSpec* variant,
              float zoom = 1.0f, float alpha = 1.0f, float yawTurn = 0.0f,
              float elevTurn = 0.0f);

}  // namespace cb::render
