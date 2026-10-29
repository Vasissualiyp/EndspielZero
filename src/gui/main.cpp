// SPDX-License-Identifier: GPL-3.0-or-later
//
// The playable window.
//
// Deliberately thin. Which screen the player is on, and what happens to a game while
// they are elsewhere, is app::Shell - covered by tests that never open a window. Every
// pixel comes from the renderer and the interface, covered by headless image tests.
// What is left here is translating platform events and deciding when to redraw, which
// is the only part that genuinely needs a display.
#include <SDL3/SDL.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <ctime>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/game_runner.hpp"
#include "app/shell.hpp"
#include "audio/audio.hpp"
#include "io/game_file.hpp"
#include "io/variant_toml.hpp"
#include "render/board_renderer.hpp"
#include "render/image_io.hpp"
#include "render/window.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/play_surface.hpp"
#include "render/shape_sequence.hpp"
#include "render/ui.hpp"
#endif

using namespace cb;

namespace {

/// Where the variants live, whether running from the build tree or an install.
std::filesystem::path variantDir() {
  for (const char* base : {"variants", "../variants", "../../variants",
                           "share/endspielzero/variants", "/usr/share/endspielzero/variants"}) {
    if (std::filesystem::is_directory(base)) return base;
  }
  return "variants";
}

std::vector<std::string> variantLibrary() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(variantDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".toml") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

/// Where the bundled showcase games live (M12.8), resolved the way the variants are:
/// beside the binary in an install, or in the working tree during development.
std::filesystem::path gameDir() {
  for (const char* base : {"games", "../games", "../../games", "share/endspielzero/games",
                           "/usr/share/endspielzero/games"}) {
    if (std::filesystem::is_directory(base)) return base;
  }
  return "games";
}

/// Read every bundled game once, at startup. A file that will not parse is skipped with a
/// message rather than taking the menu down with it.
std::vector<app::WatchGame> bundledGames() {
  std::vector<std::filesystem::path> paths;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(gameDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".cbgame")
      paths.push_back(e.path());
  }
  std::sort(paths.begin(), paths.end());
  std::vector<app::WatchGame> games;
  for (const std::filesystem::path& path : paths) {
    std::ifstream in(path);
    if (!in) continue;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto parsed = parseGameFile(buffer.str());
    if (!parsed.has_value()) {
      std::fprintf(stderr, "bundled game '%s': %s\n", path.c_str(),
                   parsed.error().format().c_str());
      continue;
    }
    app::WatchGame g;
    g.name = path.stem().string();
    g.file = std::move(*parsed);
    games.push_back(std::move(g));
  }
  return games;
}

/// The unshifted character a key produces, for the two-player keyboard; '\0' for a key
/// that is not part of either half. It is the key's identity, so the halves are the same
/// on any layout that has the keys.
char keyChar(SDL_Keycode key) {
  switch (key) {
    case SDLK_Q:
      return 'q';
    case SDLK_W:
      return 'w';
    case SDLK_E:
      return 'e';
    case SDLK_R:
      return 'r';
    case SDLK_T:
      return 't';
    case SDLK_A:
      return 'a';
    case SDLK_S:
      return 's';
    case SDLK_D:
      return 'd';
    case SDLK_F:
      return 'f';
    case SDLK_G:
      return 'g';
    case SDLK_Z:
      return 'z';
    case SDLK_X:
      return 'x';
    case SDLK_C:
      return 'c';
    case SDLK_V:
      return 'v';
    case SDLK_B:
      return 'b';
    case SDLK_Y:
      return 'y';
    case SDLK_U:
      return 'u';
    case SDLK_I:
      return 'i';
    case SDLK_O:
      return 'o';
    case SDLK_P:
      return 'p';
    case SDLK_H:
      return 'h';
    case SDLK_J:
      return 'j';
    case SDLK_K:
      return 'k';
    case SDLK_L:
      return 'l';
    case SDLK_N:
      return 'n';
    case SDLK_M:
      return 'm';
    case SDLK_COMMA:
      return ',';
    case SDLK_PERIOD:
      return '.';
    case SDLK_SEMICOLON:
      return ';';
    case SDLK_APOSTROPHE:
      return '\'';
    case SDLK_SLASH:
      return '/';
    case SDLK_BACKSLASH:
      return '\\';
    case SDLK_TAB:
      return '\t';
    default:
      return '\0';
  }
}

std::unique_ptr<app::Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = app::Shell::create(variantLibrary(), settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(variantDir() / (name + ".toml"));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  // The editor reads and writes the author's own file, not the resolved spec.
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    const auto path = variantDir() / (name + ".toml");
    std::ifstream in(path);
    if (!in) {
      return fail(ErrorCode::ParseError,
                  "cannot open variant file '" + path.string() + "'");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  });
  shell->setVariantPathResolver(
      [](const std::string& name) { return variantDir() / (name + ".toml"); });
  shell->setBundledGames(bundledGames());
  return shell;
}

render::BoardOptions optionsFrom(const app::Settings& s) {
  render::BoardOptions o;
  o.showLegalMoves = s.showLegalMoves;
  o.showLastMove = s.showLastMove;
  o.swapLayerColors = s.swapLayerColors;
  o.showCheck = s.showCheck;
  o.showSeams = s.showSeams;
  o.pieceHeightScale = s.pieceHeightScale;
  o.flat = s.flatView;
  // The player's own slide plus the anti-clip's transient offset. Both the renderer and
  // `poseFrom` must fold the offset in: the camera was targeting the aligned surface
  // while the board was still drawn unaligned, so the align affected the camera but the
  // shape never visibly turned (M17.17).
  o.surfaceSlideU = s.geometrySlideU + s.geometryAlignOffset;
  o.surfaceSlideV = s.geometrySlideV + s.geometryAlignOffsetV;
  o.surfaceHyperSpinU = s.geometryHyperSpinU;
  o.surfaceHyperSpinV = s.geometryHyperSpinV;
  o.surfaceEvert = s.geometryEvert;
  o.surfaceGhost = s.geometryGhost;
  o.surfaceTwist = s.kleinTwist;
  o.surfaceOpenness = s.geometryWidth;
  o.surfaceThickness = s.geometryThickness;
  o.surfaceCollapseSquares = s.kleinShift;
  o.surfaceFormed = s.geometryFormed;
  o.surfaceShellSpread = s.geometryShellSpread;
  o.shapePieceCylinders = s.shapePieceCylinders;
  return o;
}

/// The board options for the shell's current variant: the geometry view is offered only
/// where there is a surface to become, so `standard` and every non-glued board stay flat.
render::BoardOptions optionsFor(const app::Shell& shell) {
  render::BoardOptions o = optionsFrom(shell.settings());
#ifdef CB_HAVE_IMGUI
  const app::Session* session = shell.session();
  const app::Settings& s = shell.settings();
  // The surface path stays on while the board is forming or un-forming, so the morph is
  // drawn all the way down to flat rather than snapping the instant SHAPE is toggled
  // (M18.6). Flat 2D wins if both are set: the renderer also refuses the surface under
  // `options.flat`, but stating it here keeps the camera and the draw agreeing.
  const bool wantSurface = (s.geometryView || s.geometryFormed > 1e-4f) && !s.flatView;
  o.surface =
      wantSurface && session != nullptr && render::hasPlaySurface(session->variant());
#else
  (void)shell;
#endif
  return o;
}

#ifdef CB_HAVE_IMGUI
/// How the geometry view's surface is posed right now. The variant argument is needed
/// for the Klein collapse phase; the pose is otherwise the settings' own (M12.6 delegates
/// to `render::surfacePose`, the one definition the runner shares).
render::SurfacePose poseFrom(const app::Settings& s, const VariantSpec& v) {
  return render::surfacePose(s, v);
}

