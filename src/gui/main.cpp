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

#include "app/fx_timeline.hpp"
#include "app/game_runner.hpp"
#include "app/neural_heartbeat.hpp"
#include "app/shell.hpp"
#include "audio/audio.hpp"
#include "io/game_file.hpp"
#include "io/moveset_library.hpp"
#include "io/variant_toml.hpp"
#include "render/board_fx.hpp"
#include "render/board_renderer.hpp"
#include "render/hud_layout.hpp"
#include "render/image_io.hpp"
#include "render/lab_rig.hpp"
#include "render/lab_scene.hpp"
#include "render/neural_vr.hpp"
#include "render/panel_response.hpp"
#include "render/window.hpp"
#include "view/adjacent.hpp"
#include "view/board_boot.hpp"
#include "view/game_end_fx.hpp"
#include "view/implant_fx.hpp"
#include "view/layout.hpp"
#include "view/neural_vr.hpp"
#include "view/rig_deploy.hpp"
#include "view/rig_move.hpp"
#include "view/wake.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/play_surface.hpp"
#include "render/projector_hologram.hpp"
#include "render/shape_sequence.hpp"
#include "render/slab_skin.hpp"
#include "render/ui.hpp"
#endif

using namespace cb;

namespace {

std::string gThemeOverride;
std::string gSeamHover;
bool gMatteCapture = false;
bool gDevOverlay = false;
float gRoomLightOverride = -1.0f;
float gUiScaleOverride = -1.0f;
bool gUiAuto = false;
int gHudFraming = -1;
int gLab = -1;
int gProjector = -1;
int gImplant = -1;
int gHeartbeat = -1;
/// M24.4c: run-only living-vision switches (-1 = use the setting / capture default).
int gLiving = -1;
int gTremor = -1;
int gWander = -1;
/// --daydream-t <s>: a capture states the eye after `s` idle seconds (negative: off), and
/// --daydream-wake <s> then moves the mouse and steps `s` more seconds (M24.4d).
float gDaydreamT = -1.0f, gDaydreamWake = -1.0f;
/// A click or key press since the last gaze step.
bool gGazeInput = false;
std::optional<view::Vec2f> gCliGazePin;
int gCliGazeUi = -1;
std::string gCliGazeTarget;
std::optional<float> gCliWarp;
std::optional<float> gPanelT;
/// M24.7 waking up: --wake 0|1, and --wake-t / --handshake-t pin the age of a capture.
int gWake = -1;
std::optional<float> gWakeT, gHandshakeT;
std::string gStation;
bool gLabCapture = false;
std::optional<render::LabScene> gLabScene;
bool labEnabled(const app::Shell& shell) {
  return gLabScene.has_value() &&
         (gLab >= 0 ? gLab == 1 : !gLabCapture && shell.settings().labScene);
}
bool labBackdrop(const app::Shell& shell, bool hold) {
  return labEnabled(shell) && (!gStation.empty() || (!shell.showsBoard() && !hold));
}
std::string_view stationFor(app::Screen screen) {
  switch (screen) {
    case app::Screen::MainMenu:
      return "menu";
    case app::Screen::Welcome:
      return "tutorial";
    case app::Screen::NewGame:
      return "library";
    case app::Screen::Watch:
      return "watch";
    case app::Screen::Editor:
      return "editor";
    case app::Screen::Settings:
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
      return "settings";
    case app::Screen::QuitConfirm:
      return "quit";
    default:
      return "menu";
  }
}

#ifdef CB_HAVE_IMGUI
std::string_view focusFor(app::Screen screen) {
  switch (screen) {
    case app::Screen::MainMenu:
    case app::Screen::Welcome:
    case app::Screen::Watch:
    case app::Screen::Editor:
      return "projector";
    case app::Screen::NewGame:
      return "magazine";
    case app::Screen::Settings:
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
      return "bench";
    case app::Screen::QuitConfirm:
      return "door";
    default:
      return {};
  }
}

// Apply perception's palette only when entering/leaving the lab UI. The renderer
// retains the hardware palette, including the cyan board LEDs and room salience.
bool configureImplantUi(render::Ui& ui, const app::Shell& shell, const view::Theme& theme,
                        bool& lastLabTheme, bool hold) {
  (void)hold;
  const bool labPath = labEnabled(shell);
  if (lastLabTheme != labPath) {
    ui.setTheme(labPath ? view::implantTheme(theme) : theme);
    lastLabTheme = labPath;
  }
  const bool active =
      labPath && (gImplant >= 0 ? gImplant == 1 : shell.settings().implantFx);
  ui.setImplantActive(active);
  ui.setLivingVision(active &&
                     (gLiving >= 0 ? gLiving == 1 : shell.settings().livingVision));
  ui.pinPanelAge(gPanelT);
  return active;
}

/// What the living-vision step decided for this frame (M24.4c section 4.6).
struct LivingFrame {
  bool active{false};
  float warpK{0};
  view::Vec2f tremor{};
  float dip{0};
  /// Depth of field: the focus in world units and the blend, set when the room is seen.
  float focus{1};
};
/// The warp the input remap of section 4.3 must invert, from the previous frame.
struct WarpInput {
  bool active{false};
  float k{0};
  view::Vec2f viewport{};
} gWarpInput;
/// The hologram centre in metres while one is drawn, for the gaze targets.
std::optional<std::array<float, 3>> gHoloMetres;
bool gHoloIsWall = false;

/// What the launch sequence decided for this frame (M24.7).
struct WakeFrame {
  /// The eyelids and defocus are drawn in the post composite.
  bool postActive{false};
  view::WakeState state;
  float menuFade{1};
  float handshake{-1};
};
/// The launch sequence's own clock and its skip. `launchSeconds` is the visual time of
/// the first frame on the lab path; console `wake` re-arms it.
struct WakeRun {
  double launchSeconds{-1};
  int seenWake{0}, seenHandshake{0};
  bool finished{false};
  double skippedAt{-1};
  bool active{false}, blocking{false};
} gWakeRun;
/// The projector powers on with the room: 1 outside the sequence (M24.7 step 3).
float gWakeRoomUp = 1.f;

WakeFrame stepWake(const app::Settings& settings, bool capture, double seconds,
                   float intensity, bool eligible) {
  WakeFrame out;
  gWakeRoomUp = 1.f;
  gWakeRun.active = gWakeRun.blocking = false;
  if (gWake == 0 || !eligible) return out;
  auto& run = gWakeRun;
  float age = 0;
  if (capture) {
    if (!gWakeT && !gHandshakeT) return out;
    age = gWakeT ? *gWakeT : 3.0f + *gHandshakeT;
  } else {
    if (run.launchSeconds < 0 && !run.finished)
      run.launchSeconds = settings.wakeOnLaunch || gWake == 1 ? seconds : -1;
    if (settings.wakeReplay != run.seenWake) {
      run.seenWake = settings.wakeReplay;
      run.launchSeconds = seconds;
      run.finished = false;
      run.skippedAt = -1;
    }
    if (settings.handshakeReplay != run.seenHandshake) {
      run.seenHandshake = settings.handshakeReplay;
      run.launchSeconds = seconds - 3.0;
      run.finished = false;
      run.skippedAt = -1;
    }
    if (run.finished || run.launchSeconds < 0) return out;
    if (run.skippedAt >= 0) {
      // Skipped (or the cross-fade of a calm run): the plain 0.2 s cross-fade of
      // brief 3.6.
      const float t = static_cast<float>((seconds - run.skippedAt) / .2);
      out.menuFade = std::clamp(t, 0.f, 1.f);
      out.menuFade = out.menuFade * out.menuFade * (3.f - 2.f * out.menuFade);
      if (t >= 1.f) run.finished = true;
      return out;
    }
    age = static_cast<float>(seconds - run.launchSeconds);
  }
  out.state = view::wakeState(age, intensity);
  if (out.state.done) {
    run.finished = !capture;
    return out;
  }
  out.postActive = true;
  out.menuFade = out.state.menuFade;
  out.handshake = out.state.handshake;
  gWakeRoomUp = out.state.roomUp;
  run.active = !capture;
  run.blocking = run.active && !(out.state.handshake >= 1.f);
  return out;
}

void applyWake(render::BoardRenderer& renderer, const WakeFrame& wake,
               std::uint32_t height) {
  if (!wake.postActive) {
    renderer.setWake(std::nullopt);
    return;
  }
  const auto& w = wake.state;
  renderer.setWake(render::PostWake{w.topEdge, w.bottomEdge, w.curve, w.feather,
                                    w.blurFraction * static_cast<float>(height),
                                    w.exposureEv, w.saturation, w.lidGlow});
}

void setImplantFrame(render::BoardRenderer& renderer, const render::Ui& ui,
                     const app::Shell& shell, bool active, double seconds,
                     std::uint32_t height, bool capture, const LivingFrame& living = {},
                     const WakeFrame& wake = {}) {
  const auto& settings = shell.settings();
  renderer.setDepthOfField(std::nullopt);
  if (!active) {
    renderer.setImplant(std::nullopt);
    return;
  }
  render::ImplantParams p;
  p.time = static_cast<float>(seconds);
  p.intensity = renderer.options().presentationIntensity;
  const bool pulse = gHeartbeat >= 0 ? gHeartbeat == 1 : !capture && settings.heartbeat;
  static app::NeuralHeartbeat heartbeat;
  // Single captures integrate from zero through explicit time. Clips and live frames
  // keep one phase while screens and rates change.
  if (capture) {
    static app::NeuralHeartbeat sampled;
    static std::optional<double> sampledAt;
    const auto* session = shell.screen() == app::Screen::Game ? shell.session() : nullptr;
    if (!sampledAt || seconds < *sampledAt) {
      sampled = {};
      sampled.observe(session, 0);
    }
    sampled.observe(session, seconds);
    sampledAt = seconds;
    p.heartbeat = pulse ? view::heartbeat(sampled.phase().cycles, p.intensity) : 1.f;
  } else {
    heartbeat.observe(shell.screen() == app::Screen::Game && !shell.attracting()
                          ? shell.session()
                          : nullptr,
                      seconds);
    p.heartbeat = pulse ? view::heartbeat(heartbeat.phase().cycles, p.intensity) : 1.f;
  }
  p.dip = std::max(ui.saccadicDip(), living.dip);
  p.pixelScale = static_cast<float>(height) / 1080.f;
  p.handshake = wake.handshake;
  p.menuFade = wake.menuFade;
  if (wake.handshake >= 0) {
    // The veil rises with the menu's convergence (brief 3.6: resolve 0.6-1.8 s).
    const float t = std::clamp((wake.handshake - .6f) / 1.2f, 0.f, 1.f);
    p.globalSync = t * t * (3.f - 2.f * t);
  }
  if (living.active) {
    p.living = 1;
    p.warpK = living.warpK;
    p.tremor[0] = living.tremor[0] * p.pixelScale;
    p.tremor[1] = living.tremor[1] * p.pixelScale;
    p.gaze[0] = settings.gazeState.pointPx[0];
    p.gaze[1] = settings.gazeState.pointPx[1];
    p.uiFocus = settings.gazeState.uiFocus;
    // The room blurs at every depth but the one the eye is on (section 4.4); where depth
    // cannot be read, the uniform defocus follows how much the eye is on the menu.
    const auto& gaze = settings.gazeState;
    render::BoardRenderer::DepthOfField dof{
        living.focus, view::cocScaleFor(render::kCellsPerMetre),
        view::kWorldDefocusPixels * p.pixelScale,
        std::max(.35f * p.uiFocus, .5f * gaze.daydream)};
    // Daydream (M24.4d): the whole viewport softens; a small spot at the gaze stays
    // sharper.
    p.daydream = gaze.daydream;
    dof.floorPx = gaze.daydream * view::kDaydreamBlurPixels * p.pixelScale;
    dof.spot[0] = gaze.pointPx[0];
    dof.spot[1] = gaze.pointPx[1];
    dof.spotRadius = view::kSpotRadius * static_cast<float>(height);
    dof.spotFeather = view::kSpotFeather * static_cast<float>(height);
    dof.spotFraction = view::kSpotBlurFraction;
    renderer.setDepthOfField(dof);
  }
  for (const auto& record : ui.implantRecords()) {
    if (p.panelCount == p.panels.size()) break;
    auto state = record.leaving ? view::implantLeaving(record.age, p.intensity)
                                : view::implantForming(record.age, p.intensity);
    if (record.leaving && !state.visible()) continue;
    if (record.settled) state = {1, 0, 0};
    auto& panel = p.panels[p.panelCount++];
    panel = {{record.rect[0], record.rect[1], record.rect[2], record.rect[3]},
             state.form,
             state.dissolve,
             state.afterimage,
             static_cast<float>(record.seed % 997)};
    panel.age = record.settled ? -1.f : record.age;
    panel.settled = record.settled ? 1.f : 0.f;
  }
  for (const auto& rect : ui.toolRects()) {
    if (p.toolCount == p.toolRects.size()) break;
    p.toolRects[p.toolCount++] = rect;
  }
  const auto warm = renderer.theme().implantWarm.array();
  const auto body = renderer.theme().implant.array();
  std::copy(warm.begin(), warm.end(), p.warm);
  std::copy(body.begin(), body.end(), p.body);
  renderer.setImplant(p);
}

std::array<float, 2> appendProjector(render::Ui& ui, const app::Settings& settings,
                                     render::InstanceSet& instances,
                                     const view::Theme& theme) {
  gHoloMetres.reset();
  if (!(gProjector >= 0 ? gProjector == 1 : settings.projector)) return {};
  auto req = ui.hologramRequest();
  // Waking up: the projector powers on with the room, growing out of its lens.
  req.alpha *= gWakeRoomUp;
  req.scale *= gWakeRoomUp;
  const bool wall = req.projector == render::HologramRequest::Projector::Wall;
  auto lens = *gLabScene->doc().anchor(wall ? "wallprojector.lens" : "projector.lens");
  const auto source = gLabScene->toWorld(lens);
  if (wall)
    lens = render::wallHologramCentre(lens, req.radiusMetres);
  else
    lens[2] += .32f - .20f + req.radiusMetres;
  const render::HologramPlacement at{gLabScene->toWorld(lens),
                                     req.radiusMetres * render::kCellsPerMetre,
                                     req.yaw,
                                     req.alpha,
                                     req.scale,
                                     req.squashZ,
                                     source,
                                     wall};
  render::Hologram h;
  switch (req.kind) {
    case render::HologramRequest::Manifold:
      h = render::manifoldHologram(req.seconds, theme, at);
      break;
    case render::HologramRequest::Tesseract:
      h = render::tesseractHologram(req.seconds, theme, at);
      break;
    case render::HologramRequest::Overture:
      if (const auto* scene = ui.overtureScene())
        h = render::overtureHologram(*scene, theme, at);
      break;
    case render::HologramRequest::Editor: {
      h = render::overtureHologram(ui.editorPreviewScene(), theme, at);
      const auto camera = ui.labCamera().value_or(*gLabScene->station("editor"));
      const auto& io = ImGui::GetIO();
      std::vector<render::EditorPickQuad> picks;
      const auto eye = camera.eye(), forward = view::normalize(camera.target - eye);
      for (const auto& q : h.quads) {
        std::array<ImVec2, 4> corners;
        const view::Vec3 v{q.quat[0], q.quat[1], q.quat[2]};
        const auto rotate = [&](view::Vec3 p) {
          return p + view::cross(v, p) * (2 * q.quat[3]) +
                 view::cross(v, view::cross(v, p)) * 2;
        };
        for (int j = 0; j < 4; ++j) {
          const auto p = view::Vec3{q.center[0], q.center[1], q.center[2]} +
                         rotate({(j == 0 || j == 3 ? -.5f : .5f) * q.scale[0],
                                 (j < 2 ? -.5f : .5f) * q.scale[1], 0});
          const auto pixel = camera.project(p, io.DisplaySize.x / io.DisplaySize.y,
                                            io.DisplaySize.x, io.DisplaySize.y);
          corners[static_cast<std::size_t>(j)] = {pixel.x, pixel.y};
        }
        const float depth =
            view::dot(view::Vec3{q.center[0], q.center[1], q.center[2]} - eye, forward);
        picks.push_back(
            {corners, static_cast<CellId>(picks.size()), depth, depth > camera.nearZ});
      }
      ui.setEditorPickQuads(std::move(picks));
      break;
    }
    case render::HologramRequest::None:
      break;
  }
  // The lens flare of the power-on belongs to the beam, so it draws even before any of
  // the hologram has grown.
  if (const auto flare = render::projectorFlare(
          source, at.centre, .05f * render::kCellsPerMetre, gWakeRoomUp, theme))
    instances.holoBeam.push_back(*flare);
  if (!h.any()) return {};
  if (req.alpha > .05f) {
    gHoloMetres = lens;
    gHoloIsWall = wall;
  }
  instances.holo = std::move(h.quads);
  instances.holo.insert(instances.holo.end(), h.lines.begin(), h.lines.end());
  instances.holoTokens = std::move(h.tokens);
  instances.holoTokenBatches = h.tokenBatches;
  instances.holoBeam.push_back(render::projectorBeam(source, at, theme));
  return wall ? std::array{0.f, req.alpha} : std::array{req.alpha, 0.f};
}

/// One step of the eye (M24.4c section 4.6, M24.4d): choose the gaze, ease the focus,
/// daydream when idle. The camera is never touched. Called once per frame after the lab
/// camera is known and before the implant parameters are handed to the renderer.
LivingFrame stepLiving(render::Ui& ui, app::Shell& shell, view::OrbitCamera& camera,
                       bool capture, double seconds, float dt, std::uint32_t width,
                       std::uint32_t height, float intensity) {
  gWarpInput = {};
  LivingFrame out;
  auto& settings = shell.settings();
  if (!ui.livingVision()) return out;
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    if (gCliGazePin) settings.gazePin = gCliGazePin;
    if (gCliGazeUi >= 0) settings.gazeUi = gCliGazeUi;
    if (!gCliGazeTarget.empty()) settings.gazeTarget = gCliGazeTarget;
    if (gCliWarp) settings.warpOverride = gCliWarp;
  }
  out.active = true;
  out.warpK = settings.warpOverride.value_or(view::kLensWarpK);
  const view::Vec2f viewport{static_cast<float>(width), static_cast<float>(height)};
  const float aspect = viewport[0] / std::max(1.f, viewport[1]);
  const auto centre = ui.implantPanelCentre();

