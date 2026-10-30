// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace cb::app {
namespace {

std::string trim(std::string_view s) {
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0))
    s.remove_prefix(1);
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0))
    s.remove_suffix(1);
  return std::string(s);
}

bool asBool(const std::string& v) {
  return v == "true" || v == "1" || v == "yes";
}

float asFloat(const std::string& v, float fallback) {
  try {
    return std::stof(v);
  } catch (...) {
    return fallback;
  }
}

int asInt(const std::string& v, int fallback) {
  try {
    return std::stoi(v);
  } catch (...) {
    return fallback;
  }
}

const char* boolText(bool b) {
  return b ? "true" : "false";
}

}  // namespace

std::filesystem::path Settings::defaultPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "endspielzero" / "settings.conf";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".config" / "endspielzero" / "settings.conf";
  }
  return std::filesystem::path("endspielzero-settings.conf");
}

Settings Settings::load(const std::filesystem::path& path) {
  Settings s;
  std::ifstream in(path);
  if (!in) return s;  // no file yet: defaults, not an error

  std::string line;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;  // a malformed line is skipped, not fatal
    const std::string key = trim(std::string_view(line).substr(0, eq));
    const std::string value = trim(std::string_view(line).substr(eq + 1));
    if (key.empty()) continue;

    if (key == "gui_scale")
      s.guiScale = asFloat(value, s.guiScale);
    else if (key == "fullscreen")
      s.fullscreen = asBool(value);
    else if (key == "vsync")
      s.vsync = asBool(value);
    else if (key == "frame_cap")
      s.frameCap = asInt(value, s.frameCap);
    else if (key == "show_legal_moves")
      s.showLegalMoves = asBool(value);
    else if (key == "show_last_move")
      s.showLastMove = asBool(value);
    else if (key == "show_check")
      s.showCheck = asBool(value);
    else if (key == "show_seams")
      s.showSeams = asBool(value);
    else if (key == "show_seam_legend")
      s.showSeamLegend = asBool(value);
    else if (key == "show_coordinates")
      s.showCoordinates = asBool(value);
    else if (key == "swap_layer_colors")
      s.swapLayerColors = asBool(value);
    else if (key == "piece_height_scale")
      s.pieceHeightScale = asFloat(value, s.pieceHeightScale);
    else if (key == "piece_icons")
      s.pieceIcons = value;
    else if (key == "theme")
      s.theme = value;
    else if (key == "flat_view")
      s.flatView = asBool(value);
    else if (key == "geometry_view")
      s.geometryView = asBool(value);
    else if (key == "geometry_evert")
      s.geometryEvert = asFloat(value, s.geometryEvert);
    else if (key == "geometry_invert")
      s.geometryInvert = asBool(value);
    else if (key == "geometry_ghost")
      s.geometryGhost = asFloat(value, s.geometryGhost);
    else if (key == "shape_piece_cylinders")
      s.shapePieceCylinders = asBool(value);
    else if (key == "klein_twist")
      s.kleinTwist = asFloat(value, s.kleinTwist);
    else if (key == "geometry_width")
      s.geometryWidth = asFloat(value, s.geometryWidth);
    else if (key == "geometry_thickness")
      s.geometryThickness = asFloat(value, s.geometryThickness);
    else if (key == "klein_shift")
      s.kleinShift = asFloat(value, s.kleinShift);
    else if (key == "geometry_align")
      s.geometryAlign = asBool(value);
    else if (key == "follow_upright")
      s.followUpright = asBool(value);
    else if (key == "follow_elevation")
      s.followElevationDeg = asFloat(value, s.followElevationDeg);
    else if (key == "shape_morph_speed")
      s.shapeMorphSpeed = asFloat(value, s.shapeMorphSpeed);
    else if (key == "geometry_form_speed")
      s.geometryFormSpeed = asFloat(value, s.geometryFormSpeed);
    else if (key == "shape_follow")
      s.shapeFollow = value;
    else if (key == "geometry_slide_u")
      s.geometrySlideU = asFloat(value, s.geometrySlideU);
    else if (key == "geometry_slide_v")
      s.geometrySlideV = asFloat(value, s.geometrySlideV);
    else if (key == "geometry_hyper_spin_u")
      s.geometryHyperSpinU = asFloat(value, s.geometryHyperSpinU);
    else if (key == "geometry_hyper_spin_v")
      s.geometryHyperSpinV = asFloat(value, s.geometryHyperSpinV);
    else if (key == "animate_moves")
      s.animateMoves = asBool(value);
    else if (key == "animation_speed")
      s.animationSpeed = asFloat(value, s.animationSpeed);
    else if (key == "overture_speed")
      s.overtureSpeed = asFloat(value, s.overtureSpeed);
    else if (key == "orbit_sensitivity")
      s.orbitSensitivity = asFloat(value, s.orbitSensitivity);
    else if (key == "zoom_sensitivity")
      s.zoomSensitivity = asFloat(value, s.zoomSensitivity);
    else if (key == "invert_orbit_y")
      s.invertOrbitY = asBool(value);
    else if (key == "camera_mode")
      s.cameraMode = value;
    else if (key == "follow_strength")
      s.followStrength = asFloat(value, s.followStrength);
    else if (key == "confirm_moves")
      s.confirmMoves = asBool(value);
    else if (key == "hot_seat")
      s.hotSeat = asBool(value);
    else if (key == "vs_computer")
      s.vsComputer = asBool(value);
    else if (key == "computer_plays_white")
      s.computerPlaysWhite = asBool(value);
    else if (key == "auto_promote_to")
      s.autoPromoteTo = value;
    else if (key == "last_variant")
      s.lastVariant = value;
    else if (key == "trailer_morph_speed")
      s.trailerMorphSpeed = asFloat(value, s.trailerMorphSpeed);
    else if (key == "trailer_move_speed")
      s.trailerMoveSpeed = asFloat(value, s.trailerMoveSpeed);
    else if (key == "trailer_move_preview")
      s.trailerMovePreview = asFloat(value, s.trailerMovePreview);
    else if (key == "watch_dwell")
      s.watchDwell = asFloat(value, s.watchDwell);
    else if (key == "seen_welcome")
      s.seenWelcome = asBool(value);
    else if (key == "volume_master")
      s.volumeMaster = asFloat(value, s.volumeMaster);
    else if (key == "volume_music")
      s.volumeMusic = asFloat(value, s.volumeMusic);
    else if (key == "volume_effects")
      s.volumeEffects = asFloat(value, s.volumeEffects);
    // An unrecognised key is ignored: a file written by a newer build must still load.
  }
  s.sanitize();
  return s;
}