namespace {

float wrapAngle(float a) {
  constexpr float kPi = 3.14159265358979f;
  constexpr float kTwoPi = 2.0f * kPi;
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

/// Blend the player's own camera towards a follow pose by `k` in [0,1], the same way
/// `Session::cameraOver` folds in the flat move camera: yaw and roll wrap, pitch stays
/// off its poles, and the target and distance interpolate. The follow then eases in and
/// out with `view::shotEnvelope`, so a move neither snaps onto the piece at the first
/// frame nor jerks back at the last.
view::OrbitCamera blendCamera(const view::OrbitCamera& base,
                              const view::OrbitCamera& want, float k) {
  view::OrbitCamera out = base;
  out.target = base.target + (want.target - base.target) * k;
  out.yaw = base.yaw + wrapAngle(want.yaw - base.yaw) * k;
  out.pitch = std::clamp(base.pitch + (want.pitch - base.pitch) * k, 0.10f, 1.53f);
  out.distance = std::max(1.0f, base.distance + (want.distance - base.distance) * k);
  out.roll = base.roll + wrapAngle(want.roll - base.roll) * k;
  return out;
}

}  // namespace

/// The seats as a placement list, so the move camera can be asked for its blend against
/// the shape instead of the flat layout (M17.16).
std::vector<view::Placement> surfacePlacements(const render::PlaySurface& surf) {
  std::vector<view::Placement> out;
  out.reserve(surf.seats().size());
  for (const render::SurfaceSeat& s : surf.seats()) {
    out.push_back({s.cell, s.centre.x, s.centre.y, s.centre.z, 0});
  }
  return out;
}

/// The effective camera for what is drawn right now: the move-camera blend against the
/// shape while the geometry view is on, the session's own camera otherwise. One function
/// so the render and the pick cannot ask for different cameras (ADR-0011).
///
/// While a shape-follow choreography is running (`seq.active`) that is the camera,
/// because it is already a blend of the player's own view and the follow (M17.19). The
/// rest of this is the capture path's steady-state follow - a `--clip` of a move never
/// runs the choreography, and its frames must stay byte-for-byte what they were.
view::OrbitCamera boardCameraBase(const app::Shell& shell,
                                  const render::ShapeMoveSequence& seq);

view::OrbitCamera boardCamera(const app::Shell& shell,
                              const render::ShapeMoveSequence& seq) {
  const app::Session* session = shell.session();
  if (session == nullptr) return {};
  // The trailer's resting sway rides on whatever the base camera is (identity outside
  // it).
  return shell.trailerView(boardCameraBase(shell, seq));
}

view::OrbitCamera boardCameraBase(const app::Shell& shell,
                                  const render::ShapeMoveSequence& seq) {
  const app::Session* session = shell.session();
  if (session == nullptr) return {};
  const app::Settings& st = shell.settings();
  if (optionsFor(shell).surface) {
    if (seq.active) return seq.camera;
    const render::PlaySurface surf =
        render::PlaySurface::build(session->variant(), poseFrom(st, session->variant()));
    if (!surf.empty()) {
      const view::OrbitCamera settled = session->playerCamera();
      if (session->shotInFlight()) {
        const view::MovePath& path = session->animation().path();
        const float t = session->animation().progress();
        view::OrbitCamera want = settled;
        if (st.shapeFollow == "chase") {
          // A third-person chase: the piece centred, the camera behind it along its
          // route and standing it upright on its normal (M17.16), close enough that the
          // piece is the subject. The anti-clip turns the board so this does not put the
          // camera inside the tube.
          want =
              render::surfaceFollowCamera(path, surf, t, render::chaseEyeDistance(surf),
                                          st.followUpright, render::followLift(st));
        } else {
          // Turntable: the camera angle is the player's; only the look-at follows the
          // piece. The anti-clip turns the *board* so nothing comes between them
          // (M17.16/17).
          want.target = render::surfaceMoveSample(path, surf, t).position;
        }
        // Ease in and out at the ends, and respect the follow-strength setting, so the
        // shape follow settles like the flat board's does rather than snapping.
        return blendCamera(settled, want,
                           session->followStrength() * view::shotEnvelope(t));
      }
      return settled;
    }
  }
  return session->camera();
}

/// The seat the followed move is on right now, by nearest seat to the sampled position.
/// Aligning for the *current* cell rather than the destination keeps a long move clear
/// of the shape the whole way across, not only at the square it lands on. (The full
/// choreography lives in `render/shape_sequence`; this wrapper only detects a new move.)

/// Advance the shape-follow choreography one frame (M17.19), interactively. The
/// per-frame body is `render::stepShapeSequenceOnce`; this wrapper detects a move
/// beginning and starts the sequence for it, so the interactive loop and the M12.6
/// runner share one implementation.
void updateShapeSequence(app::Shell& shell, render::ShapeMoveSequence& seq, float dt) {
  app::Session* session = shell.session();
  app::Settings& st = shell.settings();
  if (session == nullptr || !shell.showsBoard() || !st.geometryView ||
      !render::hasPlaySurface(session->variant())) {
    seq.active = false;
    return;
  }
  if (st.cameraMode == "off" || session->followStrength() <= 0.0f) {
    seq.active = false;
    return;
  }

  const view::MovePath& path = session->animation().path();
  if (session->animation().active() &&
      (!seq.active || seq.tokenFrom != path.from || seq.tokenTo != path.to)) {
    // A new move. The camera to start from is the last one actually drawn, so a move that
    // begins while an earlier return is still running has no cut.
    const view::OrbitCamera from = seq.active ? seq.camera : session->playerCamera();
    seq = render::beginShapeSequence(
        shell, path, std::max(1e-3f, session->animation().duration()), from);
    session->setMoveProgress(0.0f);
  }
  if (!seq.active) return;
  render::stepShapeSequenceOnce(shell, seq, dt);
}

/// Put the camera round whatever the board has just become.
///
/// The shape and the flat board are different sizes and sit in different places, so a
/// toggle that left the camera where it was left the board off in a corner of the
/// window. The angle the player was looking from is kept: only the frame moves.
void frameBoard(app::Shell& shell) {
  app::Session* session = shell.session();
  if (session == nullptr) return;
  render::frameGeometryCamera(*session, optionsFor(shell),
                              poseFrom(shell.settings(), session->variant()));
}

/// One frame of the trailer's automatic framing (M13.9): ease the camera's target and
/// distance toward the shape's bounds as it is *now*, so a growing shape stays in view
/// and a cut between two pictures of the same shape moves nothing. The angle is never
/// touched. A first-order lag (about a quarter-second) rather than a snap: the framing
/// target moves continuously, and so does the camera that follows it.
void syncShapeCentre(app::Shell& shell) {
  app::Session* session = shell.session();
  if (session == nullptr) return;
  const render::BoardOptions options = optionsFor(shell);
  if (!options.surface) {
    shell.setTrailerShapeCentre({3.5f, 3.5f, 0.7f});
    return;
  }
  const render::PlaySurface surf =
      render::PlaySurface::build(session->variant(), poseFrom(shell.settings(), session->variant()));
  if (surf.empty()) return;
  const view::Bounds b = surf.bounds();
  // The ordinary board is looked at 0.7 above its plane; keep that lift over the shape.
  shell.setTrailerShapeCentre({b.centerX(), b.centerY(), b.centerZ() + 0.7f});
}

void easeTrailerFraming(app::Shell& shell, float dt) {
  app::Session* session = shell.session();
  if (session == nullptr || !shell.trailerAutoFrames()) return;
  const render::BoardOptions options = optionsFor(shell);
  if (!options.surface) return;
  const float k = 1.0f - std::exp(-dt * 4.0f);
  render::easeMorphFraming(*session, options,
                           poseFrom(shell.settings(), session->variant()), k);
}

/// Put the renderer in the attract sequence's presentation (M13.9): the shape view where
/// there is a shape. The camera policy is the shell's (`Shell::trailerCamera`, applied to
/// each beat's session) - this must not touch `Settings::cameraMode`, which is the
/// player's, and it applies on a flat board too, where the move camera used to swing
/// behind every piece of the longest beat. It deliberately does not set `geometryFormed`,
/// which the attract driver eases itself so the first beat can reveal.
void applyAttractLook(app::Shell& shell) {
  app::Session* session = shell.session();
  if (session == nullptr) return;
  if (render::hasPlaySurface(session->variant())) shell.settings().geometryView = true;
}
#endif

/// Tell the renderer what the engine says just happened, so the board can show it.
void syncMarks(render::BoardRenderer& renderer, const app::Session& session) {
  const auto& history = session.game().moveHistory();
  if (history.empty()) {
    renderer.setLastMove(kInvalidCell, kInvalidCell);
  } else {
    renderer.setLastMove(history.back().from, history.back().to);
  }
  renderer.setCheckCell(
      session.game().inCheck()
          ? session.game().position().findRoyal(session.game().position().sideToMove())
          : kInvalidCell);
}

/// Render a frame - board and interface together - to a PPM, and exit.
///
/// It needs no display: SDL's dummy video driver supplies the window the interface uses
/// for input mapping, and the frame goes to the same offscreen target the headless tests
/// use. That makes the whole screen capturable on a machine with no compositor, which is
/// how the interface gets reviewed at all.
///
/// `clip` turns one frame into a sequence: `t` is swept from `clipT0` to `clipT1` over
/// `clipFrames` frames, each rendered at that fixed `t` and written as
/// `<dir>/frame_%04d.ppm`. That is the deterministic, dependency-light half of the clip
/// export (M12.2): a directory of numbered frames is the guaranteed output, and muxing a
/// video from them is an external convenience, never a codec in the engine.
/// `--trailer-edit`: a main-menu attract capture taken in the trailer's camera-edit mode.
static bool gTrailerEditCapture = false;
/// `--trailer-clip DIR`: render the whole trailer, one lap, frame by frame, to DIR
/// (M13.9). The same per-frame logic as the interactive loop at a fixed step, so the
/// render is what the trailer would show on a machine that kept up. `--user-settings`
/// renders with the player's own settings and camera track instead of the capture
/// defaults.
static std::string gTrailerClipDir;
static bool gUseUserSettings = false;
/// `--piece NAME`: the piece the designer capture opens on.
static std::string gPieceName;
/// `--ui-click "x,y;x,y"`: pointer clicks (in pixels) replayed before the capture.
static std::string gUiClicks;
static int gTrailerMaxFrames = 30 * 900;
/// `--trailer-frames`: write numbered PPMs instead of piping into ffmpeg (for analysis).
static bool gTrailerKeepFrames = false;

int captureFrame(const std::string& variantName, const std::string& path,
                 const std::string& script, const std::string& screen, float overtureT,
                 int previewDims, bool clip, int clipFrames, float clipT0, float clipT1,
                 bool cinema, const std::string& followMode,
                 const std::string& shapeFollow, float moveT, int benchFrames,
                 bool geometry, float evert, float formed, bool flatCapture, float slideU,
                 float slideV, float ghost, const std::string& playFile, float dwell,
                 int fps, float playAt, bool framesGiven, int width, int height,
                 const std::string& watchName, bool pieceCylinders,
                 bool attractTimeGiven) {
  // Captures use default settings, never the person's own. A screenshot that changes
  // because whoever ran it likes a larger interface is not a screenshot of the game -
  // and `ctest -R gui-` would then pass or fail by whose machine it ran on.
  auto shell = makeShell(gUseUserSettings ? app::Settings::defaultPath()
                                          : std::filesystem::temp_directory_path() /
                                                "endspielzero-capture-defaults.conf");
  // M12.6: a game file names its own variant and start position, and its moves are then
  // played one at a time below rather than through the bulk `applyGameFile`.
  GameFile gameFile;
  const bool playing = !playFile.empty();
  if (playing) {
    std::ifstream in(playFile);
    if (!in) {
      std::fprintf(stderr, "cannot open game file '%s'\n", playFile.c_str());
      return 1;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto parsed = parseGameFile(buffer.str());
    if (!parsed.has_value()) {
      std::fprintf(stderr, "game file: %s\n", parsed.error().format().c_str());
      return 1;
    }
    gameFile = std::move(*parsed);
    if (auto ok = shell->startGame(gameFile.variant); !ok.has_value()) {
      std::fprintf(stderr, "cannot load '%s': %s\n", gameFile.variant.c_str(),
                   ok.error().format().c_str());
      return 1;
    }
  } else if (auto ok = shell->startGame(variantName); !ok.has_value()) {
    std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(),
                 ok.error().format().c_str());
    return 1;
  }
  if (!playing && !script.empty()) {
    if (auto ok = shell->session()->applyScript(script); !ok.has_value()) {
      std::fprintf(stderr, "script: %s\n", ok.error().format().c_str());
      return 1;
    }
  }
  // A capture can state that it wants the play board as its own shape (M17), so the
  // surface is reviewable the same way every other screen is. `--formed` states the morph
  // point (M18.6); `--geometry` alone is the fully-formed shape, exactly as before.
  if (geometry) {
    shell->settings().geometryView = true;
    shell->settings().geometryFormed = formed;
  }
  // The shape view's icon-faced pucks (M17.25), so a capture can review them the way
  // every other state is reviewable. Only meaningful with the shape; ignored otherwise.
  shell->settings().shapePieceCylinders = pieceCylinders;
  if (flatCapture) {
    shell->settings().flatView = true;
    if (shell->session() != nullptr) shell->session()->setFlatView(true);
  }
  shell->settings().geometryEvert = evert;
  shell->settings().geometrySlideU = slideU;
  shell->settings().geometrySlideV = slideV;
  shell->settings().geometryGhost = ghost;
#ifdef CB_HAVE_IMGUI
  frameBoard(*shell);
#endif
  if (screen == "menu")
    shell->go(app::Screen::MainMenu);
  else if (screen == "welcome")
    shell->go(app::Screen::Welcome);
  else if (screen == "pause")
    shell->pause();
  else if (screen == "settings")
    shell->go(app::Screen::Settings);
  else if (screen == "editor")
    shell->go(app::Screen::Editor);
  else if (screen == "designer" || screen == "body" || screen == "flat") {
    // The designers need a document open, which the editor's own entry does for them.
    (void)shell->openEditor(variantName);
    shell->go(app::Screen::Editor);
  } else if (screen == "info")
    shell->go(app::Screen::GameInfo);
  else if (screen == "pieces")
    shell->go(app::Screen::PieceMoves);
  else if (screen == "newgame") {
    shell->go(app::Screen::NewGame);
    // The named variant is the one under review, so the capture shows *its* overture
    // rather than whatever the picker would have opened on.
    shell->overtures().jumpTo(app::overtureFor(variantName));
    // A canonical point in the overture's cycle, stated rather than integrated towards:
    // the capture feeds a fixed timestep on purpose, and a screenshot of the library has
    // to be the same picture every time it is taken. Late enough that the selected
    // variant's shape has formed and there is something to look at.
    shell->overtures().setProgress(overtureT);
  } else if (screen == "quit")
    shell->go(app::Screen::QuitConfirm);
  else if (screen == "watch")
    shell->go(app::Screen::Watch);

  // A capture can show the watching state itself (M12.8): start a bundled game and render
  // its board with the STOP WATCHING control, so the mode is reviewable the way every
  // screen is. A glued variant is shown as its shape with the follow choreography, the
  // same showcase the interactive action gives it.
  if (!watchName.empty()) {
    if (auto ok = shell->startWatching(watchName); !ok.has_value()) {
      std::fprintf(stderr, "watch: %s\n", ok.error().format().c_str());
      return 1;
    }
    const app::Session* session = shell->session();
#ifdef CB_HAVE_IMGUI
    if (session != nullptr && render::hasPlaySurface(session->variant())) {
      shell->settings().geometryView = true;
      shell->settings().geometryFormed = 1.0f;
      shell->settings().cameraMode = "route";
      shell->session()->setCameraMode("route");
      shell->session()->setFollowStrength(1.0f);
    }
#else
    (void)session;
#endif
  }

  // Attract review (M13.9): a main-menu capture with an explicit `--t` shows the
  // sequence at that time - 0 the flat start of the cylinder reveal, larger values the
  // later beats - so the trailer state is reviewable with the flag the library already
  // uses. The advance is fixed-step, the same reproducibility contract every other
  // capture keeps. Without `--t` the menu capture is the ordinary menu, exactly what a
  // player sees before pressing the trailer button. Skipped when the capture asked for a
  // specific shape/ghost pose or a played game, so those captures keep their meaning.
#ifdef CB_HAVE_IMGUI
  if (screen == "menu" && attractTimeGiven && !geometry && playFile.empty() &&
      watchName.empty()) {
    shell->startAttract(app::defaultAttractSequence());
    constexpr float kStep = 1.0f / 60.0f;
    for (float t = 0.0f; t < overtureT; t += kStep) {
      if (shell->session() != nullptr) shell->session()->advanceAnimation(kStep);
      shell->advanceAttract(kStep);
    }
    if (gTrailerEditCapture) shell->setTrailerEditing(true);
    applyAttractLook(*shell);
    frameBoard(*shell);
  }
#endif

  // A capture that wants to show the move camera states it rather than relying on the
  // player's settings (a capture uses defaults, where following is off). `--move-t` pins
  // the move itself, the same way `--t` pins the overture.
  if (shell->session() != nullptr) {
    if (!followMode.empty()) {
      shell->session()->setCameraMode(followMode);
      shell->session()->setFollowStrength(1.0f);
    }
    if (!shapeFollow.empty()) shell->settings().shapeFollow = shapeFollow;
    if (moveT >= 0.0f) shell->session()->setMoveProgress(moveT);
  }

  if (SDL_getenv("DISPLAY") == nullptr && SDL_getenv("WAYLAND_DISPLAY") == nullptr) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("EndspielZero", width, height, SDL_WINDOW_HIDDEN);
  if (window == nullptr) {
    std::fprintf(stderr, "cannot create a hidden window: %s\n", SDL_GetError());
    return 1;
  }

  auto ctx = render::VulkanContext::create();
  if (!ctx.has_value()) {
    std::fprintf(stderr, "%s\n", ctx.error().format().c_str());
    return 1;
  }
  auto renderer = render::BoardRenderer::create(*ctx);
  auto target = render::OffscreenTarget::create(*ctx, static_cast<std::uint32_t>(width),
                                                static_cast<std::uint32_t>(height));
  if (!renderer.has_value() || !target.has_value()) {
    std::fprintf(stderr, "cannot set up the renderer\n");
    return 1;
  }
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(*ctx, window, renderer->theme());
  if (!ui.has_value()) {
    std::fprintf(stderr, "%s\n", ui.error().format().c_str());
    return 1;
  }
  // The capture has to show the interface at the scale the player set, or it is not a
  // picture of what they would see.
  (void)(*ui)->setScale(shell->settings().guiScale);
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  // Cinema dresses the frame with a grade and a vignette, not an interface (M18.3).
  (*ui)->setCinema(cinema);
  {
    const view::Theme theme = view::themeFromName(shell->settings().theme);
    renderer->setTheme(theme);
    (*ui)->setTheme(theme);
  }
  // Which page of the editor a designer capture wants. Set here rather than inside the
  // loop: it is where the review is pointed, not something a frame decides.
  if (!gPieceName.empty()) (*ui)->openEditorPiece(gPieceName);
  if (screen == "designer") (*ui)->openEditorPage(1, 0);
  if (screen == "body") (*ui)->openEditorPage(1, 1);
  if (screen == "flat") (*ui)->openEditorPage(1, 2);
  (*ui)->setPreviewDims(previewDims);
  // Point the library at the named variant, so a capture of a variant with a derived
  // overture shows *it* rather than the standard construction the first entry opens on.
  if (screen == "newgame") (*ui)->pickVariant(variantName);
  // One frame, built and rendered at whatever `t` the caller has pinned. Split out from
  // writing so `--clip` can call it once per frame. `dt` is the interface's own clock:
  // the warm-up gives it enough to settle a screen transition, and the real frames give
  // it none so the drifting field is the same in every frame of a clip.
  const auto renderFrame = [&](float dt, const render::ShapeMoveSequence* playSeq =
                                             nullptr) -> std::optional<render::Image> {
    // A fixed step rather than a real clock: the shell animates, and a capture has to be
    // the same picture every time it is taken.
    (*ui)->tick(dt);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, 60.0f);
    (*ui)->endFrame();
    const render::BoardRect uiRect{request.boardRect[0], request.boardRect[1],
                                   request.boardRect[2], request.boardRect[3]};
    // Cinema is the board and the move and nothing else: the whole frame is the board
    // and the interface builds no furniture, only the frame's grade and vignette
    // (M18.3), so a clip reads as footage rather than a screenshot of the panels.
    const render::BoardRect frame =
        cinema ? render::BoardRect{0.0f, 0.0f, static_cast<float>(target->width()),
                                   static_cast<float>(target->height())}
               : uiRect;
    render::InstanceSet instances;
    // The geometry view builds the warped board as ordinary instances, so the same
    // render path draws it (M17); only the surface flag on the options selects it.
    if (shell->showsBoard()) {
      // A capture of a screen that sits over the board shows it exactly as a player
      // would see it - stepped back and out of focus - which is also what puts the blur
      // passes under the validation layers in `ctest -R gui-pause`. Cinema never blurs:
      // the board is the subject.
      // The attract sequence's board is not a "stepped back from" game: it is the menu's
      // own subject, sharp like the game screen's. Every other board-over-screen state
      // keeps the pause pull-back.
      const bool sharp = shell->screen() == app::Screen::Game ||
                         shell->screen() == app::Screen::MainMenu || cinema;
      const float away = sharp ? 0.0f : 1.0f;
      shell->session()->setPullBack(away);
      renderer->setBlur(away);
      if (frame.valid()) shell->session()->setBoardAspect(frame.width / frame.height);
      // The attract board is the menu's subject and its frame is only the left half. A
      // single capture has no later frame to ease the framing home, so state it now.
      if (shell->screen() == app::Screen::MainMenu && optionsFor(*shell).surface) {
        frameBoard(*shell);
      }
      // The interactive loop eases this; a capture states it outright, so a followed
      // move is anti-clipped in a clip too (M17.17). The M12.6 runner has already stated
      // the offsets through its simulated sequence, so it skips this and only refreshes
      // the options.
      if (optionsFor(*shell).surface) {
        if (playSeq == nullptr) {
          const render::PlaySurface alignSurf = render::PlaySurface::build(
              shell->session()->variant(),
              poseFrom(shell->settings(), shell->session()->variant()));
          const render::SlideOffset off = render::alignOffsetFor(
              *shell, render::currentFollowedCell(*shell, alignSurf));
          shell->settings().geometryAlignOffset = off.u;
          shell->settings().geometryAlignOffsetV = off.v;
        }
        // The offsets just changed, so the renderer's options must be refreshed before
        // the board is built from them.
        renderer->setOptions(optionsFor(*shell));
      }
      syncMarks(*renderer, *shell->session());
      instances = renderer->buildInstances(
          shell->session()->snapshot(), shell->session()->viewConfig(),
          &shell->session()->seams(), &shell->session()->animation(),
          [&](CellId c) { return shell->session()->boardVisible(c); },
          [&](CellId c) { return shell->session()->game().cellInPresent(c); },
          shell->session()->timelineLinks());
    }
    const view::OrbitCamera camera =
        shell->showsBoard()
            ? boardCamera(*shell,
                          playSeq != nullptr ? *playSeq : render::kNoShapeSequence)
            : view::OrbitCamera{};
    const std::function<void(VkCommandBuffer)> drawUi = [&](VkCommandBuffer cmd) {
      (*ui)->record(cmd);
    };
    // The board screen's ambient field is submitted before the board, inside its own
    // pass, so the board is drawn over it rather than under it (M18 correction). Every
    // other screen leaves the field list empty and this is a no-op.
    const std::function<void(VkCommandBuffer)> drawBackground = [&](VkCommandBuffer cmd) {
      (*ui)->recordBackground(cmd);
    };
    // The interface pass is always recorded now: in cinema it holds only the grade and
    // vignette, and outside cinema it is exactly the frame it always was.
    if (auto ok =
            renderer->render(*target, instances, camera, drawUi, drawBackground, frame);
        !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return std::nullopt;
    }
    auto pixels = target->readPixels();
    if (!pixels.has_value()) {
      std::fprintf(stderr, "%s\n", pixels.error().format().c_str());
      return std::nullopt;
    }
    render::Image img;
    img.width = target->width();
    img.height = target->height();
    img.rgba = std::move(*pixels);
    return img;
  };
  const auto writeFrame = [](const render::Image& img, const std::string& outPath) {
    if (auto ok = render::writePpm(img, outPath); !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return false;
    }
    std::printf("wrote %s\n", outPath.c_str());
    return true;
  };