  // A clip renders its frames with no interface clock; its visual time is the clock then.
  static double lastSeconds = -1;
  if (capture && dt <= 0)
    dt = lastSeconds >= 0
             ? static_cast<float>(std::clamp(seconds - lastSeconds, 0.0, 0.1))
             : 0.f;
  lastSeconds = seconds;
  const auto eye = camera.eye();
  view::Vec3 forward, right, up;
  camera.basis(forward, right, up);
  const auto viewProj = camera.viewProj(aspect);
  const auto project = [&](view::Vec3 p) -> std::optional<std::pair<view::Vec2f, float>> {
    const view::Vec3 d{p.x - eye.x, p.y - eye.y, p.z - eye.z};
    const float depth = d.x * forward.x + d.y * forward.y + d.z * forward.z;
    if (depth <= camera.nearZ) return std::nullopt;
    const auto ndc = view::transformPoint(viewProj, p);
    return std::pair{
        view::Vec2f{(ndc.x * .5f + .5f) * viewport[0], (ndc.y * .5f + .5f) * viewport[1]},
        std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z)};
  };

  std::vector<view::GazeTarget> targets;
  if (gLabScene) {
    for (const auto& g : gLabScene->doc().gaze) {
      view::GazeTarget t{g.key, {}, g.weight};
      if (g.key == "hologram" || g.key == "wallholo") {
        const bool wall = g.key == "wallholo";
        if (gHoloMetres && gHoloIsWall == wall)
          t.world = gLabScene->toWorld(*gHoloMetres);
        else
          t.weight = 0;
      } else if (g.key != "menu") {
        t.world = gLabScene->toWorld(g.at);
      }
      targets.push_back(t);
    }
  }
  float defaultDistance = camera.distance;
  if (gHoloMetres)
    if (const auto p = project(gLabScene->toWorld(*gHoloMetres)))
      defaultDistance = p->second;

  float mx = viewport[0] * .5f, my = viewport[1] * .5f;
  bool inWindow = false;
  Uint32 buttons = 0;
  if (!capture) {
    float x = 0, y = 0;
    buttons = SDL_GetMouseState(&x, &y);
    const auto scale = ImGui::GetIO().DisplayFramebufferScale;
    mx = x * scale.x;
    my = y * scale.y;
    inWindow = SDL_GetMouseFocus() != nullptr;
  }
  static view::Vec2f lastMouse{};
  view::GazeInput in;
  in.dt = dt;
  in.time = seconds;
  in.mousePx = capture ? centre : view::Vec2f{mx, my};
  // A capture has no pointer: it counts as moving, unless wandering is asked for.
  in.mouseMoved = capture ? !(gWander == 1) : lastMouse != in.mousePx;
  lastMouse = in.mousePx;
  in.mouseOverPanel = capture || ui.mouseOverImplantPanel();
  in.dragging = buttons != 0;
  in.active = !ui.moveRunning();
  in.wander = gWander >= 0 ? gWander == 1 : !capture && settings.gazeWander;
  in.intensity = intensity;
  in.viewport = viewport;
  in.panelCentrePx = centre;
  in.targets = targets;
  in.project = project;
  in.defaultDistance = defaultDistance;
  in.fovY = camera.fovY;
  in.seed = 0xC0FFEEu;
  (void)inWindow;
  auto& gaze = settings.gazeState;
  in.input = gGazeInput;
  gGazeInput = false;
  if (capture && gDaydreamT >= 0) {
    // Replay the idle seconds from a fresh eye, so the capture is the same however many
    // warm-up frames ran. Then optionally wake it with a mouse move.
    gaze = {};
    in.dt = 1.f / 60;
    in.mouseMoved = false;
    in.wander = true;
    in.mouseOverPanel = false;
    const int steps = static_cast<int>(std::lround(gDaydreamT * 60));
    for (int i = 0; i < steps; ++i) {
      in.time = i / 60.0;
      gaze = view::stepGaze(gaze, in);
    }
    if (gDaydreamWake >= 0) {
      in.mouseMoved = true;
      in.mousePx = {viewport[0] * .3f, viewport[1] * .4f};
      const int wake = static_cast<int>(std::lround(gDaydreamWake * 60));
      for (int i = 0; i < wake; ++i) {
        in.time = (steps + i) / 60.0;
        gaze = view::stepGaze(gaze, in);
      }
    }
    in.dt = 0;
  } else {
    gaze = view::stepGaze(gaze, in);
  }
  // A capture states the settled gaze outright, so the frame does not depend on how many
  // warm-up frames preceded it (a finished move equals a cut).
  if (capture && !in.wander && gaze.saccadeT < 1) {
    gaze.pointPx = gaze.toPx;
    gaze.saccadeT = 1;
  }

  // A pinned fixation is stated outright, with no eased accommodation, and holds even
  // when the target is off screen (the eye still focuses on it; the point is clamped).
  const auto pinTarget = [&](std::string_view key) {
    const auto found = std::find_if(targets.begin(), targets.end(),
                                    [&](const auto& t) { return t.key == key; });
    if (found == targets.end()) return;
    const bool menu = key == "menu";
    const auto p =
        menu ? std::optional{std::pair{centre, defaultDistance}} : project(found->world);
    if (!p) return;
    gaze.pointPx = view::Vec2f{std::clamp(p->first[0], 0.f, viewport[0]),
                               std::clamp(p->first[1], 0.f, viewport[1])};
    gaze.fromPx = gaze.toPx = gaze.pointPx;
    gaze.saccadeT = 1;
    gaze.uiFocus = gaze.uiFocusTarget = menu ? 1.f : 0.f;
    gaze.focusDistance = gaze.focusDistanceTarget = p->second;
  };
  if (!settings.gazeTarget.empty()) {
    pinTarget(settings.gazeTarget);
  } else if (settings.gazePin) {
    gaze.pointPx = {(*settings.gazePin)[0] * viewport[0],
                    (*settings.gazePin)[1] * viewport[1]};
    gaze.fromPx = gaze.toPx = gaze.pointPx;
    gaze.saccadeT = 1;
  }
  if (settings.gazeUi >= 0) {
    gaze.uiFocus = gaze.uiFocusTarget = static_cast<float>(settings.gazeUi);
  }

  const bool tremorOn = gTremor >= 0 ? gTremor == 1 : !capture && settings.tremor;
  out.tremor =
      tremorOn ? view::tremorOffset(seconds, intensity, 0xA11CEu) : view::Vec2f{};
  out.dip = 0;  // the eye-shift term was the gaze's only input; the camera's own dip is
                // the UI's

  // The room's focus, in dioptres: between the gazed depth and the menu's depth.
  const float menuFocus = view::kMenuFocusDistanceMetres * render::kCellsPerMetre;
  const float inv =
      (1 - gaze.uiFocus) / std::max(gaze.focusDistance, .001f) + gaze.uiFocus / menuFocus;
  out.focus = 1.f / std::max(inv, 1e-6f);

  gWarpInput = {true, out.warpK, viewport};
  return out;
}

/// --deploy-t / --stow-t: a lab game's deploy or stow pinned at that age; --focus pins
/// the room-light focus level (M24.5). Negative means off.
float gDeployT = -1.0f;
float gStowT = -1.0f;
float gFocus = -1.0f;
/// --skin 0|1: the lab game's slab skin (M24.5b; default on, the console's `skin`
/// switches it live). --latch-t pins a seam latch's age on --latch-axis for captures.
int gSkin = -1;
bool gSkinOn = true;
float gLatchT = -1.0f;
int gLatchAxis = 1;
/// --adjacent, --adjacent-rings N, --adjacent-amount A: the tiled view for a capture
/// (M22.2); the console's `adjacent` switches it live. Negative means not given.
bool gAdjacent = false;
int gAdjacentRings = -1;
float gAdjacentAmount = -1.0f;

/// A game happens in the lab (M24.5): the lab is on, a game is drawn (or held while it
/// stows) and nothing else owns the camera. Physical or in the implant's space (M25.5).
bool labGameAny(const app::Shell& shell, bool hold) {
  return labEnabled(shell) && gStation.empty() && shell.hasGame() &&
         !shell.attracting() && gLabScene->doc().deploy.has_value() &&
         (shell.showsBoard() || hold) && shell.screen() != app::Screen::Editor;
}
/// M25.5: the lab game is drawn by the implant this frame - its space, or its 2-D
/// picture - rather than on the rig.
bool vrDrawn(const app::Shell& shell);
bool vrFor(const app::Shell& shell, const render::Ui* ui);
/// A lab game on the rig: the hardware carries its slabs (M24.5).
bool labGame(const app::Shell& shell, bool hold) {
  return labGameAny(shell, hold) && !vrDrawn(shell);
}

/// --relayout-t <s>: a lab game's layout change (the capture's script made it) pinned at
/// that age (M24.6). Negative: a capture snaps to the new layout.
float gRelayoutT = -1.0f;
/// True inside `captureFrame`: a capture states its relayout age or snaps.
bool gCapturing = false;
/// This frame's explicit presentation seconds, for the rig's moves and cable pulses.
double gRigNow = 0.0;

/// The lab game's layout, followed from frame to frame (M24.6): a change of view moves
/// the hardware rather than snapping. Pure in explicit time: a move's age is `gRigNow`
/// minus its start, or `--relayout-t`.
struct RigTrack {
  std::uint64_t session{0};
  std::vector<view::Placement> placements;  ///< the layout the rig stands in
  std::optional<render::RigLayout> layout;
  std::optional<render::Relayout> move;  ///< the last layout change
  std::vector<view::Placement> moveTo;   ///< the placements it lands in
  double start{-1e9};
  std::optional<view::OrbitCamera> fromCamera;  ///< the camera when it began
  std::optional<view::OrbitCamera> lastCamera;  ///< the last frame's lab game camera
  float lastFormed{-1.0f};
  double flatSince{-1e9};  ///< a stacked shape view last returned to flat
};
RigTrack gRigTrack;

render::BoardOptions optionsFor(const app::Shell& shell, int seamHighlight);

/// The placements the rig stands in: the session's, moved under the shape while the
/// geometry view draws a glued 2-D board as its surface (M24.9), so the shape sits on its
/// slab. Cached: the shape's centre is a whole surface build.
const std::vector<view::Placement>& rigPlacements(const app::Shell& shell) {
  const app::Session& session = *shell.session();
  if (!optionsFor(shell, -1).surface) return session.placements();
  static const VariantSpec* variant = nullptr;
  static std::vector<view::Placement> from, moved;
  if (variant != &session.variant() || from.size() != session.placements().size() ||
      !std::equal(from.begin(), from.end(), session.placements().begin(),
                  [](const auto& a, const auto& b) {
                    return a.cell == b.cell && a.x == b.x && a.y == b.y && a.z == b.z &&
                           a.slice == b.slice;
                  })) {
    variant = &session.variant();
    from = session.placements();
    moved = render::shapeRigPlacements(session.variant(), from);
  }
  return moved;
}

/// Remember the layout before a capture's script runs, so the change it makes is a move.
void primeRigTrack(const app::Shell& shell) {
  gRigTrack = RigTrack{};
  gRigTrack.session = shell.session()->id();
  gRigTrack.placements = rigPlacements(shell);
}

/// The rig around the current layout (M24.6: it follows layout changes, `trackLayout`).
const render::RigLayout* rigLayoutFor(const app::Shell& shell) {
  const app::Session& session = *shell.session();
  auto& t = gRigTrack;
  if (t.session != session.id()) {
    t = RigTrack{};
    t.session = session.id();
    t.placements = rigPlacements(shell);
  }
  if (!t.layout && !t.placements.empty())
    t.layout = render::rigLayout(*gLabScene, t.placements);
  return t.layout ? &*t.layout : nullptr;
}

bool samePlacements(const std::vector<view::Placement>& a,
                    const std::vector<view::Placement>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].cell != b[i].cell || a[i].x != b[i].x || a[i].y != b[i].y ||
        a[i].z != b[i].z || a[i].slice != b[i].slice)
      return false;
  return true;
}

/// `b` is `a` moved as a whole in x and y: the shape view switching on or off (M24.9).
bool shiftedPlacements(const std::vector<view::Placement>& a,
                       const std::vector<view::Placement>& b) {
  if (a.size() != b.size() || a.empty()) return false;
  const float dx = b[0].x - a[0].x, dy = b[0].y - a[0].y;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].cell != b[i].cell || a[i].slice != b[i].slice || a[i].z != b[i].z ||
        std::abs(b[i].x - a[i].x - dx) > 1e-4f || std::abs(b[i].y - a[i].y - dy) > 1e-4f)
      return false;
  return true;
}

/// Follow the session's layout: a change becomes a relayout move when `animate` (no
/// deploy or stow is running), else the rig snaps to it. The shape view switching on or
/// off moves the whole lab with the board (the camera reframes by the same amount), so
/// nothing on screen jumps (M24.9).
void trackLayout(const app::Shell& shell, bool animate) {
  const auto* current = rigLayoutFor(shell);
  auto& t = gRigTrack;
  const auto& next = rigPlacements(shell);
  if (samePlacements(t.placements, next)) return;
  if (shiftedPlacements(t.placements, next)) {
    t.move.reset();
    t.placements = next;
    t.layout = render::rigLayout(*gLabScene, next);
    return;
  }
  auto layout = current ? render::rigLayoutAfter(*gLabScene, *current, next)
                        : render::rigLayout(*gLabScene, next);
  if (animate && current && layout) {
    t.move = render::relayoutOf(*gLabScene, *current, t.placements, *layout, next);
    // M25.3: a formed shape unforms to flat before any slab moves.
    if (shell.settings().geometryFormed > 1e-4f) t.move->lead = view::kRigUnformSeconds;
    t.moveTo = next;
    t.start = gRigNow;
    t.fromCamera = t.lastCamera;
  } else {
    t.move.reset();
  }
  t.placements = next;
  t.layout = std::move(layout);
}

/// How far into the last relayout this frame is.
float relayoutAge() {
  return gRelayoutT >= 0.0f ? gRelayoutT : static_cast<float>(gRigNow - gRigTrack.start);
}

/// A relayout still moving the slabs: input waits for it (skip ends it).
bool relayoutRunning(float intensity) {
  const auto& t = gRigTrack;
  return t.move && intensity >= .5f && relayoutAge() < t.move->lead + t.move->seconds;
}

/// Slabs on their own boot clock (M24.6): a relayout's arrivals boot once they lock;
/// slabs lowered back from a stacked shape view take the short calm boot.
void applySlabBoot(render::BoardFxState& fx, const app::Shell& shell, float intensity) {
  if (!labGame(shell, false) || shell.session() == nullptr) return;
  const auto& t = gRigTrack;
  const std::size_t cells = shell.session()->snapshot().cellCount();
  if (t.move && !t.move->arrivals.empty() && t.session == shell.session()->id()) {
    if (const auto age = render::relayoutBootAge(*t.move, relayoutAge(), intensity)) {
      fx.cellBootAge.assign(cells, -1.0f);
      for (CellId c = 0; c < cells && c < t.move->to.cellSlab.size(); ++c)
        for (const int a : t.move->arrivals)
          if (t.move->to.cellSlab[c] == static_cast<std::uint32_t>(a))
            fx.cellBootAge[c] = *age;
    }
  }
  const float flat = static_cast<float>(gRigNow - t.flatSince);
  if (flat >= 0.0f && flat < render::kShortBootSeconds) {
    fx.cellBootAge.assign(cells, flat);
    fx.shortBoot = true;
  }
}

/// The stacked D >= 3 shape view on the lab game path (M24.6): the shape is light and
/// the slabs park high.
bool stackedShapeView(const app::Shell& shell) {
  const app::Session* session = shell.session();
  if (session == nullptr || session->variant().dims.dims() < 3) return false;
  const app::Settings& s = shell.settings();
  return (s.geometryView || s.geometryFormed > 1e-4f) && !s.flatView &&
         render::hasPlaySurface(session->variant());
}

/// The stow's board shutdown in seconds (0 for a finished game), apart from the lead
/// the stow waits before its slabs leave, which also unforms a formed shape (M25.3).
float gStowFxShutdown = app::kStowShutdownSeconds;

/// Before the interface builds: on the lab path, menu to game becomes Deploy and game to
/// menu Stow, timed by this game's mast groups. A finished game has already shut down.
/// M25.3: a stow from the shape view waits for the shape to unform before any slab
/// leaves.
void configureRigMoves(render::Ui& ui, const app::Shell& shell) {
  const bool on = labEnabled(shell) && gStation.empty() && shell.hasGame() &&
                  !shell.attracting() && gLabScene->doc().deploy.has_value();
  // M25.5: a VR game deploys and stows one stand-in slab, and the handoff runs inside the
  // moves: the deploy's "lock" is the horizon open (the boards boot then), the stow's
  // lead is the eyes opening on the room and the code fading. No rig is built for the
  // game.
  if (on && vrFor(shell, &ui)) {
    gStowFxShutdown = 0.0f;
    ui.setRigMoves(
        true, view::kVrEnterSeconds, view::kVrLeaveSeconds, view::stowTravelSeconds(1),
        render::Ui::CalmRigTiming{view::kVrEnterCalmSeconds, view::kVrLeaveCalmSeconds});
    return;
  }
  const render::RigLayout* layout = on ? rigLayoutFor(shell) : nullptr;
  const int groups = layout ? layout->slots : 1;
  gStowFxShutdown =
      on && shell.session()->endEvent().has_value() ? 0.0f : app::kStowShutdownSeconds;
  const bool formed =
      on && optionsFor(shell, -1).surface && shell.settings().geometryFormed > 1e-4f;
  ui.setRigMoves(layout != nullptr, view::deployLastLock(groups),
                 view::stowLead(gStowFxShutdown, formed),
                 view::stowTravelSeconds(groups));
}

/// This frame's rig: the layout, the running move and every slab's pose.
struct RigFrame {
  const render::RigLayout* layout{nullptr};
  std::optional<render::Ui::RigMove> move;  ///< a deploy or stow still running
  std::vector<view::SlabPose> poses;
  /// A layout change still moving the hardware (M24.6); `layout` points into it.
  std::shared_ptr<const render::RelayoutFrame> relayout;
  float relayoutAge{0};
  float parked{0};  ///< how far the slabs have risen for a stacked shape view
  [[nodiscard]] bool is(app::ScreenMove m) const { return move && move->move == m; }
};

/// Also clamps the player's stored orbit inside the room, before the game camera is
/// derived from it, so zoom can never drift outside.
RigFrame rigFrame(app::Shell& shell, const render::Ui& ui, bool hold, float intensity) {
  RigFrame f;
  if (!labGame(shell, hold)) return f;
  if (const auto m = ui.rigMove(); m && !m->done) f.move = m;
  trackLayout(shell, !f.move && (gCapturing ? gRelayoutT >= 0.0f : true));
  f.layout = rigLayoutFor(shell);
  if (f.layout == nullptr) return f;
  render::constrainGameCamera(*shell.session(), *f.layout);
  // A layout change in flight moves the slabs (M24.6), after its lead (M25.3).
  if (!f.move && gRigTrack.move) {
    f.relayoutAge = relayoutAge();
    if (f.relayoutAge < gRigTrack.move->lead + gRigTrack.move->seconds) {
      auto frame = std::make_shared<render::RelayoutFrame>(render::relayoutFrame(
          *gLabScene, *gRigTrack.move, gRigTrack.moveTo, f.relayoutAge, intensity));
      if (!frame->done) {
        f.layout = &frame->layout;
        f.poses = frame->poses;
        f.relayout = std::move(frame);
        return f;
      }
    }
  }
  auto phase = render::RigPhase::Rest;
  float age = 0;
  if (f.is(app::ScreenMove::Deploy)) {
    phase = render::RigPhase::Deploy;
    age = f.move->age;
  } else if (f.is(app::ScreenMove::Stow)) {
    phase = render::RigPhase::Stow;
    age = f.move->age - f.move->shutdown;
  }
  f.poses = render::rigPoses(*f.layout, phase, age, intensity);
  // A stacked shape view: the slabs rise to the gantry's top as the shape forms.
  if (!f.move && stackedShapeView(shell)) {
    f.parked = render::parkFraction(shell.settings().geometryFormed);
    render::parkHigh(*f.layout, f.poses, f.parked);
  }
  return f;
}

/// The board's boot waits for the deploy's last lock; a capture states that age.
std::optional<float> deployBootAge(float deployAge, float lock) {
  if (deployAge < lock) return 0.0f;
  if (deployAge - lock >= view::kBootSeconds) return std::nullopt;
  return deployAge - lock;
}