Result<void> Settings::save(const std::filesystem::path& path) const {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return fail(ErrorCode::Internal, "cannot write settings to '" + path.string() + "'");
  }
  out << "# EndspielZero settings. Edited by the game; safe to edit by hand.\n";
  out << "gui_scale = " << guiScale << '\n';
  out << "fullscreen = " << boolText(fullscreen) << '\n';
  out << "vsync = " << boolText(vsync) << '\n';
  out << "frame_cap = " << frameCap << '\n';
  out << "show_legal_moves = " << boolText(showLegalMoves) << '\n';
  out << "show_last_move = " << boolText(showLastMove) << '\n';
  out << "show_check = " << boolText(showCheck) << '\n';
  out << "show_seams = " << boolText(showSeams) << '\n';
  out << "show_seam_legend = " << boolText(showSeamLegend) << '\n';
  out << "show_coordinates = " << boolText(showCoordinates) << '\n';
  out << "swap_layer_colors = " << boolText(swapLayerColors) << '\n';
  out << "piece_height_scale = " << pieceHeightScale << '\n';
  out << "piece_icons = " << pieceIcons << '\n';
  out << "theme = " << theme << '\n';
  out << "flat_view = " << boolText(flatView) << '\n';
  out << "geometry_view = " << boolText(geometryView) << '\n';
  out << "geometry_evert = " << geometryEvert << '\n';
  out << "geometry_invert = " << boolText(geometryInvert) << '\n';
  out << "geometry_ghost = " << geometryGhost << '\n';
  out << "shape_piece_cylinders = " << boolText(shapePieceCylinders) << '\n';
  out << "klein_twist = " << kleinTwist << '\n';
  out << "geometry_width = " << geometryWidth << '\n';
  out << "geometry_thickness = " << geometryThickness << '\n';
  out << "klein_shift = " << kleinShift << '\n';
  out << "geometry_align = " << boolText(geometryAlign) << '\n';
  out << "follow_upright = " << boolText(followUpright) << '\n';
  out << "follow_elevation = " << followElevationDeg << '\n';
  out << "shape_morph_speed = " << shapeMorphSpeed << '\n';
  out << "geometry_form_speed = " << geometryFormSpeed << '\n';
  out << "shape_follow = " << shapeFollow << '\n';
  out << "geometry_slide_u = " << geometrySlideU << '\n';
  out << "geometry_slide_v = " << geometrySlideV << '\n';
  out << "geometry_hyper_spin_u = " << geometryHyperSpinU << '\n';
  out << "geometry_hyper_spin_v = " << geometryHyperSpinV << '\n';
  out << "animate_moves = " << boolText(animateMoves) << '\n';
  out << "animation_speed = " << animationSpeed << '\n';
  out << "overture_speed = " << overtureSpeed << '\n';
  out << "orbit_sensitivity = " << orbitSensitivity << '\n';
  out << "zoom_sensitivity = " << zoomSensitivity << '\n';
  out << "invert_orbit_y = " << boolText(invertOrbitY) << '\n';
  out << "camera_mode = " << cameraMode << '\n';
  out << "follow_strength = " << followStrength << '\n';
  out << "confirm_moves = " << boolText(confirmMoves) << '\n';
  out << "hot_seat = " << boolText(hotSeat) << '\n';
  out << "vs_computer = " << boolText(vsComputer) << '\n';
  out << "computer_plays_white = " << boolText(computerPlaysWhite) << '\n';
  out << "auto_promote_to = " << autoPromoteTo << '\n';
  out << "last_variant = " << lastVariant << '\n';
  out << "watch_dwell = " << watchDwell << '\n';
  out << "trailer_morph_speed = " << trailerMorphSpeed << '\n';
  out << "trailer_move_speed = " << trailerMoveSpeed << '\n';
  out << "trailer_move_preview = " << trailerMovePreview << '\n';
  out << "seen_welcome = " << boolText(seenWelcome) << '\n';
  out << "volume_master = " << volumeMaster << '\n';
  out << "volume_music = " << volumeMusic << '\n';
  out << "volume_effects = " << volumeEffects << '\n';
  if (!out) return fail(ErrorCode::Internal, "writing settings failed part-way");
  return {};
}