  // The whole trailer as a frame sequence (M13.9): one lap, fixed step, no interface.
  if (!gTrailerClipDir.empty()) {
    std::signal(SIGPIPE, SIG_IGN);  // a dead ffmpeg is an error return, not a signal
#ifdef CB_HAVE_IMGUI
    std::filesystem::create_directories(gTrailerClipDir);
    shell->startAttract(app::defaultAttractSequence());
    if (!shell->attracting()) {
      std::fprintf(stderr, "the trailer will not start\n");
      return 1;
    }
    applyAttractLook(*shell);
    renderer->setOptions(optionsFor(*shell));
    frameBoard(*shell);
    shell->refreshTrailerView();
    // Two thrown-away frames settle the interface's own screen transition, as for
    // `--clip`.
    (void)renderFrame(1.0f / 60.0f);
    (void)renderFrame(1.0f / 60.0f);
    const float step = 1.0f / static_cast<float>(std::max(1, fps));
    // One lap at 1080p is gigabytes of PPM; pipe it into ffmpeg instead when it is there.
    FILE* videoPipe = nullptr;
    if (!gTrailerKeepFrames && std::system("ffmpeg -version >/dev/null 2>&1") == 0) {
      const std::string cmd =
          "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s " +
          std::to_string(width) + "x" + std::to_string(height) + " -framerate " +
          std::to_string(fps) +
          " -i - -c:v libx264 -crf 14 -preset medium -pix_fmt yuv420p \"" +
          gTrailerClipDir + "/trailer.mp4\"";
      videoPipe = popen(cmd.c_str(), "w");
      if (videoPipe == nullptr) {
        std::fprintf(stderr, "no ffmpeg: writing PPM frames instead\n");
      }
    }
    int written = 0;
    bool lapped = false;
    for (int n = 0; n < gTrailerMaxFrames && !lapped; ++n) {
      app::Settings& st = shell->settings();
      // The INVERT ease, exactly as the interactive loop does it.
      const float evTarget = st.geometryInvert ? 1.0f : 0.0f;
      st.geometryEvert = st.geometryEvert < evTarget
                             ? std::min(evTarget, st.geometryEvert + step * 2.0f)
                             : std::max(evTarget, st.geometryEvert - step * 2.0f);
      if (n > 0 && shell->advanceAttract(step)) {
        if (!shell->attracting() || shell->attractStep() == 0) {
          lapped = true;  // round to the opener again: one lap is done
          break;
        }
        applyAttractLook(*shell);
        renderer->setOptions(optionsFor(*shell));
        shell->refreshTrailerView();
        std::printf("cut: beat %zu (%s) at frame %d\n", shell->attractStep(),
                    shell->currentVariant().c_str(), written);
      }
      if (!shell->attracting()) break;
      syncShapeCentre(*shell);
      shell->advanceCameraTrack(step);
      easeTrailerFraming(*shell, step);
      shell->session()->advanceAnimation(step);
      renderer->setOptions(optionsFor(*shell));
      auto img = renderFrame(0.0f);
      if (!img.has_value()) return 1;
      if (videoPipe != nullptr) {
        if (std::fwrite(img->rgba.data(), 1, img->rgba.size(), videoPipe) !=
            img->rgba.size()) {
          std::fprintf(stderr, "ffmpeg closed the pipe\n");
          return 1;
        }
      } else {
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%05d.ppm", written);
        if (auto ok = render::writePpm(*img, gTrailerClipDir + name); !ok.has_value()) {
          std::fprintf(stderr, "%s\n", ok.error().format().c_str());
          return 1;
        }
      }
      if (written % 10 == 0) {  // progress for whoever launched this
        std::ofstream(gTrailerClipDir + "/progress.txt") << written;
      }
      ++written;
    }
    if (videoPipe != nullptr) (void)pclose(videoPipe);
    std::ofstream(gTrailerClipDir + "/progress.txt") << "done " << written;
    std::printf("wrote %d frames to %s (%.1f s at %d fps)\n", written,
                gTrailerClipDir.c_str(),
                static_cast<double>(written) * static_cast<double>(step), fps);
    return 0;
#else
    std::fprintf(stderr, "the trailer needs the interface build\n");
    return 1;
#endif
  }