/// The stow's compressed shutdown, 0..1; a finished game is already dark (no hold fx).
/// It runs over the board's own shutdown, not the stow's whole lead (M25.3).
std::optional<float> stowShutdown(const RigFrame& rig) {
  if (!rig.is(app::ScreenMove::Stow) || rig.move->shutdown <= 0 || gStowFxShutdown <= 0)
    return std::nullopt;
  return std::clamp(rig.move->age / std::min(gStowFxShutdown, rig.move->shutdown), 0.0f,
                    1.0f);
}

/// M25.3: a slab leaves and enters the rack only as a flat screen. The share of the
/// shape's morph this frame's rig allows (`view::deployFormGate` and friends): 0 while
/// any slab is out of its clamp and its bay. 1 off the lab game path.
float rigFormGate(const app::Shell& shell, const render::Ui& ui, bool hold,
                  float intensity) {
  if (!labGame(shell, hold) || shell.session() == nullptr) return 1;
  if (const auto m = ui.rigMove(); m && !m->done) {
    if (m->move == app::ScreenMove::Deploy)
      return view::deployFormGate(m->age, m->lock, intensity);
    return view::stowFormGate(m->age, intensity);
  }
  const auto& t = gRigTrack;
  if (t.move && t.session == shell.session()->id())
    return view::relayoutFormGate(relayoutAge(), t.move->lead, t.move->seconds,
                                  intensity);
  return 1;
}

/// The shape's live morph point while a frame is drawn under a rig form gate, or -1.
float gFormedUngated = -1.0f;
/// Scales `geometryFormed` by the rig's form gate for the rest of the frame's drawing,
/// restoring it after (M25.3). Everything drawn - the surface, the parking, the ribs, a
/// seam latch - sees the gated value; the camera's framing does not (`frameBoard`).
class FormGate {
 public:
  FormGate(app::Settings& s, float gate) : settings_(s), saved_(s.geometryFormed) {
    gFormedUngated = saved_;
    settings_.geometryFormed = saved_ * gate;
  }
  ~FormGate() {
    settings_.geometryFormed = saved_;
    gFormedUngated = -1.0f;
  }
  FormGate(const FormGate&) = delete;
  FormGate& operator=(const FormGate&) = delete;

 private:
  app::Settings& settings_;
  float saved_;
};

/// The deploy's or stow's camera flight (`render::rigFlightCamera`) for this frame.
view::OrbitCamera rigFlightCamera(const RigFrame& rig, const view::OrbitCamera& game,
                                  const view::OrbitCamera& lab) {
  const auto& m = *rig.move;
  render::RigFlight f;
  f.deploy = m.move == app::ScreenMove::Deploy;
  f.age = m.age;
  f.lock = m.lock;
  f.shutdown = m.shutdown;
  return render::rigFlightCamera(*rig.layout, rig.poses, f, game, lab);
}

render::BoardOptions optionsFor(const app::Shell& shell, int seamHighlight);

/// The ribs for the board's current shape (M24.5b), joined by two spokes to the yoke
/// under its slab - the M24.5 yoke of the board's (single) slab, at its boss under the
/// slab's centre, where M24.9 centres the shape. M24.9: no rib shows outside a Klein
/// bottle's skin from `eye`, and the closed torus adds thin hoops on the outside. M25.4:
/// while the shape is formed at all, the flexible skin replaces them with one stem.
void appendSkinFrame(render::InstanceSet& instances, const app::Shell& shell,
                     const RigFrame& rig, const std::vector<render::RigPiece>& pieces,
                     const view::Vec3& eye) {
  const auto& v = shell.session()->variant();
  render::SurfacePose pose = render::surfacePose(shell.settings(), v);
  // M25.4: once the shape forms, the skin is the squares themselves - no ribs, no hoops,
  // held by the yoke at one point.
  pose.flex = render::flexBlend(pose.formed);
  pose.skin = pose.flex <= 0.0f;
  const render::PlaySurface surf = render::PlaySurface::build(v, pose);
  if (surf.empty() || surf.stacked() || rig.poses.empty()) return;
  const bool yoke = std::any_of(pieces.begin(), pieces.end(), [](const auto& p) {
    return p.kind == render::RigPiece::Kind::Yoke && p.slab == 0;
  });
  if (!yoke) return;
  if (pose.flex > 0.0f) {
    (void)render::appendRibs(
        instances, {render::skinHold(surf, render::yokeBoss(*rig.layout, rig.poses, 0))},
        render::kHoldDiameter);
    return;
  }
  const bool klein = surf.kind() == app::SurfaceKind::Klein;
  (void)render::appendRibs(
      instances, render::slabRibs(
                     surf, render::kRibCount, render::yokeBoss(*rig.layout, rig.poses, 0),
                     pose.formed, &surf.passThrough(), klein ? &eye : nullptr));
  (void)render::appendRibs(instances,
                           render::outsideHoops(surf, render::kRibCount, pose.formed),
                           render::kHoopDiameter);
}

/// Draw the lab around the board (M24.5): the camera flies between the lab's station and
/// the game camera during a move, walls between the eye and the board fade, the rig
/// hardware carries the slabs, and the room dims to focus. Returns the light block.
render::GpuLightBlock drawLabGame(const RigFrame& rig, const app::Shell& shell,
                                  render::Ui& ui, const render::BoardRenderer& renderer,
                                  render::InstanceSet& instances,
                                  view::OrbitCamera& camera, float intensity) {
  const auto& layout = *rig.layout;
  // A travelling slab carries the board itself (dark until the boot), so the lock swaps
  // nothing; picking never sees this. A relayout carries each slab's cells with it.
  if (rig.relayout)
    render::carryRelayout(instances, *rig.relayout);
  else
    render::carryBoard(instances, layout, rig.poses);
  // The eye keeps its place through a relayout and eases onto the new framing.
  if (rig.relayout && gRigTrack.fromCamera && intensity >= .5f)
    camera = view::blendCamera(*gRigTrack.fromCamera, camera,
                               view::rigEase((rig.relayoutAge - gRigTrack.move->lead) /
                                             std::max(.01f, gRigTrack.move->seconds)));
  if (rig.move && intensity >= .5f) {
    const auto& m = *rig.move;
    const bool deploy = m.move == app::ScreenMove::Deploy;
    if (const auto station =
            gLabScene->station(stationFor(deploy ? m.from : app::Screen::MainMenu)))
      camera =
          rigFlightCamera(rig, camera, render::toBoardCamera(*station, layout.xform));
  }
  render::LabDraw draw;
  draw.xform = &layout.xform;
  draw.hidden = [&](const io::LabPart& p) {
    return render::hiddenInGame(p, layout, rig.poses);
  };
  const view::Vec3 eye = camera.eye(), target = camera.target;
  if (ui.wallFade())
    draw.fade = [&](const io::LabPart& p, const view::Bounds& b) {
      return render::labPartFade(p, b, eye, target, layout);
    };
  const auto glows = render::slabGlowLights(layout, instances);
  gLabScene->appendTo(instances, renderer.theme(), {}, {}, draw);
  // M25.4: on a shape the slab body dims away as the tiles lift off it into the skin.
  const float backFade =
      renderer.options().slabSkin && renderer.options().surface &&
              !stackedShapeView(shell)
          ? render::slabBackFade(
                render::surfacePose(shell.settings(), shell.session()->variant()).formed)
          : 1.0f;
  const auto rigPieces =
      render::appendRig(instances, *gLabScene, layout, rig.poses, backFade);
  const app::Session& session = *shell.session();
  // The board's light, for what its slab edges emit (bridges, cable pulses).
  float power = 1;
  if (!instances.cellFx.empty()) {
    power = 0;
    for (std::size_t c = 1; c < instances.cellFx.size(); ++c)
      power += instances.cellFx[c].power * instances.cellFx[c].flicker;
    power /= static_cast<float>(instances.cellFx.size() - 1);
  }
  // M24.6: light joins glued slabs that cannot touch, once they hang still; 5D boards
  // are joined to the timeline they branched from by light-guide cables.
  const bool still = !rig.relayout && rig.parked <= 0.0f &&
                     std::all_of(rig.poses.begin(), rig.poses.end(),
                                 [](const auto& p) { return p.clamped; });
  if (still && ui.bridges())
    (void)render::appendBridges(instances, session.lightBridges(), power);
  (void)render::appendCables(
      instances,
      render::rigCables(layout, rig.poses, session.variant(), session.viewConfig(),
                        session.timelineLinks()),
      static_cast<float>(gRigNow), intensity >= .5f ? power : 0.0f);
  // A stacked shape view: the slabs stand by, parked high, and the nearest projector
  // casts the shape.
  if (rig.parked > 0.0f) {
    (void)render::appendStandby(instances, layout, rig.poses);
    const render::PlaySurface surf = render::PlaySurface::build(
        session.variant(), render::surfacePose(shell.settings(), session.variant()));
    if (!surf.empty()) {
      const view::Bounds b = surf.bounds();
      const view::Vec3 centre{b.centerX(), b.centerY(), b.centerZ()};
      std::optional<view::Vec3> lens;
      for (const char* name : {"projector.lens", "wallprojector.lens"})
        if (const auto a = gLabScene->doc().anchor(name)) {
          const auto at = layout.xform.toBoard(gLabScene->toWorld(*a));
          if (!lens || view::length(at - centre) < view::length(*lens - centre))
            lens = at;
        }
      if (lens) {
        render::HologramPlacement at;
        at.centre = centre;
        at.radius = b.radius();
        at.alpha = rig.parked;
        instances.holoBeam.push_back(render::projectorBeam(*lens, at, renderer.theme()));
      }
    }
  }
  // The ribs that hold the skin into its shape (M24.5b), joined to the board's yoke -
  // only on a still rig: a travelling slab carries its sheet flat (M25.3).
  if (renderer.options().slabSkin && renderer.options().surface && still)
    appendSkinFrame(instances, shell, rig, rigPieces, eye);
  const auto projected =
      appendProjector(ui, shell.settings(), instances, renderer.theme());
  render::placeInstances(instances.holo, layout.xform);
  render::placeInstances(instances.holoTokens, layout.xform);
  const std::size_t shapeBeams = rig.parked > 0.0f && !instances.holoBeam.empty() ? 1 : 0;
  {
    // The shape's beam is already in board space; the projector's own go through xform.
    std::vector<render::Instance> lab(
        instances.holoBeam.begin() + static_cast<std::ptrdiff_t>(shapeBeams),
        instances.holoBeam.end());
    render::placeInstances(lab, layout.xform);
    std::copy(lab.begin(), lab.end(),
              instances.holoBeam.begin() + static_cast<std::ptrdiff_t>(shapeBeams));
  }
  gRigTrack.lastCamera = camera;

  float focus = render::kFocusLevel;
  if (rig.is(app::ScreenMove::Deploy))
    focus =
        render::deployFocus(rig.move->age, rig.move->lock, rig.move->total, intensity);
  else if (rig.is(app::ScreenMove::Stow))
    focus = render::stowFocus(rig.move->age, rig.move->shutdown, rig.move->lock,
                              rig.move->total, intensity);
  if (const auto pin = ui.focusPin()) focus = *pin;
  if (gFocus >= 0) focus = gFocus;
  return render::labLightBlock(
      gLabScene->doc(), *gLabScene,
      gRoomLightOverride >= 0 ? gRoomLightOverride : shell.settings().roomLight,
      render::kLabKey, "projector", .5f + .5f * projected[0], renderer.theme(),
      .5f + .5f * projected[1], focus, &layout.xform, glows);
}

/// A seam latch flash on the renderer's options (M24.5b): the rail of `latch.axis` is
/// boosted by `render::latchBoost`. Only on the skin's path.
void applyLatch(render::BoardRenderer& renderer, const app::FxTimeline::LatchFx& latch) {
  render::BoardOptions o = renderer.options();
  if (!o.slabSkin) return;
  o.latchAxis = latch.axis;
  o.latchBoost = render::latchBoost(latch.age, o.presentationIntensity);
  renderer.setOptions(o);
}

/// The console's `stretch` report (M24.5b): the current skinned shape's stretch range and
/// how many cells are drawn as light.
std::string skinReport(const app::Shell& shell) {
  const render::BoardOptions o = optionsFor(shell, -1);
  if (!o.slabSkin) return {};
  const auto& v = shell.session()->variant();
  render::SurfacePose pose = render::surfacePose(shell.settings(), v);
  pose.skin = true;
  const render::PlaySurface surf = render::PlaySurface::build(v, pose);
  if (surf.stretch().empty()) return {};
  float lu = 1e9f, hu = 0, lv = 1e9f, hv = 0;
  for (const auto& c : surf.stretch()) {
    lu = std::min(lu, c.u);
    hu = std::max(hu, c.u);
    lv = std::min(lv, c.v);
    hv = std::max(hv, c.v);
  }
  char buf[200];
  std::snprintf(buf, sizeof(buf),
                "formed %.3f: u %.3f..%.3f, v %.3f..%.3f, %zu pass-through cell(s), "
                "flexible skin %.2f",
                static_cast<double>(pose.formed), static_cast<double>(lu),
                static_cast<double>(hu), static_cast<double>(lv), static_cast<double>(hv),
                surf.passThrough().size(),
                static_cast<double>(render::flexBlend(pose.formed)));
  return buf;
}

/// The console's `rig` dump: scale, groups, slab centres and their current poses.
std::string rigReport(const app::Shell& shell, const render::Ui& ui) {
  if (!labEnabled(shell) || !shell.hasGame() || !gLabScene->doc().deploy) return {};
  const auto* layout = rigLayoutFor(shell);
  if (layout == nullptr) return {};
  const auto rig = rigFrame(const_cast<app::Shell&>(shell), ui, true,
                            1.0f);  // a report never moves the camera it describes
  std::ostringstream out;
  out.precision(3);
  out << "labScale " << layout->xform.labScale << ", " << layout->groups.size()
      << " mast group(s), " << layout->slots << " departure slot(s), last lock "
      << layout->lastLock << " s";
  if (rig.move)
    out << ", " << (rig.is(app::ScreenMove::Deploy) ? "deploy" : "stow") << " at "
        << rig.move->age << " s";
  if (gRigTrack.move) {
    const auto& m = *gRigTrack.move;
    out << "\nlast relayout: " << m.matches.size() - m.arrivals.size() - m.leaving.size()
        << " moved, " << m.arrivals.size() << " arrived, " << m.leaving.size()
        << " stowed, " << m.seconds << " s"
        << (m.lead > 0 ? " after a " + std::to_string(m.lead) + " s unform" : "")
        << (rig.relayout ? ", now at " + std::to_string(rig.relayoutAge) + " s" : "");
  }
  if (rig.parked > 0) out << "\nparked high for the shape view: " << rig.parked;
  for (std::size_t g = 0; g < layout->groups.size(); ++g) {
    const auto& group = layout->groups[g];
    out << "\ngroup " << g << (group.rack() ? " (rack)" : "") << ": mast ("
        << group.mastFoot.x << ", " << group.mastFoot.y << ") foot z " << group.mastFoot.z
        << " top z " << group.mastTopZ << ", departure slot "
        << (g < layout->groupSlot.size() ? layout->groupSlot[g] : 0);
    if (group.rack()) out << ", " << group.arms.size() << " arm(s) on T-bars";
    for (const auto s : group.slabs) {
      const auto& path = layout->paths[s];
      const auto& pose = rig.poses.empty() ? view::SlabPose{} : rig.poses[s];
      out << "\n  slab " << s << " slice " << layout->slabs[s].slice << ": hang ("
          << path.hangCentre.x << ", " << path.hangCentre.y << ", " << path.hangCentre.z
          << ") now (" << pose.centre.x << ", " << pose.centre.y << ", " << pose.centre.z
          << ") mast " << pose.mastLength << (pose.clamped ? " clamped" : "")
          << (pose.inBay ? " in bay" : "");
    }
  }
  const auto& bridges = shell.session()->lightBridges();
  out << "\n" << bridges.size() << " light bridge(s)" << (ui.bridges() ? "" : " (off)");
  for (const auto& b : bridges)
    out << "\n  bridge axis " << static_cast<int>(b.axis) << ": slab " << b.slabA
        << " -> " << b.slabB << ", (" << b.a.x << ", " << b.a.y << ", " << b.a.z
        << ") -> (" << b.b.x << ", " << b.b.y << ", " << b.b.z << ")";
  return out.str();
}

// ---- M25.5 the neural VR --------------------------------------------------------------

/// --vr 0|1: a lab game out of or in the implant's space whatever its slab count (-1:
/// the count decides, `view::needsVr`). --vr-t pins the entering handoff (a deploy).
int gVr = -1;
float gVrT = -1.0f;
/// The rack's bays as authored (`render::rackBayCount`), read when the lab loads.
int gRackBays = view::kRackBays;
/// This frame's presentation intensity, for the blink clocks read before the frame is
/// assembled.
float gVrIntensity = 1.0f;

/// How a lab game is drawn this frame.
enum class VrScene : std::uint8_t {
  Physical,  ///< on the rig (M24.5), or no lab game at all
  Room,      ///< a handoff in the room: the stand-in slab and its neural code
  Space,     ///< the implant's space: the boards as light over the lucid horizon
  Picture,   ///< 2D: the implant's own front-on picture, no room, no perspective
};

/// The lab game's place in the neural VR, followed from frame to frame. Pure in explicit
/// time: every age is `gRigNow` minus a stamp.
struct VrTrack {
  std::uint64_t session{0};
  bool in{false};        ///< in the implant's space (kept for the whole game)
  double crossAt{-1e9};  ///< the room ran out mid-game, or the console moved it
  bool crossIn{true};    ///< ... into the space (false: back to the rig)
  double flatAt{-1e9};   ///< 2D or 3D was chosen
  bool flatCode{false};  ///< that blink starts with the neural code (from the room)
  view::VrSwitch flat;
  view::OrbitCamera beforeFlat;
  std::optional<bool> lastPin;
  std::vector<view::Placement> placements;  ///< the layout the light stands in
  std::vector<view::Placement> moveFrom;    ///< a relayout's start
  double moveAt{-1e9};
} gVrTrack;

/// Whether this game belongs in the implant's space: pinned (--vr, console `vr`), or more
/// slabs than the rack holds. A 5D game asks with its visible boards, so it crosses when
/// it appends the ninth.
bool vrWanted(const app::Shell& shell, const render::Ui* ui) {
  const app::Session* session = shell.session();
  if (session == nullptr) return false;
  if (gVr >= 0) return gVr == 1;
  if (ui != nullptr)
    if (const auto pin = ui->vrPin()) return *pin;
  return view::needsVr(session->placements(), gRackBays);
}

/// The session's VR state, before its first frame has been followed (a deploy is being
/// configured for it).
bool vrFor(const app::Shell& shell, const render::Ui* ui) {
  const app::Session* session = shell.session();
  if (session == nullptr) return false;
  if (gVrTrack.session == session->id()) return gVrTrack.in;
  return vrWanted(shell, ui);
}