void Settings::sanitize() {
  guiScale = std::clamp(guiScale, 0.6f, 3.0f);
  frameCap = std::clamp(frameCap, 0, 360);
  pieceHeightScale = std::clamp(pieceHeightScale, 0.0f, 2.0f);
  geometryEvert = std::clamp(geometryEvert, 0.0f, 1.0f);
  geometryFormed = std::clamp(geometryFormed, 0.0f, 1.0f);
  geometryShellSpread = std::clamp(geometryShellSpread, 0.0f, 1.0f);
  // Never fully transparent: a board you cannot see is not a ghost, it is gone.
  geometryGhost = std::clamp(geometryGhost, 0.35f, 1.0f);
  kleinTwist = std::clamp(kleinTwist, 0.0f, 6.0f);
  geometryWidth = std::clamp(geometryWidth, 0.4f, 4.0f);
  geometryThickness = std::clamp(geometryThickness, 0.2f, 3.0f);
  kleinShift = std::clamp(kleinShift, -7.0f, 7.0f);
  // The slide is not wrapped here: its period is two laps of the *board*, which this
  // layer does not know. `PlaySurface` wraps it against the board's own extent (M17.9).
  animationSpeed = std::clamp(animationSpeed, 0.05f, 4.0f);
  overtureSpeed = std::clamp(overtureSpeed, 0.25f, 4.0f);
  orbitSensitivity = std::clamp(orbitSensitivity, 0.1f, 4.0f);
  zoomSensitivity = std::clamp(zoomSensitivity, 0.1f, 4.0f);
  volumeMaster = std::clamp(volumeMaster, 0.0f, 1.0f);
  volumeMusic = std::clamp(volumeMusic, 0.0f, 1.0f);
  volumeEffects = std::clamp(volumeEffects, 0.0f, 1.0f);
  followStrength = std::clamp(followStrength, 0.0f, 1.0f);
  followElevationDeg = std::clamp(followElevationDeg, 5.0f, 80.0f);
  shapeMorphSpeed = std::clamp(shapeMorphSpeed, 0.25f, 4.0f);
  geometryFormSpeed = std::clamp(geometryFormSpeed, 0.25f, 4.0f);
  watchDwell = std::clamp(watchDwell, 0.0f, 10.0f);
  trailerMorphSpeed = std::clamp(trailerMorphSpeed, 0.25f, 4.0f);
  trailerMoveSpeed = std::clamp(trailerMoveSpeed, 0.25f, 4.0f);
  trailerMovePreview = std::clamp(trailerMovePreview, 0.0f, 0.5f);
  if (cameraMode != "off" && cameraMode != "piece" && cameraMode != "route") {
    cameraMode = "off";
  }
  if (shapeFollow != "chase" && shapeFollow != "turntable") shapeFollow = "chase";
  if (lastVariant.empty()) lastVariant = "standard";
  if (pieceIcons != "faceted" && pieceIcons != "primitive") pieceIcons = "faceted";
  if (theme != "manifold" && theme != "console") theme = "manifold";
}

}  // namespace cb::app