  // M12.6: plan the whole game before rendering a frame. A move's body is its own
  // animation on a flat board, or the follow choreography's full settle on a shape; a
  // dwell is held after every move but the last. The dry pass validates the file move by
  // move and measures those durations; the session is then reset and the real pass
  // replays the same moves, one per requested frame time.
  std::vector<float> bodies;
  std::string playError;
  std::unique_ptr<app::GameRunner> runner;
  render::SurfaceCache surfaceCache;
  std::size_t applied = 0;
  if (playing) {
    runner = std::make_unique<app::GameRunner>(gameFile, *shell->session());
    if (auto ok = runner->reset(); !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return 1;
    }
    for (;;) {
      if (runner->done()) break;
      if (auto ok = runner->step(); !ok.has_value()) {
        // Stop at the offending move, keeping the legal prefix.
        playError = ok.error().format();
        break;
      }
      const float travel = runner->lastTravelSeconds();
      float body = travel;
      if (travel > 0.0f && render::shapeFollowApplies(*shell)) {
        body = render::shapeSequenceSettleSeconds(
            *shell, shell->session()->animation().path(), travel, &surfaceCache);
      }
      bodies.push_back(body);
      // One move's worth of pose samples at a time: the surface is rebuilt from a fresh
      // prefix for every move, so keeping the whole game's poses would grow without
      // bound.
      surfaceCache.clear();
    }
    if (auto ok = runner->reset(); !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return 1;
    }
  }
  const float playTotal = app::playbackTotalSeconds(bodies, dwell);

  // One frame of the played-back game at a global time. The moves up to the cursor are
  // applied in order; the shape choreography (or the flat move camera) is then stated at
  // the cursor's own elapsed time, never integrated from a real frame.
  const auto renderPlayFrame = [&](float globalSeconds) -> std::optional<render::Image> {
    const app::PlaybackCursor cursor = app::playbackCursor(bodies, dwell, globalSeconds);
    while (applied <= cursor.move && applied < bodies.size()) {
      if (auto ok = runner->step(); !ok.has_value()) break;
      ++applied;
      // A new move: drop the previous move's pose samples before simulating this one.
      surfaceCache.clear();
    }
    render::ShapeMoveSequence seq;
    const render::ShapeMoveSequence* seqArg = nullptr;
    if (shell->session() != nullptr && applied > 0 && !bodies.empty()) {
      const view::MovePath& movePath = shell->session()->animation().path();
      const float travel = shell->session()->animation().duration();
      if (travel > 0.0f && render::shapeFollowApplies(*shell)) {
        seq = render::simulateShapeSequence(*shell, movePath, travel, cursor.elapsed,
                                            &surfaceCache);
        seqArg = &seq;
      } else if (travel > 0.0f) {
        shell->session()->setMoveProgress(cursor.elapsed / travel);
      }
    }
    return renderFrame(0.0f, seqArg);
  };

  // Two thrown-away frames, in this order: the first lets a screen-change reset the pane
  // transition (which it does after the tick), the second advances that transition to
  // done - without both, the library draws at alpha zero. Then ImGui has a previous frame
  // to size from as well.
  if (!renderFrame(0.0f).has_value()) return 1;
  if (!renderFrame(1.0f).has_value()) return 1;

  // Pointer clicks, one per ';': move, press, release on three frames, so a button sees a
  // real click and the capture shows what the program does with it.
  {
    std::size_t at = 0;
    while (at < gUiClicks.size()) {
      const std::size_t end = std::min(gUiClicks.find(';', at), gUiClicks.size());
      float x = 0.0f, y = 0.0f;
      if (std::sscanf(gUiClicks.substr(at, end - at).c_str(), "%f,%f", &x, &y) == 2) {
        for (const bool down : {false, true, false}) {
          (*ui)->injectMouse(x, y, down);
          if (!renderFrame(0.0f).has_value()) return 1;
        }
      }
      at = end + 1;
    }
  }