/// Follow the game into and out of the implant's space: a new game starts where it
/// belongs (its deploy is the handoff); crossing the limit mid-game, or the console's
/// pin, blinks across; choosing 2D or 3D blinks. Once in, a game stays in.
void stepVrTrack(app::Shell& shell, render::Ui& ui) {
  app::Session* session = shell.session();
  auto& t = gVrTrack;
  if (session == nullptr) return;
  if (t.session != session->id()) {
    // A new game: the console's pin belonged to the last one.
    if (t.session != 0) ui.clearVrPin();
    t = VrTrack{};
    t.session = session->id();
    t.in = vrWanted(shell, &ui);
    t.flat.shown = t.flat.target = session->flatView();
    t.beforeFlat = session->playerCamera();
    t.lastPin = ui.vrPin();
    t.placements = session->placements();
    return;
  }
  if (!labGameAny(shell, false)) return;
  const auto pin = ui.vrPin();
  const bool pinChanged = pin != t.lastPin;
  t.lastPin = pin;
  const bool want = vrWanted(shell, &ui);
  if (want != t.in && (want || (pinChanged && pin && !*pin))) {
    t.in = want;
    t.crossAt = gRigNow;
    t.crossIn = want;
  }
  // UI and keyboard requests set the session once. Roll back that request once while
  // the eyes close; commit once at full closure. Never reframe twice per drawn frame.
  if (session->flatView() != t.flat.shown && !t.flat.active(gRigNow, gVrIntensity)) {
    const bool wanted = session->flatView();
    if (gCapturing) {
      t.flat.shown = t.flat.target = wanted;
    } else {
      (void)t.flat.request(wanted, !t.in && !t.flat.shown, gRigNow);
      t.flatAt = gRigNow;
      t.flatCode = t.flat.codeStep;
      session->setFlatView(t.flat.shown);
      session->setOrbitCamera(t.beforeFlat);
      shell.settings().flatView = t.flat.shown;
    }
  }
  if (t.flat.advance(gRigNow, gVrIntensity)) {
    session->setFlatView(t.flat.shown);
    shell.settings().flatView = t.flat.shown;
    if (t.flat.shown)
      session->setOrbitCamera(render::vrPictureCamera(
          view::boundsOf(session->placements()), session->boardAspect()));
  }
}

/// Remember the layout before a capture's script runs, so the change it makes glides.
void primeVrTrack(const app::Shell& shell) {
  const app::Session* session = shell.session();
  gVrTrack = VrTrack{};
  if (session == nullptr) return;
  gVrTrack.session = session->id();
  gVrTrack.in = vrWanted(shell, nullptr);
  gVrTrack.flat.shown = gVrTrack.flat.target = session->flatView();
  gVrTrack.beforeFlat = session->playerCamera();
  gVrTrack.placements = session->placements();
}

float vrCrossAge() {
  return static_cast<float>(gRigNow - gVrTrack.crossAt);
}
float vrFlatAge() {
  return static_cast<float>(gRigNow - gVrTrack.flatAt);
}

/// The implant's space is shown (rather than the rig), from the track and the time: a
/// crossing switches behind closed lids.
bool vrSpaceShown() {
  const auto& t = gVrTrack;
  const float age = vrCrossAge();
  if (age >= 0.0f && age < view::vrBlinkSwitch(t.crossIn, gVrIntensity)) return !t.in;
  return t.in;
}

/// The 2-D picture is shown: the choice switches behind closed lids too.
bool vrFlatShown(const app::Shell& shell) {
  (void)shell;
  return gVrTrack.flat.shown;
}

bool vrDrawn(const app::Shell& shell) {
  const app::Session* session = shell.session();
  if (session == nullptr || gVrTrack.session != session->id()) return false;
  return vrSpaceShown() || vrFlatShown(shell);
}

/// All transitions that displace the drawn cells lock input before ImGui sees it.
/// System/window events still pass, so quit and resize remain responsive.
bool vrInputLocked(const app::Shell& shell) {
  if (!labGameAny(shell, false)) return false;
  const auto& t = gVrTrack;
  const float cross = vrCrossAge();
  const float glide = static_cast<float>(gRigNow - t.moveAt);
  return t.flat.active(gRigNow, gVrIntensity) ||
         (cross >= 0 && cross < view::vrBlinkSeconds(t.crossIn, gVrIntensity)) ||
         (t.in && !t.moveFrom.empty() && gVrIntensity >= .5f && glide >= 0 &&
          glide < view::kVrRelayoutSeconds);
}

/// This frame of the neural VR.
struct VrFrame {
  VrScene scene{VrScene::Physical};
  view::VrState state;  ///< lids, darkness, code, handshake, the horizon
  const render::RigLayout* standIn{nullptr};
  std::vector<view::SlabPose> poses;  ///< the stand-in's (Room)
  std::optional<render::Ui::RigMove> move;
  /// The neural code on the rig's own slabs: a crossing or a 2D blink from the room.
  float physicalCode{0};
};

/// The stand-in slab's rig: one standard slab (`render::vrStandIn`).
const render::RigLayout* vrStandInLayout() {
  static std::optional<render::RigLayout> layout;
  static const render::LabScene* built = nullptr;
  if (built != &*gLabScene) {
    layout = render::rigLayout(*gLabScene, render::vrStandIn());
    built = &*gLabScene;
  }
  return layout ? &*layout : nullptr;
}

VrScene gVrScene = VrScene::Physical;

/// Decide this frame's picture. Before `rigFrame`; the track is already stepped.
VrFrame vrFrame(const app::Shell& shell, const render::Ui& ui, bool hold,
                float intensity) {
  VrFrame f;
  gVrScene = VrScene::Physical;
  if (!labGameAny(shell, hold) || shell.session() == nullptr ||
      gVrTrack.session != shell.session()->id())
    return f;
  const auto& t = gVrTrack;
  const bool flat = vrFlatShown(shell);
  if (t.in) {
    if (const auto m = ui.rigMove(); m && !m->done) f.move = m;
    if (f.move) f.standIn = vrStandInLayout();
    if (f.move && f.standIn != nullptr && f.move->move == app::ScreenMove::Deploy) {
      f.state = view::vrEnter(f.move->age, intensity);
      if (f.state.room)
        f.poses = render::rigPoses(*f.standIn, render::RigPhase::Deploy,
                                   std::max(0.0f, f.state.slabAge), intensity);
    } else if (f.move && f.standIn != nullptr) {
      const float lead = f.move->shutdown;
      if (f.move->age < lead || intensity < .5f) {
        f.state = view::vrLeave(f.move->age, intensity);
        if (f.state.room)
          f.poses = render::rigPoses(*f.standIn, render::RigPhase::Rest, 0, intensity);
      } else {
        f.state = view::vrLeave(view::kVrLeaveSeconds, intensity);
        f.poses = render::rigPoses(*f.standIn, render::RigPhase::Stow, f.move->age - lead,
                                   intensity);
      }
    } else {
      f.move.reset();
      f.state = view::vrEnter(view::kVrEnterSeconds, intensity);
    }
    f.scene = f.state.room && f.standIn != nullptr ? VrScene::Room : VrScene::Space;
  }
  // A crossing mid-game: the rig's slabs show the code, the eyes close, the picture
  // switches, they open (`view::vrBlink`).
  if (!f.move) {
    const float cross = vrCrossAge();
    if (cross >= 0.0f && cross < view::vrBlinkSeconds(t.crossIn, intensity)) {
      const auto blink = view::vrBlink(cross, t.crossIn, intensity);
      f.state.lidOpen = blink.lidOpen;
      f.state.dark = blink.dark;
      f.scene = vrSpaceShown() ? VrScene::Space : VrScene::Physical;
      if (f.scene == VrScene::Physical) f.physicalCode = blink.code;
    }
    const float flatAge = vrFlatAge();
    if (flatAge >= 0.0f && flatAge < view::vrBlinkSeconds(t.flatCode, intensity)) {
      const auto blink = view::vrBlink(flatAge, t.flatCode, intensity);
      f.state.lidOpen = std::min(f.state.lidOpen, blink.lidOpen);
      f.state.dark = std::max(f.state.dark, blink.dark);
      if (f.scene == VrScene::Physical && !flat) f.physicalCode = blink.code;
    }
  }
  if (flat && f.scene != VrScene::Room) f.scene = VrScene::Picture;
  gVrScene = f.scene;
  return f;
}

/// The stand-in's game camera: the standard board framed, from the session's angle.
view::OrbitCamera vrStandInCamera(const view::OrbitCamera& session, float aspect) {
  view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(render::vrStandIn()), aspect, 1.4f);
  cam.fovY = session.fovY;
  return cam;
}

/// A handoff in the room (M25.5): the lab around the stand-in slab, which deploys (or
/// stows) exactly as a standard game's and shows the neural code; no board of the game is
/// in the room. Returns the light block.
render::GpuLightBlock drawVrRoom(const VrFrame& vr, const app::Shell& shell,
                                 render::Ui& ui, const render::BoardRenderer& renderer,
                                 render::InstanceSet& instances,
                                 view::OrbitCamera& camera, float aspect,
                                 float intensity) {
  const auto& layout = *vr.standIn;
  const view::OrbitCamera game = vrStandInCamera(camera, aspect);
  camera = game;
  if (vr.move && intensity >= .5f) {
    const auto& m = *vr.move;
    const bool deploy = m.move == app::ScreenMove::Deploy;
    if (const auto station =
            gLabScene->station(stationFor(deploy ? m.from : app::Screen::MainMenu))) {
      render::RigFlight f;
      f.deploy = deploy;
      f.age = m.age;
      f.lock = deploy ? view::kVrSlabLock : m.lock;
      f.shutdown = deploy ? 0.0f : m.shutdown;
      camera = render::rigFlightCamera(layout, vr.poses, f, game,
                                       render::toBoardCamera(*station, layout.xform));
    }
  }
  render::LabDraw draw;
  draw.xform = &layout.xform;
  draw.hidden = [&](const io::LabPart& p) {
    return render::hiddenInGame(p, layout, vr.poses);
  };
  const view::Vec3 eye = camera.eye(), target = camera.target;
  if (ui.wallFade())
    draw.fade = [&](const io::LabPart& p, const view::Bounds& b) {
      return render::labPartFade(p, b, eye, target, layout);
    };
  gLabScene->appendTo(instances, renderer.theme(), {}, {}, draw);
  (void)render::appendRig(instances, *gLabScene, layout, vr.poses);
  (void)render::appendNeuralCode(instances, layout, vr.poses, vr.state.code,
                                 static_cast<float>(gRigNow), renderer.theme());
  const auto projected =
      appendProjector(ui, shell.settings(), instances, renderer.theme());
  render::placeInstances(instances.holo, layout.xform);
  render::placeInstances(instances.holoTokens, layout.xform);
  render::placeInstances(instances.holoBeam, layout.xform);
  float focus = render::kFocusLevel;
  if (vr.move && vr.move->move == app::ScreenMove::Deploy)
    focus = render::deployFocus(vr.move->age, view::kVrSlabLock, view::kVrSlabLock + 1.0f,
                                intensity);
  else if (vr.move)
    focus = render::stowFocus(vr.move->age, vr.move->shutdown, vr.move->lock,
                              vr.move->total, intensity);
  if (const auto pin = ui.focusPin()) focus = *pin;
  if (gFocus >= 0) focus = gFocus;
  return render::labLightBlock(
      gLabScene->doc(), *gLabScene,
      gRoomLightOverride >= 0 ? gRoomLightOverride : shell.settings().roomLight,
      render::kLabKey, "projector", .5f + .5f * projected[0], renderer.theme(),
      .5f + .5f * projected[1], focus, &layout.xform);
}

/// The implant's space (M25.5): the boards are light (`BoardOptions::vrLight`), laid out
/// by the current view, gliding through a relayout; light bridges are streaks across the
/// space; the lucid horizon lies under them. No rig hardware is built or drawn. The 2-D
/// picture is the same light, front on, with no horizon.
void drawVrSpace(const VrFrame& vr, const app::Shell& shell, const render::Ui& ui,
                 render::BoardRenderer& renderer, render::InstanceSet& instances,
                 float intensity) {
  const app::Session& session = *shell.session();
  auto& t = gVrTrack;
  const auto& now = session.placements();
  if (!samePlacements(t.placements, now)) {
    if (!t.placements.empty()) {
      t.moveFrom = t.moveFrom.empty()
                       ? t.placements
                       : view::vrRelayoutPlacements(
                             t.moveFrom, t.placements,
                             static_cast<float>(gRigNow - t.moveAt), intensity);
      t.moveAt = gRigNow;
    }
    t.placements = now;
  }
  // A capture states the glide's age (--relayout-t) or snaps, as the rig's relayout does.
  const float age = !gCapturing          ? static_cast<float>(gRigNow - t.moveAt)
                    : gRelayoutT >= 0.0f ? gRelayoutT
                                         : view::kVrRelayoutSeconds;
  // A drawn shape is placed by its own sampling, not the layout (M25.6): no glide.
  const bool follows = render::vrCarriesLayout(renderer.options(), session.variant());
  if (follows && !t.moveFrom.empty() && age >= 0.0f && age < view::kVrRelayoutSeconds) {
    const std::size_t cells = session.snapshot().cellCount();
    std::vector<view::Vec3> offset(cells);
    const auto drawn = view::vrRelayoutPlacements(t.moveFrom, now, age, intensity);
    for (std::size_t k = 0; k < now.size(); ++k)
      if (now[k].cell < cells)
        offset[now[k].cell] = {drawn[k].x - now[k].x, drawn[k].y - now[k].y,
                               drawn[k].z - now[k].z};
    render::carryVr(instances, now, offset);
  }
  render::BoardOptions o = renderer.options();
  o.labMode = false;
  const bool picture = vr.scene == VrScene::Picture;
  o.lucidHorizon = picture ? 0.0f : std::max(vr.state.horizon, 1e-3f);
  const view::Bounds b = view::boundsOf(now);
  o.lucidFloorZ = b.minZ - 6.0f;
  renderer.setOptions(o);
  if (picture || !ui.bridges() || !follows) return;
  float power = 1;
  if (!instances.cellFx.empty()) {
    power = 0;
    for (std::size_t c = 1; c < instances.cellFx.size(); ++c)
      power += instances.cellFx[c].power * instances.cellFx[c].flicker;
    power /= static_cast<float>(instances.cellFx.size() - 1);
  }
  // Quiet guides that fade with depth from the camera the boards are drawn with.
  const view::Vec3 eye = session.camera().eye();
  // Rebuild from the drawn placements so endpoints ride their moving boards.
  if (!t.moveFrom.empty() && age >= 0 && age < view::kVrRelayoutSeconds) {
    const auto drawn = view::vrRelayoutPlacements(t.moveFrom, now, age, intensity);
    const auto bridges = view::lightBridges(session.variant(), session.viewConfig(),
                                            drawn, renderer.theme());
    (void)render::appendVrBridges(instances, bridges, power, renderer.theme(), eye);
  } else {
    (void)render::appendVrBridges(instances, session.lightBridges(), power,
                                  renderer.theme(), eye);
  }
}

/// The eyelids and the darkness of a handoff or a blink, through the wake's post pass
/// (M24.7's lids); the launch's own wake wins while it runs.
void applyVrLids(render::BoardRenderer& renderer, const VrFrame& vr,
                 const WakeFrame& wake) {
  if (wake.postActive) return;
  const auto& s = vr.state;
  if (s.lidOpen >= 1.0f && s.dark <= 0.0f) return;
  // Exposure alone leaves the post dither's one-code-value specks. At full darkness
  // use the wake's opaque mask; all other calm frames keep their lids fully open.
  const view::WakeState lids = view::eyelids(s.dark >= 1.0f ? 0.0f : s.lidOpen);
  const float ev = s.dark >= 1.0f ? -24.0f : std::log2(std::max(1e-6f, 1.0f - s.dark));
  renderer.setWake(render::PostWake{lids.topEdge, lids.bottomEdge, lids.curve,
                                    lids.feather, 0.0f, ev, 1.0f, 0.0f});
}

/// The implant's handshake answering the neural code (M24.7's, played by the handoff).
WakeFrame vrWake(const VrFrame& vr, const WakeFrame& wake) {
  WakeFrame out = wake;
  if (!wake.postActive && vr.state.handshake >= 0.0f) {
    out.handshake = vr.state.handshake;
    out.menuFade = 1.0f;
  }
  if (!wake.postActive) out.menuFade *= (1.0f - vr.state.dark) * vr.state.lidOpen;
  return out;
}

/// The console's `vr status`.
std::string vrReport(const app::Shell& shell) {
  const app::Session* session = shell.session();
  if (!labEnabled(shell) || session == nullptr || !shell.hasGame()) return {};
  const int slabs = view::deployedSlabCount(session->placements());
  std::ostringstream out;
  const char* scene = gVrScene == VrScene::Room      ? "handoff in the room"
                      : gVrScene == VrScene::Space   ? "in the implant's space"
                      : gVrScene == VrScene::Picture ? "the implant's 2-D picture"
                                                     : "physical (on the rig)";
  out << "vr: " << scene << ", " << slabs << " slab(s) "
      << (slabs > gRackBays ? ">" : "<=") << " " << gRackBays << " bay(s)"
      << (gVrTrack.in ? ", in VR" : ", in the room");
  if (gVr >= 0) out << ", pinned by --vr " << gVr;
  return out.str();
}
#endif

float gIntensity = -1.0f;  ///< --presentation: presentation intensity for this run
float gCaptureT = -1.0f;
/// --post 0|1 and --bloom <scale>: this run only, never saved. Negative means "use the
/// setting".
int gPostOverride = -1;
float gBloomOverride = -1.0f;
/// --boot-t <s>: render the board as if the boot sequence were `s` seconds old. Negative
/// means no override. Captures only; they never run a clock of their own.
float gBootT = -1.0f;
/// --check-t / --end-t <s>: render the check ripple, or the game-end power-down, at that
/// age for the session's current check / ended game. Negative means no override.
float gCheckT = -1.0f;
float gEndT = -1.0f;
/// --event-t / --think-t <s>: the HUD turn flip and newest ledger row, and the engine
/// thinking dots, at that age (M23.11 section 4). Negative means settled.
float gEventT = -1.0f;
float gThinkT = -1.0f;
float gTransitionT =
    -1.0f;          ///< --transition-t: the age a previewed screen move is pinned to
std::string gFrom;  ///< --from: the screen that move comes from
bool gResult = false;
/// `--review <ply>`: the capture browses to that ply of the scripted game (empty: off).
std::string gReview;

float resolveUiScale(const app::Settings& settings, int height, bool capture) {
  return render::effectiveUiScale(settings.guiScale, settings.guiScaleAuto, height,
                                  capture, gUiAuto, gUiScaleOverride);
}

/// Hand the renderer the effective post settings, and say once if the device cannot run
/// it.
void applyPost(render::BoardRenderer& renderer, const app::Settings& settings) {
  const bool want = gPostOverride >= 0 ? gPostOverride == 1 : settings.postProcess;
  renderer.setPost(want, gBloomOverride >= 0.0f ? gBloomOverride : settings.bloomScale);
  static bool reported = false;
  if (want && !renderer.postAvailable() && !reported) {
    reported = true;
    std::fprintf(stderr, "%s; continuing without it\n",
                 renderer.postUnavailable().c_str());
  }
}

/// Whether the HDR path will run, decided from the settings so a capture's first frame
/// agrees with its last (applyPost only runs after the interface is built).
bool postWanted(const render::BoardRenderer& renderer, const app::Settings& settings) {
  return renderer.postAvailable() &&
         (gPostOverride >= 0 ? gPostOverride == 1 : settings.postProcess);
}