  if (benchFrames > 0) {
    // A frame-time benchmark with no display: the same offscreen path `--shot` uses, run
    // N times. It measures the CPU recording plus the GPU wait, which is the per-frame
    // cost a player pays - a number for a regression, not an opinion (M4.8).
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < benchFrames; ++i) {
      if (!renderFrame(0.0f).has_value()) return 1;
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0)
            .count();
    const double ms = us / 1000.0 / static_cast<double>(benchFrames);
    std::printf("frame time: %.3f ms  (%.1f fps)  over %d frames\n", ms, 1000.0 / ms,
                benchFrames);
    return 0;
  }

  if (playing) {
    // A played-back game states its own global time per frame: one `--shot` at
    // `--play-at` (default: the end), or `ceil(total * fps)` frames at `i / fps`. No wall
    // clock is read anywhere in this path.
    if (!clip) {
      const float at = playAt >= 0.0f ? playAt : playTotal;
      const std::optional<render::Image> img = renderPlayFrame(at);
      if (!img.has_value() || !writeFrame(*img, path)) return 1;
    } else {
      std::error_code ec;
      std::filesystem::create_directories(path, ec);
      const int frames =
          framesGiven
              ? std::max(1, clipFrames)
              : std::max(1, static_cast<int>(std::ceil(
                                playTotal * static_cast<float>(std::max(1, fps)))));
      for (int i = 0; i < frames; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(std::max(1, fps));
        char name[32];
        std::snprintf(name, sizeof(name), "frame_%04d.ppm", i);
        const std::optional<render::Image> img = renderPlayFrame(g);
        if (!img.has_value() ||
            !writeFrame(*img, (std::filesystem::path(path) / name).string())) {
          return 1;
        }
      }
    }
    if (!playError.empty()) {
      // The whole legal prefix was still rendered; the bad move is the reason for the
      // nonzero exit, exactly as the runner's own step reported it.
      std::fprintf(stderr, "%s\n", playError.c_str());
      return 1;
    }
  } else if (!clip) {
    const std::optional<render::Image> img = renderFrame(0.0f);
    if (!img.has_value() || !writeFrame(*img, path)) return 1;
  } else {
    // A directory of numbered frames: the guaranteed output. Muxing a video from them is
    // an external step, so no codec enters the build.
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    const int frames = std::max(1, clipFrames);
    for (int i = 0; i < frames; ++i) {
      const float t = frames <= 1 ? clipT0
                                  : clipT0 + (clipT1 - clipT0) * static_cast<float>(i) /
                                                 static_cast<float>(frames - 1);
      // Pin the cycle outright: a clip is only deterministic if every frame states its
      // own `t` rather than integrating towards it. The library sweeps the overture;
      // every other screen sweeps the running move, so a board clip is a gameplay shot.
      if (screen == "newgame") {
        shell->overtures().setProgress(t);
      } else if (shell->session() != nullptr) {
        shell->session()->setMoveProgress(t);
      }
      char name[32];
      std::snprintf(name, sizeof(name), "frame_%04d.ppm", i);
      const std::optional<render::Image> img = renderFrame(0.0f);
      if (!img.has_value() ||
          !writeFrame(*img, (std::filesystem::path(path) / name).string())) {
        return 1;
      }
    }
  }
#else
  const auto instances = renderer->buildInstances(
      shell->session()->snapshot(), shell->session()->viewConfig(),
      &shell->session()->seams(), &shell->session()->animation(),
      [&](CellId c) { return shell->session()->boardVisible(c); });
  (void)renderer->render(*target, instances, shell->session()->camera());
  auto pixels = target->readPixels();
  if (!pixels.has_value()) {
    std::fprintf(stderr, "%s\n", pixels.error().format().c_str());
    return 1;
  }
  render::Image img;
  img.width = target->width();
  img.height = target->height();
  img.rgba = std::move(*pixels);
  if (auto ok = render::writePpm(img, path); !ok.has_value()) {
    std::fprintf(stderr, "%s\n", ok.error().format().c_str());
    return 1;
  }
#endif

  const std::size_t validation = ctx->validationErrorCount();
  for (const std::string& message : ctx->takeValidationMessages()) {
    std::fprintf(stderr, "%s\n", message.c_str());
  }
  std::printf("wrote %s\n", path.c_str());
  return validation == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string variantName;
  std::string shotPath;
  std::string script;
  std::string screen;
  std::string clipDir;
  std::string followMode;
  std::string shapeFollow;
  float overtureT = 0.78f;
  bool overtureTGiven = false;
  float moveT = -1.0f;
  int previewDims = 2;
  int clipFrames = 96;
  float clipT0 = 0.0f;
  float clipT1 = 1.0f;
  bool cinema = false;
  int benchFrames = 0;
  bool geometry = false;
  float evert = 0.0f;
  float formed = 1.0f;
  bool formedGiven = false;
  bool flatCapture = false;
  float slideU = 0.0f;
  float slideV = 0.0f;
  float ghost = 1.0f;
  bool pieceCylinders = false;
  std::string playFile;
  float dwell = app::kDefaultDwellSeconds;
  int captureFps = 30;
  float playAt = -1.0f;
  bool framesGiven = false;
  std::string watchName;
  int width = 1920;
  int height = 1080;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::printf(
          "endspielzero_gui [variant] [--shot FILE [--screen NAME]] [--script TEXT]\n\n"
          "  left click    select a piece, then a lit cell to move it\n"
          "  right drag    orbit     wheel  zoom\n"
          "  g             the board as its own shape; middle drag slides it round\n"
          "  Esc           pause     u  undo     r  reset\n"
          "  k             record a camera keyframe at the current move (M13.9 Part E)\n"
          "  j             remove the keyframe at the current move\n"
          "The trailer (Watch trailer):\n"
          "  left / right  previous / next keyframe      shift+left/right  one grid "
          "step\n"
          "  Enter         add a keyframe here           Del  remove the keyframe here\n"
          "  Space         pause / resume                orbit, zoom or slide: pause to "
          "edit\n"
          "  RENDER        the whole trailer to a video, frame by frame "
          "(trailer_render/)\n"
          "  --trailer-clip DIR [--user-settings] [--trailer-frames]  the same, "
          "headless\n\n"
          "With no variant, the game opens on the main menu.\n"
          "--shot renders one frame to a PPM and exits, with no display required;\n"
          "--screen picks which one: menu, welcome, newgame, pause, settings, editor,\n"
          "designer, body, flat, info, pieces, or the board by default.\n"
          "--t 0..1 is where the library screen's overture is in its cycle: 0 the flat\n"
          "board every variant starts from, 1 the shape it becomes.\n"
          "--clip DIR renders a sequence of numbered PPM frames instead of one shot,\n"
          "sweeping t from --t0 to --t1 over --frames frames (default 0..1, 96).\n"
          "--cinema drops the interface and fills the frame with the board.\n"
          "--follow off|piece|route and --move-t 0..1 show the move camera in a "
          "capture;\n"
          "--shape-follow turntable|chase picks whether the board turns under a fixed\n"
          "camera or the camera chases the piece.\n"
          "--bench-frame N times N headless frames of the chosen screen and prints "
          "ms/frame.\n"
          "--geometry draws the play board as its own shape (a cylinder, a torus, ...),\n"
          "and --evert 0..1 turns that shape through itself - a torus inside out,\n"
          "while --slide and --slide-v move the board round the shape, in cells, and\n"
          "--ghost 0.35..1 makes it translucent so the far side shows through.\n"
          "--piece-cylinders draws the shape view's pieces as icon-faced pucks (M17.25)\n"
          "instead of the sculptural meshes, so a flipped seat still reads.\n"
          "--formed 0..1 states how far that shape has formed (0 the flat board), and\n"
          "--flat draws the ordinary 2D token view instead of the shape.\n"
          "--play FILE plays a saved game (M12.7) move by move: with --clip it exports\n"
          "the whole game, with --shot it captures one instant. --dwell SECONDS is the\n"
          "pause held after each move (default 0.6), --fps N the export frame rate\n"
          "(default 30), --play-at SECONDS the instant a --shot captures (default: "
          "end).\n"
          "--watch NAME starts the bundled game NAME and captures the watching state, "
          "so\n"
          "the reviewable screens include it; --screen watch is the bundled-game "
          "picker.\n"
          "--width W --height H set the capture/window resolution (default 1920x1080).\n"
          "--ui-click \"x,y;x,y\" replays pointer clicks (pixels) before the capture.\n"
          "--piece NAME opens the designer captures on that piece.\n"
          "--dims 2..4 is how many dimensions the designer's move preview shows.\n");
      return 0;
    }
    if (arg == "--shot" && i + 1 < argc)
      shotPath = argv[++i];
    else if (arg == "--clip" && i + 1 < argc)
      clipDir = argv[++i];
    else if (arg == "--script" && i + 1 < argc)
      script = argv[++i];
    else if (arg == "--screen" && i + 1 < argc)
      screen = argv[++i];
    else if (arg == "--t" && i + 1 < argc) {
      overtureT = std::strtof(argv[++i], nullptr);
      overtureTGiven = true;
    } else if (arg == "--t0" && i + 1 < argc)
      clipT0 = std::strtof(argv[++i], nullptr);
    else if (arg == "--t1" && i + 1 < argc)
      clipT1 = std::strtof(argv[++i], nullptr);
    else if (arg == "--frames" && i + 1 < argc) {
      clipFrames = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
      framesGiven = true;
    } else if (arg == "--cinema")
      cinema = true;
    else if (arg == "--geometry")
      geometry = true;
    else if (arg == "--formed" && i + 1 < argc) {
      formed = std::strtof(argv[++i], nullptr);
      formedGiven = true;
    } else if (arg == "--flat")
      flatCapture = true;
    else if (arg == "--evert" && i + 1 < argc)
      evert = std::strtof(argv[++i], nullptr);
    else if (arg == "--slide" && i + 1 < argc)
      slideU = std::strtof(argv[++i], nullptr);
    else if (arg == "--slide-v" && i + 1 < argc)
      slideV = std::strtof(argv[++i], nullptr);
    else if (arg == "--trailer-edit")
      gTrailerEditCapture = true;
    else if (arg == "--trailer-clip" && i + 1 < argc)
      gTrailerClipDir = argv[++i];
    else if (arg == "--trailer-frames")
      gTrailerKeepFrames = true;
    else if (arg == "--user-settings")
      gUseUserSettings = true;
    else if (arg == "--ghost" && i + 1 < argc)
      ghost = std::strtof(argv[++i], nullptr);
    else if (arg == "--piece-cylinders")
      pieceCylinders = true;
    else if (arg == "--play" && i + 1 < argc)
      playFile = argv[++i];
    else if (arg == "--watch" && i + 1 < argc)
      watchName = argv[++i];
    else if (arg == "--dwell" && i + 1 < argc)
      dwell = std::strtof(argv[++i], nullptr);
    else if (arg == "--fps" && i + 1 < argc)
      captureFps = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--play-at" && i + 1 < argc)
      playAt = std::strtof(argv[++i], nullptr);
    else if (arg == "--width" && i + 1 < argc)
      width = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--height" && i + 1 < argc)
      height = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--follow" && i + 1 < argc)
      followMode = argv[++i];
    else if (arg == "--shape-follow" && i + 1 < argc)
      shapeFollow = argv[++i];
    else if (arg == "--move-t" && i + 1 < argc)
      moveT = std::strtof(argv[++i], nullptr);
    else if (arg == "--bench-frame" && i + 1 < argc)
      benchFrames = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--ui-click" && i + 1 < argc)
      gUiClicks = argv[++i];
    else if (arg == "--piece" && i + 1 < argc)
      gPieceName = argv[++i];
    else if (arg == "--dims" && i + 1 < argc)
      previewDims = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (!arg.starts_with("-"))
      variantName = arg;
  }
  // A trailer render is footage: the board and the grade, no interface.
  if (!gTrailerClipDir.empty()) cinema = true;
  const std::string& targetPath = !clipDir.empty() ? clipDir : shotPath;
  if (!targetPath.empty() || benchFrames > 0 || !gTrailerClipDir.empty()) {
    return captureFrame(
        variantName.empty() ? "standard" : variantName, targetPath, script, screen,
        overtureT, previewDims, !clipDir.empty(), clipFrames, clipT0, clipT1, cinema,
        followMode, shapeFollow, moveT, benchFrames, geometry || formedGiven, evert,
        formed, flatCapture, slideU, slideV, ghost, playFile, dwell, captureFps, playAt,
        framesGiven, width, height, watchName, pieceCylinders, overtureTGiven);
  }

  auto shell = makeShell();
  // Naming a variant on the command line starts it directly; otherwise the game opens
  // where a game should, on its menu.
  if (!variantName.empty()) {
    if (auto ok = shell->startGame(variantName); !ok.has_value()) {
      std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(),
                   ok.error().format().c_str());
      return 1;
    }
  }

  auto window = render::Window::create("EndspielZero", static_cast<std::uint32_t>(width),
                                       static_cast<std::uint32_t>(height));
  if (!window.has_value()) {
    std::fprintf(stderr, "cannot open a window: %s\n", window.error().format().c_str());
    return 1;
  }
  // Sound is an interactive-only concern (M18.5): the capture branch above returns before
  // this point, so `--shot`/`--clip` and every headless test reach the board with no
  // audio device touched. A machine with no device (CI has none) is not fatal - `create`
  // logs and returns null, `play` stays a no-op, and the game is simply silent. Created
  // after the window so SDL is already up when the audio subsystem is initialised.
  auto mixer = audio::Mixer::create();
  if (mixer) {
    mixer->setVolumes(shell->settings().volumeMaster, shell->settings().volumeEffects);
    audio::setSink(mixer.get());
  }
  auto renderer = render::BoardRenderer::create(window->context(), window->colorFormat());
  if (!renderer.has_value()) {
    std::fprintf(stderr, "cannot create the renderer: %s\n",
                 renderer.error().format().c_str());
    return 1;
  }
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(window->context(), window->handle(), renderer->theme(),
                               window->colorFormat());
  if (!ui.has_value()) {
    std::fprintf(stderr, "cannot create the interface: %s\n",
                 ui.error().format().c_str());
    return 1;
  }
  (void)(*ui)->setScale(shell->settings().guiScale);
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  {
    const view::Theme theme = view::themeFromName(shell->settings().theme);
    renderer->setTheme(theme);
    (*ui)->setTheme(theme);
  }