void setFxTint(render::BoardFxState& fx, const render::BoardRenderer& renderer) {
  const view::Rgba blood = renderer.theme().blood;
  fx.tint[0] = blood.r;
  fx.tint[1] = blood.g;
  fx.tint[2] = blood.b;
  const auto& t = renderer.theme();
  const auto winner = fx.end.loser == Color::White ? t.blackPiece : t.whitePiece;
  fx.winnerColor[0] = winner.r;
  fx.winnerColor[1] = winner.g;
  fx.winnerColor[2] = winner.b;
  fx.drawColor[0] = t.bone.r;
  fx.drawColor[1] = t.bone.g;
  fx.drawColor[2] = t.bone.b;
}

/// The HUD panels follow the board (M23.10 section 7): their edges trace on during the
/// first 0.4 s of a boot and their contents fade in over the next 0.4 s; a finished game
/// dims them to 0.7 (the status panel stays lit).
void applyHudFx(render::Ui& ui, std::optional<float> bootAge,
                std::optional<float> endAge) {
  const auto smooth = [](float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
  };
  float draw = 1.0f, content = 1.0f, level = 1.0f;
  if (bootAge) {
    draw = std::clamp(*bootAge / 0.4f, 0.0f, 1.0f);
    content = smooth((*bootAge - 0.4f) / 0.4f);
  }
  if (endAge) level = 1.0f - 0.3f * smooth((*endAge - view::kEndDelaySeconds) / 1.5f);
  ui.setHudFx(draw, content, level);
}

/// Game to menu (M23.11 section 6): the menu screen has no board, yet for 0.8 s the board
/// stays up and powers down from its centre. True while that runs.
bool holdsBoard(const app::Shell& shell, const render::Ui& ui) {
  return shell.hasGame() && ui.holdsBoard();
}

/// The board's decisive power-down with no delay, compressed into the move's 0.8 s.
void applyMoveHold(render::BoardFxState& fx, const app::Session& session,
                   float progress) {
  const CellId centre = session.game().position().cellCount() / 2;
  const std::vector<int>& dist = session.distancesFrom(centre);
  int far = 0;
  for (const int d : dist) far = std::max(far, d);
  fx.end = view::EndFx{view::EndKind::Decisive, Color::White, centre, false};
  fx.endAge = app::endAgeAt(progress, far);
  fx.compressedShutdown = true;
  fx.holdProgress = progress;
}

#ifdef CB_HAVE_IMGUI
/// The board held while its screen leaves: the menu move's power-down, or a stow's
/// compressed 1 s shutdown (M24.5) - none when the game had already ended and shut down.
void applyHold(render::BoardFxState& fx, const app::Session& session,
               const render::Ui& ui, const RigFrame& rig) {
  if (!rig.is(app::ScreenMove::Stow))
    applyMoveHold(fx, session, ui.powerDownProgress());
  else if (const auto progress = stowShutdown(rig))
    applyMoveHold(fx, session, *progress);
}
#endif

/// The effects a capture asks for through --boot-t, in the renderer's own terms.
render::BoardFxState captureFx(const render::BoardRenderer& renderer,
                               const app::Shell& shell) {
  render::BoardFxState fx;
  if (gBootT >= 0.0f) fx.bootAge = gBootT;
  const app::Session& session = *shell.session();
#ifdef CB_HAVE_IMGUI
  // A pinned deploy: the board stays dark until the last slab locks, then boots.
  if (gDeployT >= 0.0f && labGame(shell, false))
    if (const auto* layout = rigLayoutFor(shell))
      fx.bootAge = deployBootAge(gDeployT, renderer.options().presentationIntensity < .5f
                                               ? 0.0f
                                               : layout->lastLock);
  // M25.5: the boards boot as the lucid horizon opens.
  if (gDeployT >= 0.0f && labGameAny(shell, false) && vrDrawn(shell))
    fx.bootAge = deployBootAge(gDeployT, renderer.options().presentationIntensity < .5f
                                             ? 0.0f
                                             : view::kVrEnterSeconds);
  applySlabBoot(fx, shell, renderer.options().presentationIntensity);
#endif
  if (gCheckT >= 0.0f) {
    // The ripple of the last check; a position that never gave one ripples from the
    // royal of the side to move, so any board can be reviewed.
    fx.rippleAge = gCheckT;
    fx.rippleOrigin = session.checkEvent().value_or(
        session.game().position().findRoyal(session.game().position().sideToMove()));
  }
  if (gEndT >= 0.0f && session.endEvent() && !shell.attracting()) {
    fx.endAge = gEndT;
    fx.end = *session.endEvent();
  }
  fx.reviewing = session.reviewPly().has_value();
  // The trailer's first board powers on on the trailer's own clock.
  if (shell.attracting()) fx.bootAge = shell.trailerBootAge();
  fx.intensity = renderer.options().presentationIntensity;
  fx.ledCells = renderer.theme().materials.cells == view::Material::Led;
  setFxTint(fx, renderer);
  return fx;
}

view::Theme resolveTheme(const app::Settings& settings) {
  view::Theme theme =
      view::themeFromName(gThemeOverride.empty() ? settings.theme : gThemeOverride);
  if (gMatteCapture) theme.materials = view::Materials{};
  return theme;
}

/// Where the variants live, whether running from the build tree or an install.
std::filesystem::path variantDir() {
  for (const char* base :
       {"variants", "../variants", "../../variants", "share/endspielzero/variants",
        "/usr/share/endspielzero/variants"}) {
    if (std::filesystem::is_directory(base)) return base;
  }
  return "variants";
}

bool loadLab(render::BoardRenderer& renderer) {
  const auto file = variantDir().parent_path() / "assets/lab/lab.toml";
  auto doc = io::loadLabDoc(file);
  if (!doc) {
    std::fprintf(stderr, "lab disabled: %s\n", doc.error().format().c_str());
    return gStation.empty();
  }
  auto lab = render::LabScene::create(std::move(*doc));
  if (!lab) {
    std::fprintf(stderr, "lab disabled: %s\n", lab.error().format().c_str());
    return gStation.empty();
  }
  if (!gStation.empty() && !lab->station(gStation)) {
    std::fprintf(stderr, "unknown lab station: %s\n", gStation.c_str());
    return false;
  }
  try {
    lab->setFileRanges(renderer.addLabMeshes(lab->fileMeshes()));
    gLabScene = std::move(*lab);
#ifdef CB_HAVE_IMGUI
    gRackBays = render::rackBayCount(gLabScene->doc());
#endif
  } catch (const std::exception& e) {
    std::fprintf(stderr, "lab disabled: %s\n", e.what());
    return gStation.empty();
  }
  return true;
}

/// Where the player's own variants live, or empty when only the shipped ones are in use
/// (a capture without the user's settings must not depend on what is in their home). The
/// piece designer saves here, never over a shipped file, and a variant saved under a
/// shipped name takes its place in the library.
static std::filesystem::path gUserVariantDir;

/// The file a variant is read from: the player's own copy if there is one, else shipped.
std::filesystem::path variantFile(const std::string& name) {
  if (!gUserVariantDir.empty()) {
    const auto mine = gUserVariantDir / (name + ".toml");
    if (std::filesystem::is_regular_file(mine)) return mine;
  }
  return variantDir() / (name + ".toml");
}

std::vector<std::string> variantLibrary() {
  std::vector<std::string> names;
  std::error_code ec;
  const auto list = [&](const std::filesystem::path& dir) {
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
      if (e.is_regular_file() && e.path().extension() == ".toml") {
        names.push_back(e.path().stem().string());
      }
    }
  };
  list(variantDir());
  if (!gUserVariantDir.empty()) list(gUserVariantDir);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
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
    auto v = loadVariantFile(variantFile(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  // The trailer, and nothing else, plays the variants exactly as they ship.
  shell->setShippedVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(variantDir() / (name + ".toml"));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  // The editor reads and writes the author's own file, not the resolved spec.
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    const auto path = variantFile(name);
    std::ifstream in(path);
    if (!in) {
      return fail(ErrorCode::ParseError,
                  "cannot open variant file '" + path.string() + "'");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  });
  // Saved into the player's own directory when there is one: a designer that overwrote
  // the shipped files would damage the game it is editing.
  shell->setVariantPathResolver([](const std::string& name) {
    if (gUserVariantDir.empty()) return variantDir() / (name + ".toml");
    std::error_code ec;
    std::filesystem::create_directories(gUserVariantDir, ec);
    return gUserVariantDir / (name + ".toml");
  });
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
  o.nearClip = s.nearClip;
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
  o.presentationIntensity = s.presentationIntensity;
  o.adjacentRings = s.adjacentRings;
  o.adjacentAmount = s.adjacentFormed;
  return o;
}

/// The board options for the shell's current variant: the geometry view is offered only
/// where there is a surface to become, so `standard` and every non-glued board stay flat.
render::BoardOptions optionsFor(const app::Shell& shell, int seamHighlight = -1) {
  render::BoardOptions o = optionsFrom(shell.settings());
  o.seamHighlightAxis = seamHighlight;
  o.labMode = labBackdrop(shell, false);
  // The trailer clips further in (see `Settings::trailerNearClip`).
  if (shell.attracting())
    o.nearClip = std::max(o.nearClip, shell.settings().trailerNearClip);
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
  // M22: the tiled view's one rule, which the interface's flat tokens read too.
  o.showAdjacent = view::adjacentDrawn(s.showAdjacent, s.adjacentLeaving, o.surface);
  // M22.5: the copy under the pointer answers it faintly.
  if (o.showAdjacent && session != nullptr) {
    o.hoverCopyU = session->hoverCopyU();
    o.hoverCopyV = session->hoverCopyV();
  }
  // The slab skin (M24.5b) lives on the lab game path only.
  o.slabSkin = gSkinOn && o.surface && labGame(shell, true);
  // A stacked shape view in the lab is projected light (M24.6).
  if (o.surface && labGame(shell, true) && stackedShapeView(shell) &&
      s.geometryFormed > 1e-4f)
    o.stackedLight = render::kStackedLightOpacity;
  // M25.5: drawn by the implant - its space or its 2-D picture - the boards are light.
  if (labGameAny(shell, true) && vrDrawn(shell)) {
    o.vrLight = true;
    if (o.surface && stackedShapeView(shell) && s.geometryFormed > 1e-4f)
      o.stackedLight = render::kStackedLightOpacity;
  }
#else
  (void)shell;
#endif
  return o;
}