#endif
  (void)window->setVsync(shell->settings().vsync);
  if (shell->settings().fullscreen) SDL_SetWindowFullscreen(window->handle(), true);

  render::BoardRect boardRect{0, 0, static_cast<float>(window->width()),
                              static_cast<float>(window->height())};
  // How far the camera has stepped back off the board. Pause is not a dialog landing on
  // top of the position - it is the player looking up from it - so the view pulls away
  // rather than being covered. It lives on the session so that the board, the flat
  // board's pieces and the picking ray all see the same camera.
  float steppedBack = 0.0f;
  bool running = true;
  bool orbiting = false;
  bool panning = false;
  /// Middle-dragging the board around its own surface, in the geometry view.
  bool sliding = false;
  /// Whether the board was its own shape last frame, so the camera can be put round
  /// whatever it has just become.
  bool wasSurface = false;
  /// The camera choreography for a followed move on a shape (M17.19): hold, morph,
  /// approach, travel, morph home. Inactive on the flat board and in every capture.
  render::ShapeMoveSequence shapeSeq;
  // A trailer render running as a child process (the panel's RENDER button).
  pid_t renderPid = -1;
  std::filesystem::path renderDir;
  std::string renderStatus;
  int renderPoll = 0;
  auto lastFrame = std::chrono::steady_clock::now();
  float fps = 0.0f;

  while (running && !shell->quitRequested()) {
    SDL_Event e;
    // A board game has nothing to animate most of the time, so the loop waits for
    // input rather than spinning. While a piece is actually moving it wants frames, so
    // the wait shortens to roughly one.
    // The trailer moves the camera and the shape on every frame, with or without a piece
    // in flight; waiting for events would draw it at the idle ten frames a second.
    const bool animating = shell->hasGame() && (shell->session()->animation().active() ||
                                                shell->attracting());
    if (!SDL_WaitEventTimeout(&e, animating ? 8 : 100)) e.type = SDL_EVENT_POLL_SENTINEL;
    do {
      if (e.type == SDL_EVENT_POLL_SENTINEL) continue;
#ifdef CB_HAVE_IMGUI
      const bool consumed = (*ui)->processEvent(e);
#else
      const bool consumed = false;
#endif
      const app::Settings& settings = shell->settings();
      const bool inGame = shell->screen() == app::Screen::Game && shell->hasGame();

      switch (e.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          const auto w = static_cast<std::uint32_t>(e.window.data1);
          const auto h = static_cast<std::uint32_t>(e.window.data2);
          if (w > 0 && h > 0) {
            // The window rebuilds the swapchain and its per-image targets with it.
            (void)window->recreate(w, h);
          }
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (consumed || !inGame) break;
          if (e.button.button == SDL_BUTTON_LEFT) {
            bool handled = false;
#ifdef CB_HAVE_IMGUI
            const app::Session* session = shell->session();
            if (optionsFor(*shell).surface && session != nullptr) {
              // The geometry view: pick against the same surface the renderer drew, with
              // the effective camera, so the click resolves to the cell under the cursor
              // (M17.3). A shot in flight means the camera is about to move, so the click
              // is refused - the same rule `clickPixel` already applies on the flat board
              // (M17.16). The ray is in the board rectangle's pixel space.
              handled = true;
              if (!session->shotInFlight()) {
                const render::PlaySurface surf = render::PlaySurface::build(
                    session->variant(), poseFrom(settings, session->variant()));
                app::Action a;
                a.kind = app::ActionKind::ClickCell;
                a.cell =
                    surf.pick(session->cameraOver(surfacePlacements(surf), surf.bounds(),
                                                  view::ViewConfig{}),
                              boardRect.width, boardRect.height, e.button.x - boardRect.x,
                              e.button.y - boardRect.y, settings.geometryGhost,
                              &session->snapshot(), settings.pieceHeightScale,
                              settings.shapePieceCylinders);
                (void)shell->session()->apply(a);
              }
            }
#endif
            if (!handled) {
              // Picking uses the same rectangle the board was drawn into, or a click
              // would land on a different cell than the one under the cursor.
              shell->session()->clickPixel(e.button.x - boardRect.x,
                                           e.button.y - boardRect.y, boardRect.width,
                                           boardRect.height);
            }
          } else if (e.button.button == SDL_BUTTON_RIGHT) {
            orbiting = true;
          } else if (e.button.button == SDL_BUTTON_MIDDLE) {
            // On the shape, the middle button slides the board *along* the surface rather
            // than sliding the view about. Nothing is lost: the shape is framed and
            // centred the moment the view is switched on, so there is nothing to pan to -
            // and riding a1 round to b1, to c1, and eventually back to a1 is the one way
            // to feel a gluing rather than be told about it.
            if (inGame && optionsFor(*shell).surface) {
              sliding = true;
            } else {
              panning = true;
            }
          }
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (e.button.button == SDL_BUTTON_RIGHT) orbiting = false;
          if (e.button.button == SDL_BUTTON_MIDDLE) {
            panning = false;
            sliding = false;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION:
          // Reaching for the camera in the trailer pauses it into edit mode, so the hand
          // is not fought by keyframes or the trailer's own framing.
          if ((sliding || panning || orbiting) && shell->hasGame()) {
            shell->trailerUserTookCamera();
          }
          if (sliding && shell->hasGame()) {
            // Drag the board round its own surface: left and right along the ranks, up
            // and down along the files. On a cylinder the ranks are free, so left/right
            // does nothing and up/down carries the board (M17.8). Roughly a cell per
            // seventy pixels, and the pose is a pure function of the number, so dragging
            // back retraces it with nothing remembered between frames.
            app::Settings& st = shell->settings();
            const VariantSpec& variant = shell->session()->variant();
            const bool ring = render::PlaySurface::slidesAlongRanks(variant);
            const bool stacked = variant.dims.dims() >= 3;
            if (variant.name == "hyper4") {
              // `hyper4`'s tesseract has its own fourth-dimension pose instead of a
              // slide: the middle-drag turns it through the file/aeon plane horizontally
              // and the rank/aeon plane vertically, at the same feel as the slide. Both
              // are accumulated angles and the projection is a pure function of them, so
              // dragging back retraces it. U starts at the library's fixed spin, so an
              // undragged shape is exactly the pre-M17.12.B picture (M17.12 part B).
              st.geometryHyperSpinU += e.motion.xrel * 0.014f;
              st.geometryHyperSpinV += e.motion.yrel * 0.014f;
            } else if (ring && !variant.geom.isOrientable()) {
              // On a non-orientable board the V axis is the only slide measured to keep
              // the tile corners abutting across the seam (the U slide's gap grew to
              // about ten times an ordinary one), so both drag axes turn V - up does what
              // left-right does.
              st.geometrySlideV += (e.motion.xrel + e.motion.yrel) * 0.014f;
            } else {
              if (ring) st.geometrySlideV += e.motion.xrel * 0.014f;
              st.geometrySlideU += e.motion.yrel * 0.014f;
              // A stacked `D >= 3` shape whose ranks the 2-D test does not call a ring
              // still rotates about the same horizontal drag; `hyper4` is handled above.
              if (stacked && !ring) st.geometrySlideV += e.motion.xrel * 0.014f;
            }
          } else if (panning && shell->hasGame()) {
            // Drag the board with the middle button: slide the look-at point in the
            // camera plane, scaled so it tracks the pixels at any zoom.
            app::Action a;
            a.kind = app::ActionKind::Pan;
            const float k = shell->session()->camera().distance * 0.0016f;
            a.dx = e.motion.xrel * -k;
            a.dy = e.motion.yrel * k;
            (void)shell->session()->apply(a);
          } else if (orbiting && shell->hasGame() && !shell->session()->flatView()) {
            // Orbiting a flat board would only tilt the diagram out of true.
            app::Action a;
            a.kind = app::ActionKind::Orbit;
            a.dx = e.motion.xrel * 0.01f * settings.orbitSensitivity;
            a.dy = e.motion.yrel * -0.01f * settings.orbitSensitivity *
                   (settings.invertOrbitY ? -1.0f : 1.0f);
            (void)shell->session()->apply(a);
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          if (consumed || !inGame) break;
          shell->trailerUserTookCamera();
          app::Action a;
          a.kind = app::ActionKind::Zoom;
          const float step = 0.1f * settings.zoomSensitivity;
          a.dx = e.wheel.y > 0 ? 1.0f - step : 1.0f + step;
          (void)shell->session()->apply(a);
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          if (consumed) break;
          // In two-player mode the keyboard belongs to the players, not the shell: a key
          // a player owns is fed to their half of the board, and the single-player
          // shortcuts are off entirely so a stray key while the other player types cannot
          // undo or reset the game. Esc still pauses.
          const bool twoPlayer = inGame && shell->session()->hotSeat();
          if (twoPlayer && shell->session()->feedHotSeat(keyChar(e.key.key))) break;
          app::Action a;
          switch (e.key.key) {
            case SDLK_ESCAPE:
              // Esc is "step back one screen", which from the board means pause. While a
              // bundled game is being watched it is the way out instead: the board is
              // read-only, so there is no pause to step back to (M12.8). The attract
              // sequence is the same kind of run on the game screen, so Esc stops it and
              // leaves the ordinary menu (M13.9).
              if (shell->attracting()) {
                shell->stopAttract();
              } else if (shell->watching()) {
                shell->stopWatching();
              } else {
                shell->back();
              }
              break;
            case SDLK_Q:
              if (shell->screen() == app::Screen::MainMenu) running = false;
              break;
            case SDLK_U:
              if (inGame && !twoPlayer) {
                a.kind = app::ActionKind::Undo;
                (void)shell->session()->apply(a);
              }
              break;
            case SDLK_R:
              if (inGame && !twoPlayer) {
                a.kind = app::ActionKind::Reset;
                (void)shell->session()->apply(a);
              }
              break;
            case SDLK_F:
              // Flat view, without a trip to the settings screen.
              if (inGame && !twoPlayer) {
                app::Settings& st = shell->settings();
                st.flatView = !st.flatView;
                shell->session()->setFlatView(st.flatView);
              }
              break;
#ifdef CB_HAVE_IMGUI
            case SDLK_G:
              // Turn the board into its own shape (M17), without a trip to the rail.
              // Only where there is a surface to become.
              if (inGame && !twoPlayer &&
                  render::hasPlaySurface(shell->session()->variant())) {
                app::Settings& st = shell->settings();
                st.geometryView = !st.geometryView;
              }
              break;
            case SDLK_LEFTBRACKET:
            case SDLK_RIGHTBRACKET:
              // The turn, for a keyboard: the same target the INVERT button sets, which
              // the front end then eases the pose towards (M17.7). `]` turns it inside
              // out, `[` brings it back.
              if (inGame && !twoPlayer && optionsFor(*shell).surface) {
                shell->settings().geometryInvert = e.key.key == SDLK_RIGHTBRACKET;
              }
              break;
#endif
            // The trailer's authoring keys (M13.9): arrows step between keyframes (shift:
            // by the grid, which pauses into edit mode), Enter records a keyframe, Delete
            // removes the one under the playhead, Space pauses and resumes.
            case SDLK_LEFT:
            case SDLK_RIGHT:
              if (inGame && shell->attracting()) {
                const int dir = e.key.key == SDLK_RIGHT ? 1 : -1;
                if ((e.key.mod & SDL_KMOD_SHIFT) != 0) {
                  shell->setTrailerEditing(true);
                  (void)shell->trailerStepFraction(dir);
                } else {
                  (void)shell->requestTrailerSeekKey(dir);
                }
              }
              break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              if (inGame && shell->attracting()) {
                shell->recordCameraKeyframe(shell->trailerKeySpeed());
              }
              break;
            case SDLK_DELETE:
              if (inGame && shell->attracting()) (void)shell->eraseCameraKeyframe();
              break;
            case SDLK_SPACE:
              if (inGame && shell->attracting()) shell->toggleTrailerPause();
              break;
            case SDLK_K:
              // Record a camera keyframe (M13.9 Part E): whatever the camera and shape
              // pose look like right now, saved against the current move. The scaffold's
              // whole authoring surface is this one key plus `J` to remove one - there is
              // no in-game list or editor yet, just the saved text file.
              if (inGame && !twoPlayer)
                shell->recordCameraKeyframe(shell->trailerKeySpeed());
              break;
            case SDLK_J:
              // Remove the keyframe at the current move, if any.
              if (inGame && !twoPlayer) (void)shell->eraseCameraKeyframe();
              break;
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
    } while (SDL_PollEvent(&e));

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastFrame).count();
    lastFrame = now;
    if (dt > 0.0f) fps = fps * 0.9f + (1.0f / dt) * 0.1f;

    // The title names the game: the variant, and whose move it is.
    {
      std::string title = "EndspielZero";
      if (!shell->currentVariant().empty()) title += " - " + shell->currentVariant();
      if (shell->hasGame()) {
        title += shell->session()->game().position().sideToMove() == Color::White
                     ? " - White to move"
                     : " - Black to move";
      }
      static std::string applied;
      if (title != applied) {
        SDL_SetWindowTitle(window->handle(), title.c_str());
        applied = title;
      }
    }

    // Stepping back off the board, and coming back to it. Held on the session so the
    // board, the flat board's pieces and the picking ray cannot disagree about where
    // the camera is.
    if (shell->hasGame()) {
      // The target comes from the shell: the board recedes at pause, and recedes again
      // one step further in at a pause panel. Stepping back to the board brings it in.
      const float want = shell->boardPullBack();
      steppedBack += std::clamp(want - steppedBack, -dt * 3.4f, dt * 3.4f);
      shell->session()->setPullBack(steppedBack);
      // Out of focus by the same amount the camera has stepped back, so the two read as
      // one movement. Past the pause it is already fully soft.
      renderer->setBlur(std::min(steppedBack, 1.0f));
    }

    // Whether the flat<->shape morph is in flight this frame, and whether this is the
    // frame it settles on. The camera follow below uses both (M17.24).
    bool morphInFlight = false;
    bool morphSettling = false;
    // Ease the eversion towards the INVERT button's target, over about half a second. The
    // ramp lives here and not in the pose, so the pose stays a pure function of its
    // number and a still stays reproducible (M17.7).
    {
      app::Settings& st = shell->settings();
      const float target = st.geometryInvert ? 1.0f : 0.0f;
      const float step = dt * 2.0f;
      if (st.geometryEvert < target) {
        st.geometryEvert = std::min(target, st.geometryEvert + step);
      } else if (st.geometryEvert > target) {
        st.geometryEvert = std::max(target, st.geometryEvert - step);
      }
      // And the flat<->shape morph the same way (M18.6): SHAPE toggles `geometryView`,
      // and this eases the live `geometryFormed` toward it. The surface path stays on
      // while it is not yet flat, so toggling SHAPE off morphs back to the ordinary board
      // instead of snapping - see `optionsFor`.
      // The rate is the shared `kFormMorphRate` ramp times the player's own
      // `geometryFormSpeed` (M18.6 revision); the pace is no longer a bare constant.
      const float formTarget = st.geometryView ? 1.0f : 0.0f;
      const bool wasMorphing = st.geometryFormed != formTarget;
      // While the attract sequence runs, its own driver owns `geometryFormed` (M13.9), so
      // the SHAPE toggle's ramp stands down rather than fighting it and doubling the
      // authored pace.
      if (!shell->attracting()) {
        st.geometryFormed = render::advanceFormMorph(st.geometryFormed, formTarget, dt,
                                                     st.geometryFormSpeed);
      }
      // `wasMorphing`/`nowMorphing` bracket the actual step, whatever its size, so the
      // settling frame the camera snaps on is the real one at any speed (M17.24).
      const bool nowMorphing = st.geometryFormed != formTarget;
      morphInFlight = wasMorphing || nowMorphing;
      morphSettling = wasMorphing && !nowMorphing;
    }

    // The shape-follow choreography (M17.19): align, approach, travel, return. It drives
    // the piece's clock and the transient slide, and leaves the effective camera in
    // `shapeSeq` for `boardCamera`. On the flat board and in captures it stays inactive,
    // so the old follow behaviour is untouched.
    updateShapeSequence(*shell, shapeSeq, dt);

    // The watched game's own clock (M12.8): it plays the next move once the current one
    // has settled and its dwell has passed, and leaves for the main menu when the file
    // runs out. This is the real interactive clock, not a fixed `t` - the watching action
    // is for a person, not a reproducible capture, so nothing here is deterministic.
    // While the attract sequence runs it owns the session's watch, so the ordinary driver
    // stands down rather than advancing the same runner twice (M13.9).
    if (!shell->attracting()) shell->advanceWatching(dt);

    // The attract sequence (M13.9): started from the "Watch trailer" button, never by
    // merely sitting on the menu. It plays on the ordinary game screen like a watched
    // game, and a cut to a new shape needs the same options push and camera re-frame the
    // button's own start does, because the shell's startGame/startWatching do neither.
    // Navigating away from the game screen stops it rather than letting it play on behind
    // a menu (M13.9 revision, Part G).
#ifdef CB_HAVE_IMGUI
    if (shell->attracting()) {
      if (shell->screen() == app::Screen::Game) {
        if (shell->advanceAttract(dt)) {
          applyAttractLook(*shell);
          renderer->setOptions(optionsFor(*shell));
          // Only the first beat of a lap is framed outright; every later cut keeps the
          // camera it was carried over with, and `easeTrailerFraming` follows the shape.
          if (shell->attractStep() == 0) frameBoard(*shell);
          shell->refreshTrailerView();  // edit mode shows what the keyframes say
          wasSurface = optionsFor(*shell).surface;
        }
      } else {
        shell->stopAttract();
        renderer->setOptions(optionsFor(*shell));
      }
    }
#endif

    // The camera-keyframe track (M13.9 Part E): a recorded keyframe at the move play has
    // just reached eases the camera and shape pose toward it, for however long the
    // attract sequence or a watched/played game lasts - not just during the trailer, the
    // same way a track could be recorded over an ordinary game too. A no-op with no
    // session or no track, so this costs nothing when the scaffold is unused.
    if (shell->hasGame()) {
      syncShapeCentre(*shell);
      shell->advanceCameraTrack(dt);
    }

    // Set once the interface has built this frame: the surface view draws the board
    // itself, so the ordinary renderer stays out of the way (M17).
#ifdef CB_HAVE_IMGUI
    // A trailer render in another process: reap it when it ends, and tell the panel how
    // far along it is (a progress file the child updates every few frames).
    if (renderPid > 0) {
      int status = 0;
      if (waitpid(renderPid, &status, WNOHANG) == renderPid) {
        renderPid = -1;
        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        renderStatus = ok ? "rendered: " + renderDir.string()
                          : "render failed - see the terminal output";
      } else if (++renderPoll % 20 == 1) {
        std::ifstream in(renderDir / "progress.txt");
        int frames = 0;
        if (in >> frames) {
          renderStatus = "rendering... " + std::to_string(frames) + " frames  (" +
                         renderDir.string() + ")";
        }
      }
    }
    (*ui)->setRenderStatus(renderStatus, renderPid > 0);
    (*ui)->tick(dt);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, fps);
    (*ui)->endFrame();

    if (request.quit) running = false;
    // Start whenever the library asks, even for the variant that is already loaded:
    // leaving a game and picking the same one again means "start over", and skipping it
    // because the name matched left the button dead. A request is one-shot - the library
    // is only built on its own screen - so this cannot restart a running game by itself.
    if (!request.loadVariant.empty()) {
      (void)shell->startGame(request.loadVariant);
      renderer->setOptions(optionsFor(*shell));
      // A new variant is a new shape, and `geometryView` is sticky across the load, so
      // frame unconditionally - a one-shot "did the toggle flip" check would miss this
      // and leave the camera on the previous variant's shape, or the new one's flat board
      // (M17.14). Set `wasSurface` too so the toggle check below does not re-fire.
      frameBoard(*shell);
      wasSurface = optionsFor(*shell).surface;
    }
    if (request.saveGame) (void)shell->saveGame(request.gameName);
    if (request.loadGame) {
      if (shell->loadGame(request.gameName).has_value()) {
        // The loaded game may be a different variant, so treat it like a variant load:
        // refresh the renderer and put the camera round whatever is now drawn.
        renderer->setOptions(optionsFor(*shell));
        frameBoard(*shell);
        wasSurface = optionsFor(*shell).surface;
      }
    }
    if (!request.watchGame.empty()) {
      if (shell->startWatching(request.watchGame).has_value()) {
        // The showcase gives a glued board its shape and the follow choreography, so the
        // first-time watcher sees what the geometry view is for. A flat game is left
        // alone; nothing is forced there. This is presentation only - the game and its
        // variant are untouched (M12.8).
        const app::Session* session = shell->session();
        if (session != nullptr && render::hasPlaySurface(session->variant())) {
          shell->settings().geometryView = true;
          shell->settings().geometryFormed = 1.0f;
          shell->settings().cameraMode = "route";
          shell->session()->setCameraMode("route");
          shell->session()->setFollowStrength(1.0f);
        }
        renderer->setOptions(optionsFor(*shell));
        frameBoard(*shell);
        wasSurface = optionsFor(*shell).surface;
      }
    }
    if (request.renderTrailer && renderPid < 0) {
      // Persist what the render must see - settings, and the keyframes were saved as they
      // were recorded - then render in a child: this process keeps drawing, and the
      // render is frame-by-frame, so what this machine can draw in real time does not
      // matter to it.
      shell->applySettings();
      char stamp[32];
      const std::time_t wallClock = std::time(nullptr);
      std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&wallClock));
      renderDir = shell->trailerRenderDir() / stamp;
      std::error_code ec;
      std::filesystem::create_directories(renderDir, ec);
      const std::string dirText = renderDir.string();
      const pid_t pid = fork();
      if (pid == 0) {
        execl("/proc/self/exe", "endspielzero_gui", "--trailer-clip", dirText.c_str(),
              "--user-settings", "--fps", "30", "--width", "1920", "--height", "1080",
              static_cast<char*>(nullptr));
        _exit(127);
      }
      if (pid > 0) {
        renderPid = pid;
        renderPoll = 0;
        renderStatus = "rendering... " + dirText;
      } else {
        renderStatus = "could not start the render";
      }
    }
    if (request.startAttract) {
      // The trailer button (M13.9 revision): the sequence is fixed, so it starts in
      // place rather than through a picker. `applyAttractLook` states the shape view and
      // the no-follow camera; `frameBoard` puts the camera round the first beat's shape.
      shell->startAttract(app::defaultAttractSequence());
      applyAttractLook(*shell);
      renderer->setOptions(optionsFor(*shell));
      frameBoard(*shell);
      wasSurface = optionsFor(*shell).surface;
    }
    if (request.settingsChanged) {
      shell->applySettings();
      if (mixer) {
        mixer->setVolumes(shell->settings().volumeMaster,
                          shell->settings().volumeEffects);
      }
      renderer->setOptions(optionsFor(*shell));
      (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
      const view::Theme theme = view::themeFromName(shell->settings().theme);
      renderer->setTheme(theme);
      (*ui)->setTheme(theme);
      (void)window->setVsync(shell->settings().vsync);
    }
#ifdef CB_HAVE_IMGUI
    // The board has just become its own shape, or gone back to being a diagram. Either
    // way it is a different size in a different place, so the camera is put round it -
    // once, on the change, and never while the player is turning it.
    {
      const bool nowSurface = optionsFor(*shell).surface;
      if (nowSurface != wasSurface) {
        renderer->setOptions(optionsFor(*shell));
        frameBoard(*shell);
        wasSurface = nowSurface;
      } else if (nowSurface) {
        renderer->setOptions(optionsFor(*shell));
      }
    }
#endif
    if (request.toggleFullscreen) {
      SDL_SetWindowFullscreen(window->handle(), shell->settings().fullscreen);
    }
    if (request.boardRect[2] > 0) {
      const render::BoardRect wanted{request.boardRect[0], request.boardRect[1],
                                     request.boardRect[2], request.boardRect[3]};
      // Ease the board's rectangle towards where the screen says it belongs, so opening
      // or closing the pause menu slides the board aside instead of teleporting it.
      const float k = std::clamp(dt * 8.0f, 0.0f, 1.0f);
      const auto approach = [k](float& v, float to) {
        v += (to - v) * k;
        if (std::abs(to - v) < 0.5f) v = to;
      };
      approach(boardRect.x, wanted.x);
      approach(boardRect.y, wanted.y);
      approach(boardRect.width, wanted.width);
      approach(boardRect.height, wanted.height);
    }
#else
    const render::UiRequest request{};
#endif

    render::InstanceSet instances;
    view::OrbitCamera camera;
    if (shell->showsBoard()) {
      if (boardRect.valid()) {
        shell->session()->setBoardAspect(boardRect.width / boardRect.height);
      }
#ifdef CB_HAVE_IMGUI
      // The board's bounds are a continuous function of `geometryFormed` while the morph
      // is in flight, so the one-shot frame at the SHAPE instant is not enough: ease the
      // camera's frame toward where the shape is *now*, every frame, and snap on the
      // frame it settles so the endpoints land exactly (M17.24). This is a frame
      // correction only
      // - the player's own angle is kept.
      //
      // Not while the trailer's authored camera is in charge: there the camera is the
      // author's (keyframes, or their hand in edit mode), and the trailer's morph value
      // never equals the SHAPE toggle's target, so this would run every frame and pull
      // the zoom to the tightest framing over whatever was set up.
      if (shell->attracting()) {
        easeTrailerFraming(*shell, dt);
      } else if (morphInFlight) {
        const app::Settings& st = shell->settings();
        render::easeMorphFraming(*shell->session(), optionsFor(*shell),
                                 poseFrom(st, shell->session()->variant()),
                                 morphSettling ? 1.0f : render::morphFollowBlend(dt));
      }
#endif
      syncMarks(*renderer, *shell->session());
      // The choreography drives the animation's own progress while it is running (the
      // piece is pinned through the lead-in and the return); advancing it here too would
      // fight that. Outside the choreography it advances as it always did.
      if (!shapeSeq.active) shell->session()->advanceAnimation(dt);
      instances = renderer->buildInstances(
          shell->session()->snapshot(), shell->session()->viewConfig(),
          &shell->session()->seams(), &shell->session()->animation(),
          [&](CellId c) { return shell->session()->boardVisible(c); },
          [&](CellId c) { return shell->session()->game().cellInPresent(c); },
          shell->session()->timelineLinks());
      camera = boardCamera(*shell, shapeSeq);
    }

    const auto overlay = [&](VkCommandBuffer cmd) {
#ifdef CB_HAVE_IMGUI
      (*ui)->record(cmd);
#else
      (void)cmd;
#endif
    };
    // The board screen's field is drawn before the board, in the same pass, so the board
    // covers it (M18 correction); empty on every other screen.
    const auto background = [&](VkCommandBuffer cmd) {
#ifdef CB_HAVE_IMGUI
      (*ui)->recordBackground(cmd);
#else
      (void)cmd;
#endif
    };

    // Acquire a swapchain image and record the whole frame straight into it (ADR-0018):
    // no offscreen image, no blit. Two frames may be in flight, so the previous frame can
    // still be on the GPU while this one is recorded. A frame that comes back invalid had
    // the swapchain rebuilt under it; skipping one frame is cheap and correct.
    auto frame = window->beginFrame();
    if (!frame.has_value()) {
      std::fprintf(stderr, "begin frame failed: %s\n", frame.error().format().c_str());
      break;
    }
    if (frame->valid()) {
      if (auto ok = renderer->record(frame->commandBuffer, *frame->target, instances,
                                     camera, overlay, background, boardRect, frame->slot);
          !ok.has_value()) {
        std::fprintf(stderr, "render failed: %s\n", ok.error().format().c_str());
        break;
      }
      if (auto ok = window->submitFrame(*frame); !ok.has_value()) {
        std::fprintf(stderr, "submit failed: %s\n", ok.error().format().c_str());
        break;
      }
      if (auto ok = window->presentFrame(*frame); !ok.has_value()) {
        std::fprintf(stderr, "present failed: %s\n", ok.error().format().c_str());
        break;
      }
    }

#ifdef CB_HAVE_IMGUI
    // Rescaling rebuilds the font atlas, so it happens between frames, never inside one.
    if (request.applyScale) (void)(*ui)->setScale(shell->settings().guiScale);
#endif

    // Frame cap (M4.8): with vsync off the loop would redraw as fast as the CPU allows,
    // and every menu animates forever, so a static screen would still spin a core. Sleep
    // out the rest of the target period.
    if (const int cap = shell->settings().frameCap; cap > 0) {
      const float period = 1.0f / static_cast<float>(cap);
      const float spent =
          std::chrono::duration<float>(std::chrono::steady_clock::now() - now).count();
      if (spent < period) {
        SDL_Delay(static_cast<Uint32>((period - spent) * 1000.0f));
      }
    }
  }
  return 0;
}