/// Resolve a run-only legend pin once the command-line session has loaded.
bool pinSeamHover(app::Shell& shell) {
  if (gSeamHover.empty()) return true;
  if (auto* session = shell.session()) {
    const auto legend = view::seamLegend(session->seams(), session->viewConfig(),
                                         session->variant().dims);
    for (const auto& entry : legend) {
      if (entry.axis == gSeamHover) {
        session->setSeamHoverPin(entry.screenAxis);
        return true;
      }
    }
  }
  std::fprintf(stderr, "--seam-hover: no glued axis named %s\n", gSeamHover.c_str());
  return false;
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
view::OrbitCamera blendFollowCamera(const view::OrbitCamera& base,
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
  view::OrbitCamera cam = shell.trailerView(boardCameraBase(shell, seq));
  // M22.6: the tiled view's copies arrive from `kAdjacentFar` boards out, past the board's
  // framed far plane; the plane reaches them, so they never cross it with a hard edge.
  // At amount 0 none is drawn, and the plain board's projection stays exactly as it was.
  if (optionsFor(shell).showAdjacent && shell.settings().adjacentFormed > 0.0f)
    cam.farZ = view::adjacentFarZ(cam, session->placements(), session->variant(),
                                  session->viewConfig(), shell.settings().adjacentRings);
  return cam;
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
        return blendFollowCamera(settled, want,
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

/// `lab`: a lab game frames its board on more of the screen (`render::labStageArea`,
/// M24.9); the same framing feeds the pick ray, so clicks stay exact.
bool applyStage(app::Session& session, const render::UiRequest& request,
                render::BoardRect viewport, bool enabled, bool lab) {
  if (!enabled || request.stageRect[2] <= 0 || request.stageRect[3] <= 0)
    return session.setFramingArea(1, 1);
  const float cx = viewport.x + viewport.width * .5f;
  const float cy = viewport.y + viewport.height * .5f;
  const float halfW = std::min(cx - request.stageRect[0],
                               request.stageRect[0] + request.stageRect[2] - cx);
  const float halfH = std::min(cy - request.stageRect[1],
                               request.stageRect[1] + request.stageRect[3] - cy);
  const float w = 2 * halfW / viewport.width, h = 2 * halfH / viewport.height;
  if (lab) {
    const auto area = render::labStageArea(w, h);
    return session.setFramingArea(area[0], area[1]);
  }
  return session.setFramingArea(w, h);
}

/// Put the camera round whatever the board has just become.
///
/// The shape and the flat board are different sizes and sit in different places, so a
/// toggle that left the camera where it was left the board off in a corner of the
/// window. The angle the player was looking from is kept: only the frame moves.
void frameBoard(app::Shell& shell) {
  app::Session* session = shell.session();
  if (session == nullptr) return;
  if (labEnabled(shell) && session->flatView()) {
    session->setOrbitCamera(render::vrPictureCamera(view::boundsOf(session->placements()),
                                                    session->boardAspect()));
    return;
  }
  // Under a rig form gate (M25.3) the camera still frames the shape the game is in.
  app::Settings& s = shell.settings();
  const float drawn = s.geometryFormed;
  if (gFormedUngated >= 0.0f) s.geometryFormed = gFormedUngated;
  render::frameGeometryCamera(*session, optionsFor(shell),
                              poseFrom(shell.settings(), session->variant()));
  s.geometryFormed = drawn;
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
  const render::PlaySurface surf = render::PlaySurface::build(
      session->variant(), poseFrom(shell.settings(), session->variant()));
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
  // The variant's own piece bodies, if it brings any, before the board is built from
  // them.
  if (auto ok = renderer.syncBodies(session.variant()); !ok.has_value()) {
    std::fprintf(stderr, "%s\n", ok.error().format().c_str());
  }
  // The move at the review cursor while reviewing, else the game's last.
  if (const auto last = session.shownLastMove()) {
    renderer.setLastMove(last->first, last->second);
  } else {
    renderer.setLastMove(kInvalidCell, kInvalidCell);
  }
  renderer.setCheckCell(
      session.game().inCheck() && !session.reviewPly()
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
                 const std::string& watchName, bool pieceCylinders, bool attractTimeGiven,
                 double visualOrigin) {
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
  if (!pinSeamHover(*shell)) return 1;
  // M24.6: the lab follows the layout the script changes, and a move it plays can ride a
  // light bridge.
  gCapturing = true;
  if (shell->session() != nullptr) {
    primeRigTrack(*shell);
#ifdef CB_HAVE_IMGUI
    primeVrTrack(*shell);
#endif
    shell->session()->setLightBridges(gLab == 1);
  }
  if (!playing && !script.empty()) {
    if (auto ok = shell->session()->applyScript(script); !ok.has_value()) {
      std::fprintf(stderr, "script: %s\n", ok.error().format().c_str());
      return 1;
    }
  }
  if (!playing && !gReview.empty() && shell->session() != nullptr) {
    app::Action review;
    review.kind = app::ActionKind::ReviewJump;
    review.text = gReview;
    if (auto ok = shell->session()->apply(review); !ok.has_value()) {
      std::fprintf(stderr, "review: %s\n", ok.error().format().c_str());
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
  if (gIntensity >= 0.0f) shell->settings().presentationIntensity = gIntensity;
  // M22.2 the tiled view, for review the way every other state is reviewable.
  if (gAdjacent) shell->settings().showAdjacent = true;
  if (gAdjacentRings > 0) shell->settings().adjacentRings = gAdjacentRings;
  if (gAdjacentAmount >= 0.0f) shell->settings().adjacentFormed = gAdjacentAmount;
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
  else if (screen == "settings") {
    // Captures start a game first. Explicit lab Settings captures review the
    // menu's tesseract, so enter through the menu just as the player does.
    if (gLab == 1) shell->go(app::Screen::MainMenu);
    shell->go(app::Screen::Settings);
  } else if (screen == "editor")
    shell->go(app::Screen::Editor);
  else if (screen == "designer" || screen == "body" || screen == "flat" ||
           screen == "identity" || screen == "castling" || screen == "marks" ||
           screen == "fields" || screen == "rules") {
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
  else if (screen == "pausequit") {
    shell->pause();
    shell->go(app::Screen::PauseQuitConfirm);
  } else if (screen == "watch")
    shell->go(app::Screen::Watch);

  // `--from X --transition-t S`: the screen move from X to the chosen screen, S seconds
  // in (M23.11 section 6). The preview names the source; the clock is pinned per frame.
  if (!gFrom.empty()) {
    static const std::pair<const char*, app::Screen> kFrom[]{
        {"menu", app::Screen::MainMenu},    {"game", app::Screen::Game},
        {"pause", app::Screen::Paused},     {"newgame", app::Screen::NewGame},
        {"watch", app::Screen::Watch},      {"welcome", app::Screen::Welcome},
        {"quit", app::Screen::QuitConfirm}, {"settings", app::Screen::Settings},
        {"info", app::Screen::GameInfo},    {"pieces", app::Screen::PieceMoves},
        {"editor", app::Screen::Editor}};
    bool known = false;
    for (const auto& [name, from] : kFrom) {
      if (gFrom == name) {
        shell->previewTransition(from, shell->screen());
        known = true;
      }
    }
    if (!known) {
      std::fprintf(stderr, "--from: unknown screen %s\n", gFrom.c_str());
      return 1;
    }
  }

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
  if (screen == "menu" && gFrom.empty() && attractTimeGiven && !geometry &&
      playFile.empty() && watchName.empty()) {
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
  if (!loadLab(*renderer)) return 1;
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(*ctx, window, renderer->theme());
  if (!ui.has_value()) {
    std::fprintf(stderr, "%s\n", ui.error().format().c_str());
    return 1;
  }
  if (gSkin >= 0) (*ui)->setSkin(gSkin == 1);
  gSkinOn = (*ui)->skin();
  // The capture has to show the interface at the scale the player set, or it is not a
  // picture of what they would see.
  (*ui)->forceDevOverlay(gDevOverlay);
  (void)(*ui)->setScale(
      resolveUiScale(shell->settings(), static_cast<int>(target->height()), true));
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  // Cinema dresses the frame with a grade and a vignette, not an interface (M18.3).
  (*ui)->setCinema(cinema);
  bool lastLabTheme = false;
  {
    const view::Theme theme = resolveTheme(shell->settings());
    renderer->setTheme(theme);
    (*ui)->setTheme(theme);
  }
  // Which page of the editor a designer capture wants. Set here rather than inside the
  // loop: it is where the review is pointed, not something a frame decides.
  if (!gPieceName.empty()) (*ui)->openEditorPiece(gPieceName);
  if (screen == "designer") (*ui)->openEditorPage(1, 0);
  if (screen == "body") (*ui)->openEditorPage(1, 1);
  if (screen == "flat") (*ui)->openEditorPage(1, 2);
  if (screen == "identity") (*ui)->openEditorPage(1, 3);
  if (screen == "castling") (*ui)->openEditorPage(1, 4);
  if (screen == "marks") (*ui)->openEditorPage(1, 5);
  if (screen == "fields") (*ui)->openEditorPage(1, 6);
  if (screen == "rules") (*ui)->openEditorPage(1, 7);
  (*ui)->setPreviewDims(previewDims);
  // Point the library at the named variant, so a capture of a variant with a derived
  // overture shows *it* rather than the standard construction the first entry opens on.
  if (screen == "newgame" || gFrom == "newgame") (*ui)->pickVariant(variantName);
  if (gFrom == "newgame" && screen != "newgame") {
    shell->overtures().jumpTo(app::overtureFor(variantName));
    shell->overtures().setProgress(overtureT);
  }
  // One frame, built and rendered at whatever `t` the caller has pinned. Split out from
  // writing so `--clip` can call it once per frame. `dt` is the interface's own clock:
  // the warm-up gives it enough to settle a screen transition, and the real frames give
  // it none so the drifting field is the same in every frame of a clip.
  struct VisualSample {
    double seconds;
  };
  const auto renderFrame = [&](float dt, VisualSample sample = {0},
                               const render::ShapeMoveSequence* playSeq =
                                   nullptr) -> std::optional<render::Image> {
    // An explicit end age includes the final move landing. A frozen handoff copy would
    // otherwise show the mover at its starting position in --shot/--play.
    // Keep an explicitly requested intermediate --move-t for animation inspection.
    if (gEndT >= 0.0f && !shell->attracting() && shell->session() != nullptr &&
        shell->session()->endEvent() && (moveT < 0.0f || moveT >= 1.0f)) {
      auto& session = *shell->session();
      if (session.animation().active()) {
        const float duration = session.animation().duration();
        if (gEndT >= duration)
          (void)session.advanceAnimation(gEndT);
        else if (duration > 0.0f)
          session.setMoveProgress(gEndT / duration);
      }
    }
    renderer->setVisualTime(visualOrigin + sample.seconds);
    (*ui)->setVisualTime(visualOrigin + sample.seconds);
    {
      if (shell->showsBoard() && shell->session() != nullptr) {
        const render::BoardFxState fx = captureFx(*renderer, *shell);
        applyHudFx(**ui, fx.bootAge, fx.endAge);
      }
    }
    // A fixed step rather than a real clock: the shell animates, and a capture has to be
    // the same picture every time it is taken.
    (*ui)->setResultAge(gResult && gEndT >= 0 ? std::optional<float>(gEndT)
                                              : std::nullopt);
    if (gResult && gEndT < 0) (void)shell->session()->showResult();
    (*ui)->setCaptureAge(gCaptureT >= 0 ? std::optional<float>(gCaptureT) : std::nullopt);
    {
      const auto age = [](float t) {
        return t >= 0 ? std::optional<float>(t) : std::nullopt;
      };
      (*ui)->setHudEvents({age(gEventT), age(gEventT), age(gThinkT), age(gCheckT)});
    }
    (*ui)->setPresentationIntensity(renderer->options().presentationIntensity);
    (*ui)->pinTransitionAge(gTransitionT >= 0 ? std::optional<float>(gTransitionT)
                                              : std::nullopt);
    (*ui)->tick(dt);
    (*ui)->setLabDoc(labEnabled(*shell) ? &gLabScene->doc() : nullptr);
    (*ui)->setLabBackdrop(labBackdrop(*shell, holdsBoard(*shell, **ui)));
    const bool implantActive = configureImplantUi(**ui, *shell, renderer->theme(),
                                                  lastLabTheme, holdsBoard(*shell, **ui));
    const WakeFrame wake =
        stepWake(shell->settings(), true, visualOrigin + sample.seconds,
                 renderer->options().presentationIntensity,
                 implantActive && postWanted(*renderer, shell->settings()));
    applyWake(*renderer, wake, target->height());
    (*ui)->setStationCamera([&](app::Screen which) -> std::optional<view::OrbitCamera> {
      if (!labEnabled(*shell)) return std::nullopt;
      return gLabScene->station(stationFor(which));
    });
    (*ui)->setFocusFor(focusFor);
    configureRigMoves(**ui, *shell);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, 60.0f);
    (*ui)->endFrame();
    const render::BoardRect uiRect{request.boardRect[0], request.boardRect[1],
                                   request.boardRect[2], request.boardRect[3]};
    // Cinema is the board and the move and nothing else: the whole frame is the board
    // and the interface builds no furniture, only the frame's grade and vignette
    // (M18.3), so a clip reads as footage rather than a screenshot of the panels.
    render::BoardRect frame =
        cinema ? render::BoardRect{0.0f, 0.0f, static_cast<float>(target->width()),
                                   static_cast<float>(target->height())}
               : uiRect;
    render::InstanceSet instances;
    const bool hold = holdsBoard(*shell, **ui);
    const float intensity = renderer->options().presentationIntensity;
    gRigNow = visualOrigin + sample.seconds;
    // M25.5: the neural VR decides which picture this is before anything is built.
    gVrIntensity = intensity;
    stepVrTrack(*shell, **ui);
    const VrFrame vr = vrFrame(*shell, **ui, hold, intensity);
    shell->session()->setLightBridges(
        (labGame(*shell, hold) || vr.scene == VrScene::Space) && (*ui)->bridges());
    // M25.3: the shape stays flat while any slab is off its clamp.
    const FormGate formGate(shell->settings(),
                            rigFormGate(*shell, **ui, hold, intensity));
    const RigFrame rig = rigFrame(*shell, **ui, hold, intensity);
    // The geometry view builds the warped board as ordinary instances, so the same
    // render path draws it (M17); only the surface flag on the options selects it.
    if ((shell->showsBoard() || hold) && gStation.empty()) {
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
      if (frame.valid()) {
        shell->session()->setBoardAspect(frame.width / frame.height);
        if (applyStage(*shell->session(), request, frame,
                       gHudFraming >= 0 ? gHudFraming == 1 : shell->settings().hudFraming,
                       labGame(*shell, false)))
          frameBoard(*shell);
      }
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
      }
      // Refresh the alignment offsets and this frame's legend hover before building.
      renderer->setOptions(optionsFor(*shell, (*ui)->seamHover()));
      if (gLatchT >= 0.0f)
        applyLatch(*renderer, app::FxTimeline::LatchFx{gLatchAxis, gLatchT});
      syncMarks(*renderer, *shell->session());
      instances = renderer->buildInstances(
          shell->session()->snapshot(), shell->session()->viewConfig(),
          &shell->session()->seams(), &shell->session()->animation(),
          [&](CellId c) { return shell->session()->boardVisible(c); },
          [&](CellId c) { return shell->session()->game().cellInPresent(c); },
          shell->session()->timelineLinks());
      render::BoardFxState fx = captureFx(*renderer, *shell);
      if (hold) applyHold(fx, *shell->session(), **ui, rig);
      render::applyBoardFx(instances, *shell->session(), fx);
    }
    view::OrbitCamera camera =
        shell->showsBoard() || hold
            ? boardCamera(*shell,
                          playSeq != nullptr ? *playSeq : render::kNoShapeSequence)
            : view::OrbitCamera{};
    const bool room = labBackdrop(*shell, hold);
    // A game in the lab (M24.5): the lab is placed around the board, not the reverse.
    const bool inLab = !room && rig.layout != nullptr;
    auto labOptions = renderer->options();
    labOptions.labMode = room || inLab;
    renderer->setOptions(labOptions);
    std::array<float, 2> projected{};
    std::optional<render::GpuLightBlock> gameLights;
    if (inLab) {
      gameLights =
          drawLabGame(rig, *shell, **ui, *renderer, instances, camera, intensity);
      camera = camera.withNearClip(labOptions.nearClip);
      if (vr.physicalCode > 0.0f)
        (void)render::appendNeuralCode(instances, *rig.layout, rig.poses, vr.physicalCode,
                                       static_cast<float>(gRigNow), renderer->theme());
    }
    // M25.5: the handoff's room (the stand-in slab, no board of the game), or the
    // implant's space and picture (the boards as light).
    if (!room && vr.scene == VrScene::Room) {
      instances = render::InstanceSet{};
      labOptions.labMode = true;
      renderer->setOptions(labOptions);
      const float aspect = frame.valid() ? frame.width / frame.height
                                         : static_cast<float>(target->width()) /
                                               static_cast<float>(target->height());
      gameLights =
          drawVrRoom(vr, *shell, **ui, *renderer, instances, camera, aspect, intensity);
      camera = camera.withNearClip(labOptions.nearClip);
    } else if (!room && (vr.scene == VrScene::Space || vr.scene == VrScene::Picture)) {
      drawVrSpace(vr, *shell, **ui, *renderer, instances, intensity);
    }
    applyVrLids(*renderer, vr, wake);
    if (room) {
#ifdef CB_HAVE_IMGUI
      gLabScene->appendTo(
          instances, renderer->theme(),
          [&](std::string_view focus) { return (*ui)->salience(focus); },
          (*ui)->rackSlides());
#else
      gLabScene->appendTo(instances, renderer->theme());
#endif
#ifdef CB_HAVE_IMGUI
      projected = appendProjector(**ui, shell->settings(), instances, renderer->theme());
#endif
      camera = *gLabScene->station(stationFor(shell->screen()));
#ifdef CB_HAVE_IMGUI
      if (const auto moving = (*ui)->labCamera()) camera = *moving;
      if (!(*ui)->stationPin().empty()) camera = *gLabScene->station((*ui)->stationPin());
#endif
      if (!gStation.empty()) camera = *gLabScene->station(gStation);
      frame = {};
      renderer->setBlur(0);
    }
#ifdef CB_HAVE_IMGUI
    {
      const auto living = stepLiving(
          **ui, *shell, camera, true, visualOrigin + sample.seconds, dt, target->width(),
          target->height(), renderer->options().presentationIntensity);
      setImplantFrame(*renderer, **ui, *shell, implantActive,
                      visualOrigin + sample.seconds, target->height(), true, living,
                      vrWake(vr, wake));
    }
#endif
    renderer->setLights(room ? render::labLightBlock(
                                   gLabScene->doc(), *gLabScene,
                                   gRoomLightOverride >= 0 ? gRoomLightOverride
                                                           : shell->settings().roomLight,
                                   render::kLabKey, "projector", .5f + .5f * projected[0],
                                   renderer->theme(), .5f + .5f * projected[1])
                        : gameLights ? *gameLights
                                     : render::neutralLightBlock());
    const std::function<void(VkCommandBuffer)> drawUi = [&](VkCommandBuffer cmd) {
      (*ui)->record(cmd);
    };
    // The board screen's ambient field is submitted before the board, inside its own
    // pass, so the board is drawn over it rather than under it (M18 correction). Every
    // other screen leaves the field list empty and this is a no-op.
    const std::function<void(VkCommandBuffer)> drawBackground = [&](VkCommandBuffer cmd) {
      (*ui)->recordBackground(cmd, renderer->hdrScene());
    };
    applyPost(*renderer, shell->settings());
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
      auto img =
          renderFrame(0.0f, {static_cast<double>(written) * static_cast<double>(step)});
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
    return renderFrame(0.0f, {globalSeconds}, seqArg);
  };

  // Two thrown-away frames, in this order: the first lets a screen-change reset the pane
  // transition (which it does after the tick), the second advances that transition to
  // done - without both, the library draws at alpha zero. Then ImGui has a previous frame
  // to size from as well.
  if (!renderFrame(0.0f).has_value()) return 1;
  if (!renderFrame(1.0f).has_value()) return 1;
  // Lab menus centre a measured pane containing nested scrolling children. Their
  // scrollbars consume the previous child's content size, one frame after the pane
  // moves to its measured centre. Settle that last layout dependency as well.
  if (labBackdrop(*shell, holdsBoard(*shell, **ui)) && !renderFrame(0.0f).has_value())
    return 1;

  // Pointer clicks, one per ';': move, press, release on three frames, so a button sees a
  // real click and the capture shows what the program does with it.
  {
    std::size_t at = 0;
    while (at < gUiClicks.size()) {
      const std::size_t end = std::min(gUiClicks.find(';', at), gUiClicks.size());
      const std::string one = gUiClicks.substr(at, end - at);
      float x = 0.0f, y = 0.0f, x2 = 0.0f, y2 = 0.0f;
      int button = 0;
      if (std::sscanf(one.c_str(), "drag:%f,%f,%f,%f,%d", &x, &y, &x2, &y2, &button) ==
          5) {
        // A drag: press at the start, move in four steps, release at the end.
        (*ui)->injectMouse(x, y, false, button);
        if (!renderFrame(0.0f).has_value()) return 1;
        (*ui)->injectMouse(x, y, true, button);
        if (!renderFrame(0.0f).has_value()) return 1;
        for (int i = 1; i <= 4; ++i) {
          const float f = static_cast<float>(i) / 4.0f;
          (*ui)->injectMouse(x + (x2 - x) * f, y + (y2 - y) * f, true, button);
          if (!renderFrame(0.0f).has_value()) return 1;
        }
        (*ui)->injectMouse(x2, y2, false, button);
        if (!renderFrame(0.0f).has_value()) return 1;
      } else if (one == "key:delete" || one == "key:escape" || one == "key:enter" ||
                 one == "key:f12") {
        const int key = one == "key:delete"   ? ImGuiKey_Delete
                        : one == "key:f12"    ? ImGuiKey_F12
                        : one == "key:escape" ? ImGuiKey_Escape
                                              : ImGuiKey_Enter;
        for (const bool down : {true, false}) {
          (*ui)->injectKey(key, down);
          if (!renderFrame(0.0f).has_value()) return 1;
        }
      } else if (std::sscanf(one.c_str(), "%f,%f,%d", &x, &y, &button) >= 2) {
        for (const bool down : {false, true, false}) {
          (*ui)->injectMouse(x, y, down, button);
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
      if (!gFrom.empty()) gTransitionT = t;
      // Pin the cycle outright: a clip is only deterministic if every frame states its
      // own `t` rather than integrating towards it. The library sweeps the overture;
      // every other screen sweeps the running move, so a board clip is a gameplay shot.
      if (gFrom.empty() && screen == "newgame") {
        shell->overtures().setProgress(t);
      } else if (shell->session() != nullptr) {
        shell->session()->setMoveProgress(t);
      }
      char name[32];
      std::snprintf(name, sizeof(name), "frame_%04d.ppm", i);
      const std::optional<render::Image> img =
          renderFrame(0.0f, {static_cast<double>(i) / std::max(1, fps)});
      if (!img.has_value() ||
          !writeFrame(*img, (std::filesystem::path(path) / name).string())) {
        return 1;
      }
    }
  }
#else
  renderer->setVisualTime(visualOrigin);
  const auto instances = renderer->buildInstances(
      shell->session()->snapshot(), shell->session()->viewConfig(),
      &shell->session()->seams(), &shell->session()->animation(),
      [&](CellId c) { return shell->session()->boardVisible(c); });
  applyPost(*renderer, shell->settings());
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
  double visualTime = 0;
  bool visualTimeGiven = false;
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
          "designer, body, flat, info, pieces, quit, pausequit, or the board by "
          "default.\n"
          "--t 0..1 is where the library screen's overture is in its cycle: 0 the flat\n"
          "board every variant starts from, 1 the shape it becomes.\n"
          "--clip DIR renders a sequence of numbered PPM frames instead of one shot,\n"
          "sweeping t from --t0 to --t1 over --frames frames (default 0..1, 96).\n"
          "--visual-time SECONDS sets capture presentation time (finite, nonnegative; "
          "default 0).\n"
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
          "--dev forces the developer overlay on for this run (not saved).\n"
          "--post 0|1 forces post-processing off or on and --bloom 0..4 scales its "
          "bloom,\n"
          "for this run only (not saved).\n"
          "--theme console|manifold overrides the theme for this run; --matte disables "
          "materials.\n"
          "--seam-hover AXIS pins that drawn axis's seam-legend highlight for this run.\n"
          "--adjacent draws the board's neighbours through its glued edges (M22.2);\n"
          "--adjacent-rings 1..3 how many rings, --adjacent-amount 0..1 the reveal.\n"
          "--ui-scale 0.6..3.0 is exact; --ui-auto opts captures into automatic "
          "scaling.\n"
          "--presentation 0..1 sets the presentation intensity for this run (not "
          "saved).\n"
          "--from SCREEN --transition-t SECONDS pins the screen move from SCREEN (menu, "
          "game, "
          "pause, newgame, watch, welcome, settings, quit, info, pieces, editor) to "
          "--screen at "
          "that age.\n"
          "--room-light 0..2 scales the room practicals for this run only.\n"
          "--projector 0|1 toggles the bench hologram; --overture-t 0..1 pins Library "
          "progress (alias --t).\n"
          "--lab 0|1 toggles the static lab room; --station <name> requires --lab 1.\n"
          "--skin 0|1 the lab game's slab skin on a shape (default 1); --latch-t "
          "SECONDS\n"
          "pins a seam latch flash's age on --latch-axis 0|1 (default 1, the ranks).\n"
          "--deploy-t / --stow-t SECONDS pin a lab game's slab deploy (menu to game) or\n"
          "stow (live game to menu), with --lab 1; --focus 0..1 pins the room focus.\n"
          "--relayout-t SECONDS pins the layout change a --script makes (axes ...) in a\n"
          "lab game: the gantry carrying the slabs (M24.6), with --lab 1.\n"
          "--vr 0|1 keeps a lab game on the rig or starts it in the implant's space\n"
          "(M25.5; default: more slabs than the rack's 8 bays go to VR); --vr-t SECONDS\n"
          "pins the entering handoff (a VR deploy); --stow-t pins the leaving one.\n"
          "--implant 0|1 toggles the neural lab interface; --wake 0|1 and --wake-t "
          "SECONDS "
          "(--handshake-t SECONDS) play or pin the launch waking-up sequence; --panel-t "
          "SECONDS pins panel "
          "age.\n"
          "--heartbeat 0|1 toggles the implant double pulse (captures default off).\n"
          "--living 0|1 toggles living vision (lens warp, gaze, focus; M24.4c);\n"
          "--tremor 0|1 and --gaze-wander 0|1 (captures default off); --gaze X,Y pins "
          "the\n"
          "gaze (fractions); --gaze-ui 0|1 pins menu/room focus; --gaze-target KEY pins "
          "a\n"
          "fixation; --warp K overrides the lens warp.\n"
          "--daydream-t S pins the eye S idle seconds into its daydream (blurred "
          "viewport,\n"
          "sharper focus spot); --daydream-wake S then moves the mouse and steps S "
          "more.\n"
          "--hud-framing 0|1 toggles HUD-safe camera framing for this run.\n"
          "--capture-t SECONDS pins the newest tray icon scan (0..0.35).\n"
          "--event-t SECONDS pins the HUD turn flip and newest ledger row; --think-t "
          "SECONDS shows the engine thinking dots.\n"
          "--ui-click \"x,y;x,y,button;drag:x1,y1,x2,y2,button\" replays pointer clicks "
          "and\n"
          "drags (pixels; button 0 left, 1 right, 2 middle) before the capture.\n"
          "--variants-dir DIR reads and saves the player's own variants there.\n"
          "--piece NAME opens the designer captures on that piece.\n"
          "--result shows a decided result panel; --end-t pins its event age.\n"
          "--review PLY browses the scripted game to that ply (0 = the start).\n"
          "--dims 2..4 is how many dimensions the designer's move preview shows.\n");
      return 0;
    }
    if (arg == "--shot" && i + 1 < argc)
      shotPath = argv[++i];
    else if (arg == "--clip" && i + 1 < argc)
      clipDir = argv[++i];
    else if (arg == "--result")
      gResult = true;
    else if (arg == "--review" && i + 1 < argc)
      gReview = argv[++i];
    else if (arg == "--script" && i + 1 < argc)
      script = argv[++i];
    else if (arg == "--screen" && i + 1 < argc)
      screen = argv[++i];
    else if (arg == "--from" && i + 1 < argc)
      gFrom = argv[++i];
    else if (arg == "--transition-t" && i + 1 < argc)
      gTransitionT = std::strtof(argv[++i], nullptr);
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
    else if (arg == "--theme" && i + 1 < argc)
      gThemeOverride = argv[++i];
    else if (arg == "--seam-hover" && i + 1 < argc)
      gSeamHover = argv[++i];
    else if (arg == "--visual-time") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--visual-time requires finite nonnegative seconds\n");
        return 1;
      }
      char* end = nullptr;
      const char* value = argv[++i];
      visualTime = std::strtod(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(visualTime) || visualTime < 0) {
        std::fprintf(stderr, "--visual-time requires finite nonnegative seconds\n");
        return 1;
      }
      visualTimeGiven = true;
    } else if (arg == "--dev")
      gDevOverlay = true;
    else if (arg == "--post" && i + 1 < argc) {
      const std::string v = argv[++i];
      if (v != "0" && v != "1") {
        std::fprintf(stderr, "--post requires 0 or 1\n");
        return 1;
      }
      gPostOverride = v == "1" ? 1 : 0;
    } else if (arg == "--bloom" && i + 1 < argc) {
      char* end = nullptr;
      gBloomOverride = std::strtof(argv[++i], &end);
      if (*end != '\0' || !std::isfinite(gBloomOverride) || gBloomOverride < 0.0f ||
          gBloomOverride > 4.0f) {
        std::fprintf(stderr, "--bloom requires a scale from 0 to 4\n");
        return 1;
      }
    } else if (arg == "--vr") {
      const std::string value = i + 1 < argc ? argv[++i] : "";
      if (value != "0" && value != "1") {
        std::fprintf(stderr, "--vr requires 0 or 1\n");
        return 1;
      }
#ifdef CB_HAVE_IMGUI
      gVr = value == "1" ? 1 : 0;
#endif
    } else if (arg == "--vr-t") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[++i] : "";
      const float v = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(v) || v < 0) {
        std::fprintf(stderr, "--vr-t requires finite seconds\n");
        return 1;
      }
#ifdef CB_HAVE_IMGUI
      gVrT = v;
#endif
    } else if (arg == "--adjacent") {
      gAdjacent = true;
    } else if (arg == "--adjacent-rings") {
      const std::string value = i + 1 < argc ? argv[++i] : "";
      if (value != "1" && value != "2" && value != "3") {
        std::fprintf(stderr, "--adjacent-rings requires 1, 2 or 3\n");
        return 1;
      }
      gAdjacentRings = value[0] - '0';
    } else if (arg == "--adjacent-amount") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[++i] : "";
      const float v = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(v) || v < 0 || v > 1) {
        std::fprintf(stderr, "--adjacent-amount requires 0..1\n");
        return 1;
      }
      gAdjacentAmount = v;
    } else if (arg == "--skin" || arg == "--latch-axis") {
      const std::string value = i + 1 < argc ? argv[++i] : "";
      if (value != "0" && value != "1") {
        std::fprintf(stderr, "%s requires 0 or 1\n", arg.c_str());
        return 1;
      }
      (arg == "--skin" ? gSkin : gLatchAxis) = value == "1" ? 1 : 0;
    } else if (arg == "--latch-t") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[++i] : "";
      const float v = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(v) || v < 0) {
        std::fprintf(stderr, "--latch-t requires finite seconds\n");
        return 1;
      }
      gLatchT = v;
    } else if (arg == "--relayout-t") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[++i] : "";
      const float v = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(v) || v < 0) {
        std::fprintf(stderr, "--relayout-t requires finite seconds\n");
        return 1;
      }
      gRelayoutT = v;
    } else if (arg == "--deploy-t" || arg == "--stow-t" || arg == "--focus") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[++i] : "";
      const float v = std::strtof(value, &end);
      const bool focus = arg == "--focus";
      if (end == value || *end != '\0' || !std::isfinite(v) || v < 0 ||
          (focus && v > 1)) {
        std::fprintf(stderr, "%s requires %s\n", arg.c_str(),
                     focus ? "a level from 0 to 1" : "finite seconds");
        return 1;
      }
      (arg == "--deploy-t" ? gDeployT : arg == "--stow-t" ? gStowT : gFocus) = v;
    } else if (arg == "--room-light") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--room-light requires a scale from 0 to 2\n");
        return 1;
      }
      char* end = nullptr;
      const char* value = argv[++i];
      gRoomLightOverride = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(gRoomLightOverride) ||
          gRoomLightOverride < 0 || gRoomLightOverride > 2) {
        std::fprintf(stderr, "--room-light requires a scale from 0 to 2\n");
        return 1;
      }
    } else if (arg == "--boot-t" && i + 1 < argc) {
      char* end = nullptr;
      gBootT = std::strtof(argv[++i], &end);
      if (*end != '\0' || !std::isfinite(gBootT) || gBootT < 0.0f) {
        std::fprintf(stderr, "--boot-t requires seconds >= 0\n");
        return 1;
      }
    } else if ((arg == "--check-t" || arg == "--end-t" || arg == "--capture-t" ||
                arg == "--event-t" || arg == "--think-t") &&
               i + 1 < argc) {
      char* end = nullptr;
      float& target = arg == "--check-t"   ? gCheckT
                      : arg == "--end-t"   ? gEndT
                      : arg == "--event-t" ? gEventT
                      : arg == "--think-t" ? gThinkT
                                           : gCaptureT;
      target = std::strtof(argv[++i], &end);
      if (*end != '\0' || !std::isfinite(target) || target < 0.0f) {
        std::fprintf(stderr, "%s requires seconds >= 0\n", arg.c_str());
        return 1;
      }
    } else if (arg == "--matte")
      gMatteCapture = true;
    else if (arg == "--presentation" && i + 1 < argc) {
      gIntensity = std::clamp(std::strtof(argv[++i], nullptr), 0.0f, 1.0f);
    } else if (arg == "--hud-framing" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "0" && value != "1") {
        std::fprintf(stderr, "--hud-framing requires 0 or 1\n");
        return 1;
      }
      gHudFraming = value == "1" ? 1 : 0;
    } else if (arg == "--lab") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--lab requires 0 or 1\n");
        return 1;
      }
      const std::string value = argv[++i];
      if (value != "0" && value != "1") {
        std::fprintf(stderr, "--lab requires 0 or 1\n");
        return 1;
      }
      gLab = value == "1" ? 1 : 0;
    } else if (arg == "--projector") {
      if (i + 1 >= argc || (std::string_view(argv[i + 1]) != "0" &&
                            std::string_view(argv[i + 1]) != "1")) {
        std::fprintf(stderr, "--projector requires 0 or 1\n");
        return 1;
      }
      gProjector = std::string_view(argv[++i]) == "1" ? 1 : 0;
    } else if (arg == "--overture-t") {
      char* end = nullptr;
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--overture-t requires a finite progress in 0..1\n");
        return 1;
      }
      const char* value = argv[++i];
      const float progress = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(progress) || progress < 0 ||
          progress > 1) {
        std::fprintf(stderr, "--overture-t requires a finite progress in 0..1\n");
        return 1;
      }
      overtureT = progress;
      overtureTGiven = true;
    } else if (arg == "--station") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--station requires a name\n");
        return 1;
      }
      gStation = argv[++i];
    } else if (arg == "--implant" || arg == "--heartbeat") {
      if (i + 1 >= argc || (std::string_view(argv[i + 1]) != "0" &&
                            std::string_view(argv[i + 1]) != "1")) {
        std::fprintf(stderr, "%s requires 0 or 1\n", arg.c_str());
        return 1;
      }
      const int value = std::string_view(argv[++i]) == "1" ? 1 : 0;
      (arg == "--implant" ? gImplant : gHeartbeat) = value;
    } else if (arg == "--living" || arg == "--tremor" || arg == "--gaze-wander" ||
               arg == "--gaze-ui") {
      if (i + 1 >= argc || (std::string_view(argv[i + 1]) != "0" &&
                            std::string_view(argv[i + 1]) != "1")) {
        std::fprintf(stderr, "%s requires 0 or 1\n", arg.c_str());
        return 1;
      }
      const int value = std::string_view(argv[++i]) == "1" ? 1 : 0;
      (arg == "--living"    ? gLiving
       : arg == "--tremor"  ? gTremor
       : arg == "--gaze-ui" ? gCliGazeUi
                            : gWander) = value;
    } else if (arg == "--daydream-t" || arg == "--daydream-wake") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires seconds\n", arg.c_str());
        return 1;
      }
      (arg == "--daydream-t" ? gDaydreamT : gDaydreamWake) =
          std::strtof(argv[++i], nullptr);
    } else if (arg == "--gaze-target") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--gaze-target requires a key\n");
        return 1;
      }
      gCliGazeTarget = argv[++i];
    } else if (arg == "--gaze") {
      float x = 0, y = 0;
      if (i + 1 >= argc || std::sscanf(argv[i + 1], "%f,%f", &x, &y) != 2 ||
          !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) {
        std::fprintf(stderr, "--gaze requires <x>,<y> as fractions of the viewport\n");
        return 1;
      }
      ++i;
      gCliGazePin = view::Vec2f{x, y};
    } else if (arg == "--warp") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[i + 1] : "";
      const float k = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(k)) {
        std::fprintf(stderr, "--warp requires a finite number\n");
        return 1;
      }
      ++i;
      gCliWarp = k;
    } else if (arg == "--wake") {
      if (i + 1 >= argc || (std::string_view(argv[i + 1]) != "0" &&
                            std::string_view(argv[i + 1]) != "1")) {
        std::fprintf(stderr, "--wake requires 0 or 1\n");
        return 1;
      }
      gWake = std::string_view(argv[++i]) == "1" ? 1 : 0;
    } else if (arg == "--wake-t" || arg == "--handshake-t") {
      char* end = nullptr;
      const char* value = i + 1 < argc ? argv[i + 1] : "";
      const float age = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(age) || age < 0) {
        std::fprintf(stderr, "%s requires finite seconds\n", arg.c_str());
        return 1;
      }
      ++i;
      (arg == "--wake-t" ? gWakeT : gHandshakeT) = age;
    } else if (arg == "--panel-t") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "--panel-t requires finite seconds\n");
        return 1;
      }
      char* end = nullptr;
      const char* value = argv[++i];
      const float age = std::strtof(value, &end);
      if (end == value || *end != '\0' || !std::isfinite(age)) {
        std::fprintf(stderr, "--panel-t requires finite seconds\n");
        return 1;
      }
      gPanelT = age;
    } else if (arg == "--ui-auto")
      gUiAuto = true;
    else if (arg == "--ui-scale" && i + 1 < argc)
      gUiScaleOverride = std::strtof(argv[++i], nullptr);
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
    else if (arg == "--variants-dir" && i + 1 < argc)
      gUserVariantDir = argv[++i];
    else if (arg == "--dims" && i + 1 < argc)
      previewDims = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (!arg.starts_with("-"))
      variantName = arg;
  }
  if (!gStation.empty() && gLab != 1) {
    std::fprintf(stderr, "--station requires --lab 1\n");
    return 1;
  }
  // M24.5: a lab game's deploy (menu to game) or stow (a live game to the menu), pinned
  // the way --from/--transition-t pins any screen move.
  if (gRelayoutT >= 0 && gLab != 1) {
    std::fprintf(stderr, "--relayout-t requires --lab 1\n");
    return 1;
  }
#ifdef CB_HAVE_IMGUI
  // M25.5: --vr-t pins the entering handoff - a VR game's deploy - at that age.
  if (gVrT >= 0) {
    if (gDeployT >= 0 || gStowT >= 0) {
      std::fprintf(stderr, "--vr-t is exclusive with --deploy-t and --stow-t\n");
      return 1;
    }
    gDeployT = gVrT;
    if (gVr < 0) gVr = 1;
  }
  if ((gVr >= 0 || gVrT >= 0) && gLab != 1) {
    std::fprintf(stderr, "--vr and --vr-t require --lab 1\n");
    return 1;
  }
#endif
  if ((gDeployT >= 0 || gStowT >= 0) && gLab != 1) {
    std::fprintf(stderr, "--deploy-t and --stow-t require --lab 1\n");
    return 1;
  }
  if (gDeployT >= 0 && gStowT >= 0) {
    std::fprintf(stderr, "--deploy-t and --stow-t are exclusive\n");
    return 1;
  }
  if (gDeployT >= 0) {
    screen = "game";
    gFrom = "menu";
    gTransitionT = gDeployT;
  } else if (gStowT >= 0) {
    screen = "menu";
    gFrom = "game";
    gTransitionT = gStowT;
  }
  // A trailer render is footage: the board and the grade, no interface.
  if (!gTrailerClipDir.empty()) cinema = true;
  const std::string& targetPath = !clipDir.empty() ? clipDir : shotPath;
  // The player's own variants are part of the game when it is being played, and of a
  // capture only when asked for (`--user-settings`, or a directory named outright).
  if (gUserVariantDir.empty() &&
      (gUseUserSettings ||
       (targetPath.empty() && benchFrames == 0 && gTrailerClipDir.empty()))) {
    gUserVariantDir = app::Settings::defaultPath().parent_path() / "variants";
  }
  // The shared movesets: the player's own first, then the ones that ship.
  if (!gUserVariantDir.empty()) {
    auto dirs = movesetDirs();
    dirs.insert(dirs.begin(), gUserVariantDir.parent_path() / "movesets");
    setMovesetDirs(std::move(dirs));
  }
  if (!targetPath.empty() || benchFrames > 0 || !gTrailerClipDir.empty()) {
    gLabCapture = true;
    return captureFrame(variantName.empty() ? "standard" : variantName, targetPath,
                        script, screen, overtureT, previewDims, !clipDir.empty(),
                        clipFrames, clipT0, clipT1, cinema, followMode, shapeFollow,
                        moveT, benchFrames, geometry || formedGiven, evert, formed,
                        flatCapture, slideU, slideV, ghost, playFile, dwell, captureFps,
                        playAt, framesGiven, width, height, watchName, pieceCylinders,
                        overtureTGiven, visualTime);
  }

  if (visualTimeGiven) {
    std::fprintf(stderr, "--visual-time is a capture-only option\n");
    return 1;
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

  if (!pinSeamHover(*shell)) return 1;
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
  if (!loadLab(*renderer)) return 1;
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(window->context(), window->handle(), renderer->theme(),
                               window->colorFormat());
  if (!ui.has_value()) {
    std::fprintf(stderr, "cannot create the interface: %s\n",
                 ui.error().format().c_str());
    return 1;
  }
  if (gSkin >= 0) (*ui)->setSkin(gSkin == 1);
  (*ui)->forceDevOverlay(gDevOverlay);
  bool lastLabTheme = false;
  (void)(*ui)->setScale(
      resolveUiScale(shell->settings(), static_cast<int>(window->height()), false));
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  {
    const view::Theme theme = resolveTheme(shell->settings());
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
  // Where the right button went down, and whether it has since dragged: a click that did
  // not drag cancels the selected move instead of orbiting.
  float rightDownX = 0.0f, rightDownY = 0.0f;
  bool rightDragged = false;
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
  double visualSeconds = 0;
  render::PanelResponse panelResponse;
  std::vector<render::PanelTarget> panelTargets;
  // Stamps the session's presentation events (boot, ...) with this loop's visual clock.
  app::FxTimeline fxTimeline;
  // The live morph's last value, for the seam latches (M24.5b); negative: none yet.
  float latchFormed = -1.0f;
  std::uint64_t latchSession = 0;
#ifdef CB_HAVE_IMGUI
  (*ui)->setRigReport([&] { return rigReport(*shell, **ui); });
  (*ui)->setSkinReport([&] { return skinReport(*shell); });
  (*ui)->setVrReport([&] { return vrReport(*shell); });
#endif
  // Hover aids (M20): events only mark the pointer dirty; the cell is resolved once per
  // frame, because the shape view's pick builds a whole surface.
  bool hoverDirty = false;
  bool hoverOver = false;  // the pointer is over the board (not the interface / window)
  float hoverX = 0.0f, hoverY = 0.0f;
  // The cell under a pixel of the window, on the flat/3-D board or the shape.
  const auto hoverPick = [&](float x, float y) -> app::Session::Pick {
    const app::Session* session = shell->session();
    if (session == nullptr || x < boardRect.x || y < boardRect.y ||
        x >= boardRect.x + boardRect.width || y >= boardRect.y + boardRect.height) {
      return {};
    }
#ifdef CB_HAVE_IMGUI
    if (optionsFor(*shell).surface) {
      if (session->boardLocked()) return {};
      const render::PlaySurface surf = render::PlaySurface::build(
          session->variant(), poseFrom(shell->settings(), session->variant()));
      return {surf.pick(
          session->cameraOver(surfacePlacements(surf), surf.bounds(), view::ViewConfig{}),
          boardRect.width, boardRect.height, x - boardRect.x, y - boardRect.y,
          shell->settings().geometryGhost, &session->snapshot(),
          shell->settings().pieceHeightScale, shell->settings().shapePieceCylinders)};
    }
#endif
    return session->pick(x - boardRect.x, y - boardRect.y, boardRect.width,
                         boardRect.height);
  };

  // M22.4: the tiled view's switch as last seen. A switch saved on is already arrived at
  // launch (the reveal starts at 1), so nothing replays.
  bool adjacentWasOn = shell->settings().showAdjacent;
  while (running && !shell->quitRequested()) {
#ifdef CB_HAVE_IMGUI
    if (shell->session() != nullptr && !gVrTrack.flat.active(gRigNow, gVrIntensity))
      gVrTrack.beforeFlat = shell->session()->playerCamera();
#endif
    // M22.5: picking sees the tiled view the renderer is drawing - the same predicate
    // and the same reveal - so a click lands on what is under the pointer.
    if (shell->session() != nullptr) {
      const app::Settings& st = shell->settings();
      shell->session()->setAdjacent(
          {view::adjacentDrawn(st.showAdjacent, optionsFor(*shell).surface),
           st.adjacentRings, st.adjacentFormed, st.adjacentPick});
    }
    SDL_Event e;
    // A board game has nothing to animate most of the time, so the loop waits for
    // input rather than spinning. While a piece is actually moving it wants frames, so
    // the wait shortens to roughly one.
    // The trailer moves the camera and the shape on every frame, with or without a piece
    // in flight; waiting for events would draw it at the idle ten frames a second.
    const bool animating =
        resolveTheme(shell->settings()).liveControls ||
        resolveTheme(shell->settings()).materials.pieces == view::Material::Hologram ||
        (shell->hasGame() &&
         (shell->session()->animation().active() || shell->attracting())) ||
        // M22.4: the copies arriving or leaving want frames, not the idle wait.
        shell->settings().adjacentLeaving ||
        (shell->settings().showAdjacent && shell->settings().adjacentFormed < 1.0f);
    if (!SDL_WaitEventTimeout(&e, animating ? 8 : 100)) e.type = SDL_EVENT_POLL_SENTINEL;
    do {
      if (e.type == SDL_EVENT_POLL_SENTINEL) continue;
      // Waking up (M24.7): any key or click ends the sequence. The menu is not usable
      // until the handshake has converged, so that input is swallowed rather than acted
      // on.
      if (gWakeRun.active) {
        const bool press =
            e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (press) {
          // Already past the convergence: the menu is up, so there is nothing to fade in.
          gWakeRun.skippedAt = gWakeRun.blocking ? visualSeconds : -1;
          gWakeRun.finished = !gWakeRun.blocking;
          gWakeRun.active = false;
          if (gWakeRun.blocking) continue;
        } else if (gWakeRun.blocking &&
                   (e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                    e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_TEXT_INPUT)) {
          continue;
        }
      }
#ifdef CB_HAVE_IMGUI
      if (vrInputLocked(*shell) &&
          (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP ||
           e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
           e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_WHEEL ||
           e.type == SDL_EVENT_TEXT_INPUT)) {
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          // Match the rig: the first press completes the move and is consumed.
          gVrTrack.flat.skip(gRigNow, gVrIntensity);
          gVrTrack.crossAt = gVrTrack.moveAt = -1e9;
          ImGui::GetIO().ClearInputKeys();
          for (int button = 0; button < 5; ++button)
            ImGui::GetIO().AddMouseButtonEvent(button, false);
        }
        // A button which initiated the transition must not remain held in ImGui.
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
          for (int button = 0; button < 5; ++button)
            ImGui::GetIO().AddMouseButtonEvent(button, false);
        continue;
      }
      const bool consumed = (*ui)->processEvent(e);
#else
      const bool consumed = false;
#endif
      const app::Settings& settings = shell->settings();
      const bool inGame = shell->screen() == app::Screen::Game && shell->hasGame();

      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        hoverDirty = true;
        hoverOver = !consumed;
        hoverX = e.motion.x;
        hoverY = e.motion.y;
      } else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        hoverDirty = true;
        hoverOver = false;
      } else if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
                 (e.key.key == SDLK_LCTRL || e.key.key == SDLK_RCTRL)) {
        hoverDirty = true;
      }

      // The first click or key completes the board's boot at once and is then handled
      // normally (M23.10 section 3).
      if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        gGazeInput = true;
        // A running deploy has no input but skip (M24.5): the slabs land and the boot
        // starts now, and the key or click goes no further.
        const auto rigMove = (*ui)->rigMove();
        if (rigMove && !rigMove->done && rigMove->move == app::ScreenMove::Deploy) {
          (*ui)->skipTransition();
          fxTimeline.holdBootUntil(visualSeconds);
          continue;
        }
        // A relayout has no input either (M24.6): skip lands every slab now.
        if (labGame(*shell, false) &&
            relayoutRunning(renderer->options().presentationIntensity)) {
          gRigTrack.start = -1e9;
          continue;
        }
        fxTimeline.skipBoot();
        (*ui)->skipTransition();
      }

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
            rightDownX = e.button.x;
            rightDownY = e.button.y;
            rightDragged = false;
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
          if (e.button.button == SDL_BUTTON_RIGHT) {
            // A right-click that did not turn the camera cancels the selected move.
            if (orbiting && !rightDragged && !consumed && shell->hasGame() &&
                shell->screen() == app::Screen::Game) {
              (void)shell->session()->cancelSelection();
            }
            orbiting = false;
          }
          if (e.button.button == SDL_BUTTON_MIDDLE) {
            panning = false;
            sliding = false;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION:
          if (orbiting &&
              std::hypot(e.motion.x - rightDownX, e.motion.y - rightDownY) > 4.0f) {
            rightDragged = true;
          }
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
              if (inGame && !shell->attracting() && shell->session()->reviewPly()) {
                a.kind = app::ActionKind::ReviewStep;
                a.dx = e.key.key == SDLK_RIGHT ? 1.0f : -1.0f;
                (void)shell->session()->apply(a);
                break;
              }
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
            case SDLK_HOME:
            case SDLK_END:
              // Review's ends: the start position and the final one.
              if (inGame && !shell->attracting() && shell->session()->reviewPly()) {
                a.kind = app::ActionKind::ReviewJump;
                a.text = e.key.key == SDLK_HOME
                             ? "0"
                             : std::to_string(shell->session()->game().plyCount());
                (void)shell->session()->apply(a);
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
#ifdef CB_HAVE_IMGUI
    // The pixel the player sees at p shows layer content from lensWarpSource(p), so ImGui
    // is told the mouse is there (M24.4c 4.3). Tools are never warped, 3-D picking and
    // hologram dragging keep the real mouse, and the rects are last frame's.
    if (gWarpInput.active && SDL_GetMouseFocus() != nullptr) {
      float x = 0, y = 0;
      SDL_GetMouseState(&x, &y);
      auto& io = ImGui::GetIO();
      const view::Vec2f real{x * io.DisplayFramebufferScale.x,
                             y * io.DisplayFramebufferScale.y};
      bool tool = false;
      for (const auto& r : (*ui)->toolRects())
        tool = tool ||
               (real[0] >= r[0] && real[0] < r[2] && real[1] >= r[1] && real[1] < r[3]);
      if (!tool) {
        const auto src = view::lensWarpSource(real, gWarpInput.viewport, gWarpInput.k);
        io.AddMousePosEvent(src[0] / io.DisplayFramebufferScale.x,
                            src[1] / io.DisplayFramebufferScale.y);
      }
    }
#endif
    if (hoverDirty && shell->hasGame()) {
      hoverDirty = false;
      const bool inGameNow = shell->screen() == app::Screen::Game;
      const bool dragging = orbiting || panning || sliding;
      app::Session::Pick pick;
      if (inGameNow && hoverOver && !dragging && !shell->attracting() &&
          !vrInputLocked(*shell)) {
        pick = hoverPick(hoverX, hoverY);
      }
      shell->session()->setHover(pick.cell, (SDL_GetModState() & SDL_KMOD_CTRL) != 0,
                                 pick.stepU, pick.stepV);
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastFrame).count();
    const float dt = static_cast<float>(elapsed);
    visualSeconds += std::max(0.0, elapsed);
    gSkinOn = (*ui)->skin();
    renderer->setVisualTime(visualSeconds);
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
      // M22.4 the tiled view's arrival, eased the same way at the same pace: on, the
      // copies come in from infinity; off, they leave and are drawn until they are gone.
      // The camera does not move; the arrival is the motion.
      const view::AdjacentReveal adj = view::advanceAdjacentReveal(
          {st.adjacentFormed, st.adjacentLeaving, adjacentWasOn}, st.showAdjacent, dt,
          st.geometryFormSpeed);
      st.adjacentFormed = adj.formed;
      st.adjacentLeaving = adj.leaving;
      adjacentWasOn = adj.wasOn;
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
    // The computer opponent (M10.2-M10.3): it thinks on a worker, so this only starts a
    // search or collects an answer - the frame is never held up by it. It waits for the
    // board to finish powering on (M23.10).
    fxTimeline.observe(shell->hasGame() ? shell->session() : nullptr, visualSeconds,
                       shell->screen() == app::Screen::Game && !shell->attracting(),
                       shell->attracting());
    if (!shell->attracting() && !fxTimeline.booting(visualSeconds)) {
      (void)shell->advanceEngine(dt);
    }

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
    (*ui)->setVisualTime(visualSeconds);
    applyHudFx(
        **ui,
        shell->attracting() ? shell->trailerBootAge() : fxTimeline.bootAge(visualSeconds),
        [&]() -> std::optional<float> {
          if (const auto over = fxTimeline.end(visualSeconds)) return over->age;
          return std::nullopt;
        }());
    const auto resultEnd = fxTimeline.end(visualSeconds);
    (*ui)->setResultAge(resultEnd ? std::optional<float>(resultEnd->age) : std::nullopt);
    (*ui)->setCaptureAge(fxTimeline.captureAge(visualSeconds));
    (*ui)->setPresentationIntensity(renderer->options().presentationIntensity);
    {
      const auto ripple = fxTimeline.ripple(visualSeconds);
      (*ui)->setHudEvents({fxTimeline.turnAge(visualSeconds),
                           fxTimeline.moveAge(visualSeconds),
                           fxTimeline.thinkingAge(visualSeconds),
                           ripple ? std::optional<float>(ripple->age) : std::nullopt});
    }
    (*ui)->setRenderStatus(renderStatus, renderPid > 0);
    (*ui)->tick(dt);
    (*ui)->setLabDoc(labEnabled(*shell) ? &gLabScene->doc() : nullptr);
    (*ui)->setLabBackdrop(labBackdrop(*shell, holdsBoard(*shell, **ui)));
    const bool implantActive = configureImplantUi(**ui, *shell, renderer->theme(),
                                                  lastLabTheme, holdsBoard(*shell, **ui));
    const WakeFrame wake =
        stepWake(shell->settings(), false, visualSeconds,
                 renderer->options().presentationIntensity,
                 implantActive && postWanted(*renderer, shell->settings()));
    applyWake(*renderer, wake, window->height());
    (*ui)->setStationCamera([&](app::Screen which) -> std::optional<view::OrbitCamera> {
      if (!labEnabled(*shell)) return std::nullopt;
      return gLabScene->station(stationFor(which));
    });
    (*ui)->setFocusFor(focusFor);
    configureRigMoves(**ui, *shell);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, fps);
    (*ui)->endFrame();
    // M22.4: a tiled-view switch flipped by the interface or the console takes effect in
    // this frame's picture (a zero step: on starts at infinity, off starts leaving), so the
    // board never draws one frame of the stale reveal.
    {
      app::Settings& st = shell->settings();
      const view::AdjacentReveal adj = view::advanceAdjacentReveal(
          {st.adjacentFormed, st.adjacentLeaving, adjacentWasOn}, st.showAdjacent, 0.0f,
          st.geometryFormSpeed);
      st.adjacentFormed = adj.formed;
      st.adjacentLeaving = adj.leaving;
      adjacentWasOn = adj.wasOn;
    }
    // The board boots once the deploy's last slab has locked (M24.5).
    if (const auto m = (*ui)->rigMove();
        m && !m->done && m->move == app::ScreenMove::Deploy)
      fxTimeline.holdBootUntil(visualSeconds - static_cast<double>(m->age - m->lock));

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
      const view::Theme theme = resolveTheme(shell->settings());
      renderer->setTheme(theme);
      (*ui)->setTheme(theme);
      lastLabTheme = false;
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
#ifdef CB_HAVE_IMGUI
    // M25.3: the shape stays flat while any slab is off its clamp; a closing seam then
    // latches as it forms on the locked slab.
    gRigNow = visualSeconds;
    // M25.5: the neural VR decides which picture this is before anything is built.
    gVrIntensity = renderer->options().presentationIntensity;
    stepVrTrack(*shell, **ui);
    const VrFrame vr = vrFrame(*shell, **ui, holdsBoard(*shell, **ui),
                               renderer->options().presentationIntensity);
    const FormGate formGate(shell->settings(),
                            rigFormGate(*shell, **ui, holdsBoard(*shell, **ui),
                                        renderer->options().presentationIntensity));
    renderer->setOptions(optionsFor(*shell, (*ui)->seamHover()));
    // A seam latch (M24.5b): the console forces one; otherwise the live morph closing a
    // wrap axis upward between two frames stamps one. Opening never flashes.
    if (const auto axis = (*ui)->takeLatch()) fxTimeline.latch(*axis, visualSeconds);
    if (renderer->options().slabSkin && shell->session() != nullptr) {
      const float liveFormed = shell->settings().geometryFormed;
      const auto close = render::wrapCloseAt(shell->session()->variant());
      if (latchFormed >= 0.0f && latchSession == shell->session()->id())
        for (int a = 0; a < 2; ++a)
          if (render::latchCrossed(latchFormed, liveFormed,
                                   close[static_cast<std::size_t>(a)]))
            fxTimeline.latch(a, visualSeconds);
      latchFormed = liveFormed;
      latchSession = shell->session()->id();
    } else {
      latchFormed = -1.0f;
    }
    if (const auto latch = fxTimeline.latchAge(visualSeconds))
      applyLatch(*renderer, *latch);
#endif
    // Check after Ui::build and request processing: the console may replace a
    // position in the existing Session without producing a load request.
    const bool collectPanels = panelResponse.prepare(
        shell->showsBoard() && !shell->attracting() ? shell->session() : nullptr,
        renderer->theme(), renderer->options());
    const bool hold = holdsBoard(*shell, **ui);
    const float intensity = renderer->options().presentationIntensity;
    gRigNow = visualSeconds;
    if (shell->session() != nullptr) {
#ifdef CB_HAVE_IMGUI
      shell->session()->setLightBridges(
          (labGame(*shell, hold) || vr.scene == VrScene::Space) && (*ui)->bridges());
#else
      shell->session()->setLightBridges(labGame(*shell, hold) && (*ui)->bridges());
#endif
      // The console's `relayout` replays the last layout change from now.
      if ((*ui)->takeRelayout() && gRigTrack.move) {
        gRigTrack.start = visualSeconds;
        gRigTrack.fromCamera = gRigTrack.lastCamera;
      }
      // Lowered back from a stacked shape view: the slabs power up with a short boot.
      const float liveFormed = shell->settings().geometryFormed;
      if (labGame(*shell, hold) && stackedShapeView(*shell) &&
          gRigTrack.lastFormed > 0.0f && liveFormed <= 0.0f)
        gRigTrack.flatSince = visualSeconds;
      gRigTrack.lastFormed = liveFormed;
    }
    const RigFrame rig = rigFrame(*shell, **ui, hold, intensity);
    if ((shell->showsBoard() || hold) && gStation.empty()) {
      if (boardRect.valid()) {
        shell->session()->setBoardAspect(boardRect.width / boardRect.height);
#ifdef CB_HAVE_IMGUI
        if (applyStage(*shell->session(), request, boardRect,
                       gHudFraming >= 0 ? gHudFraming == 1 : shell->settings().hudFraming,
                       labGame(*shell, false)))
          frameBoard(*shell);
#endif
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
        // Framed on the live morph, not the rig's flat-gated one (M25.3).
        render::SurfacePose pose = poseFrom(st, shell->session()->variant());
        if (gFormedUngated >= 0.0f) pose.formed = gFormedUngated;
        render::easeMorphFraming(*shell->session(), optionsFor(*shell), pose,
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
          shell->session()->timelineLinks(), collectPanels ? &panelTargets : nullptr);
      if (collectPanels) panelResponse.apply(instances, panelTargets, visualSeconds);
      render::BoardFxState fx;
      fx.bootAge = shell->attracting() ? shell->trailerBootAge()
                                       : fxTimeline.bootAge(visualSeconds);
      fx.intensity = renderer->options().presentationIntensity;
      fx.ledCells = renderer->theme().materials.cells == view::Material::Led;
      if (const auto r = fxTimeline.ripple(visualSeconds)) {
        fx.rippleAge = r->age;
        fx.rippleOrigin = r->origin;
      }
      if (const auto over = fxTimeline.end(visualSeconds)) {
        fx.endAge = over->age;
        fx.end = over->fx;
      }
      if (const auto u = fxTimeline.powerUp(visualSeconds)) {
        fx.powerUpAge = u->age;
        fx.end = u->fx;
      }
      fx.reviewing = shell->session()->reviewPly().has_value();
      if (hold) applyHold(fx, *shell->session(), **ui, rig);
      applySlabBoot(fx, *shell, intensity);
      setFxTint(fx, *renderer);
      render::applyBoardFx(instances, *shell->session(), fx);
      camera = boardCamera(*shell, shapeSeq);
    }

    const bool room = labBackdrop(*shell, hold);
    // A game in the lab (M24.5): the lab is placed around the board, not the reverse.
    const bool inLab = !room && rig.layout != nullptr;
    auto labOptions = renderer->options();
    labOptions.labMode = room || inLab;
    renderer->setOptions(labOptions);
    std::array<float, 2> projected{};
    std::optional<render::GpuLightBlock> gameLights;
    if (inLab) {
      gameLights =
          drawLabGame(rig, *shell, **ui, *renderer, instances, camera, intensity);
      camera = camera.withNearClip(labOptions.nearClip);
#ifdef CB_HAVE_IMGUI
      if (vr.physicalCode > 0.0f)
        (void)render::appendNeuralCode(instances, *rig.layout, rig.poses, vr.physicalCode,
                                       static_cast<float>(gRigNow), renderer->theme());
#endif
    }
#ifdef CB_HAVE_IMGUI
    // M25.5: the handoff's room (the stand-in slab), or the implant's space and picture.
    if (!room && vr.scene == VrScene::Room) {
      instances = render::InstanceSet{};
      labOptions.labMode = true;
      renderer->setOptions(labOptions);
      const float aspect = boardRect.valid() ? boardRect.width / boardRect.height
                                             : static_cast<float>(window->width()) /
                                                   static_cast<float>(window->height());
      gameLights =
          drawVrRoom(vr, *shell, **ui, *renderer, instances, camera, aspect, intensity);
      camera = camera.withNearClip(labOptions.nearClip);
    } else if (!room && (vr.scene == VrScene::Space || vr.scene == VrScene::Picture)) {
      drawVrSpace(vr, *shell, **ui, *renderer, instances, intensity);
    }
    applyVrLids(*renderer, vr, wake);
#endif
    if (room) {
#ifdef CB_HAVE_IMGUI
      gLabScene->appendTo(
          instances, renderer->theme(),
          [&](std::string_view focus) { return (*ui)->salience(focus); },
          (*ui)->rackSlides());
#else
      gLabScene->appendTo(instances, renderer->theme());
#endif
#ifdef CB_HAVE_IMGUI
      projected = appendProjector(**ui, shell->settings(), instances, renderer->theme());
#endif
      camera = *gLabScene->station(stationFor(shell->screen()));
#ifdef CB_HAVE_IMGUI
      if (const auto moving = (*ui)->labCamera()) camera = *moving;
      if (!(*ui)->stationPin().empty()) camera = *gLabScene->station((*ui)->stationPin());
#endif
      if (!gStation.empty()) camera = *gLabScene->station(gStation);
      renderer->setBlur(0);
    }
    renderer->setLights(room ? render::labLightBlock(
                                   gLabScene->doc(), *gLabScene,
                                   gRoomLightOverride >= 0 ? gRoomLightOverride
                                                           : shell->settings().roomLight,
                                   render::kLabKey, "projector", .5f + .5f * projected[0],
                                   renderer->theme(), .5f + .5f * projected[1])
                        : gameLights ? *gameLights
                                     : render::neutralLightBlock());
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
      (*ui)->recordBackground(cmd, renderer->hdrScene());
#else
      (void)cmd;
#endif
    };

    // Acquire a swapchain image and record the whole frame straight into it (ADR-0018):
    // no offscreen image, no blit. Two frames may be in flight, so the previous frame can
    // still be on the GPU while this one is recorded. A frame that comes back invalid had
    // the swapchain rebuilt under it; skipping one frame is cheap and correct.
    applyPost(*renderer, shell->settings());
#ifdef CB_HAVE_IMGUI
    {
      const auto living =
          stepLiving(**ui, *shell, camera, false, visualSeconds, dt, window->width(),
                     window->height(), renderer->options().presentationIntensity);
      setImplantFrame(*renderer, **ui, *shell, implantActive, visualSeconds,
                      window->height(), false, living, vrWake(vr, wake));
    }
#endif
    auto frame = window->beginFrame();
    if (!frame.has_value()) {
      std::fprintf(stderr, "begin frame failed: %s\n", frame.error().format().c_str());
      break;
    }
    if (frame->valid()) {
      if (auto ok = renderer->record(frame->commandBuffer, *frame->target, instances,
                                     camera, overlay, background,
                                     room ? render::BoardRect{} : boardRect, frame->slot);
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
    (void)(*ui)->setScale(
        resolveUiScale(shell->settings(), static_cast<int>(window->height()), false));
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
