// SPDX-License-Identifier: GPL-3.0-or-later
//
// M13.9 revision: the trailer owns its camera. A calm default that never follows a piece,
// an edit mode that freezes the trailer so a camera can be authored, and stepping through
// the chain. Headless, driving a Shell and a clock.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "app/shell.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::filesystem::path tempSettings(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "endspielzero-tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::filesystem::remove(path);
  std::filesystem::remove(dir / "camera_track.txt");
  return path;
}

std::unique_ptr<Shell> makeShell(const char* name) {
  auto shell = Shell::create({"standard", "cylinder", "torus", "klein", "torus3d_8x8"},
                             tempSettings(name));
  shell->setVariantLoader([](const std::string& n) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(n));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}

void run(Shell& shell, float seconds) {
  constexpr float kStep = 1.0f / 60.0f;
  for (float t = 0.0f; t < seconds; t += kStep) {
    if (shell.session() != nullptr) shell.session()->advanceAnimation(kStep);
    shell.advanceAttract(kStep);
    if (shell.hasGame()) shell.advanceCameraTrack(kStep);
  }
}

}  // namespace

TEST_CASE("the trailer never follows a piece, even on a flat board with route set",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-follow.conf");
  shell->settings().cameraMode = "route";
  shell->settings().followStrength = 1.0f;
  shell->startAttract(defaultAttractSequence());
  REQUIRE(shell->attracting());
  REQUIRE(shell->currentVariant() == "standard");
  CHECK_FALSE(shell->session()->followsMove());
  // The player's own preference is theirs: the trailer must not rewrite it.
  CHECK(shell->settings().cameraMode == "route");
  // ... and the same on the next beat's fresh session.
  run(*shell, 400.0f);
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the Follow camera mode restores the chase", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-follow2.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Follow);
  CHECK(shell->session()->followsMove());
  shell->setTrailerCamera(TrailerCamera::Auto);
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the Auto camera sways a few degrees and Authored holds still",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-sway.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Auto);  // the default is the authored camera
  run(*shell, 10.0f);
  const view::OrbitCamera base = shell->session()->camera();
  const view::OrbitCamera swayed = shell->trailerView(base);
  const float d = std::abs(swayed.yaw - base.yaw);
  CHECK(d > 0.0f);
  CHECK(d < 0.1f);
  CHECK(swayed.target.x == base.target.x);

  shell->setTrailerCamera(TrailerCamera::Authored);
  run(*shell, 5.0f);  // the sway fades out
  const view::OrbitCamera held = shell->session()->camera();
  CHECK(shell->trailerView(held).yaw == held.yaw);
  CHECK(shell->trailerView(held).pitch == held.pitch);
}

TEST_CASE("the trailer's own camera does nothing outside the trailer",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-outside.conf");
  REQUIRE(shell->startGame("standard").has_value());
  const view::OrbitCamera c = shell->session()->camera();
  CHECK(shell->trailerView(c).yaw == c.yaw);
}

TEST_CASE("edit mode freezes the trailer and selects the authored camera",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-edit.conf");
  shell->startAttract(defaultAttractSequence());
  run(*shell, 3.0f);
  shell->setTrailerEditing(true);
  CHECK(shell->trailerEditing());
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);
  const std::size_t beat = shell->attractStep();
  const std::size_t index = shell->session()->watchIndex();
  run(*shell, 300.0f);  // far longer than the whole trailer
  CHECK(shell->attractStep() == beat);
  CHECK(shell->session()->watchIndex() == index);
  shell->setTrailerEditing(false);
  run(*shell, 5.0f);
  // Resumed: the opener is brisk enough to be on a later move, or already cut onward.
  CHECK((shell->attractStep() > beat || shell->session()->watchIndex() > index));
}

TEST_CASE("stepping in edit mode lands on the requested ply", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-step.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  CHECK_FALSE(shell->trailerStep(-5));  // cannot go before the start
  for (int i = 0; i < 6; ++i) CHECK(shell->trailerStep(+1));
  run(*shell, 3.0f);
  CHECK(shell->session()->game().moveHistory().size() == 7);
  CHECK(shell->trailerStep(-1));
  CHECK(shell->session()->game().moveHistory().size() == 6);
  CHECK(shell->trailerStep(-6));
  CHECK(shell->session()->game().moveHistory().size() == 0);
  // Not in edit mode: stepping is refused rather than racing the trailer clock.
  shell->setTrailerEditing(false);
  CHECK_FALSE(shell->trailerStep(+1));
}

TEST_CASE("jumping to a beat replays the chain so it continues the position",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-goto.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  shell->trailerGoto(2, 0);
  CHECK(shell->attractStep() == 2);
  CHECK(shell->currentVariant() == "torus");
  // A torus beat that continues the game is never the 32-piece opening position.
  const auto& pos = shell->session()->game().position();
  int pieces = 0;
  for (CellId c = 0; c < pos.cellCount(); ++c) {
    if (!pos.isEmpty(c)) ++pieces;
  }
  CHECK(pieces < 32);
  CHECK(pieces > 4);
  // And a jump backwards still works.
  shell->trailerGoto(0, 0);
  CHECK(shell->attractStep() == 0);
  CHECK(shell->currentVariant() == "standard");
  // The next advance reports the cut so the front end re-frames.
  CHECK(shell->advanceAttract(1.0f / 60.0f));
}

TEST_CASE(
    "recording during the trailer pins the keyframe to its beat and plays only in "
    "the authored mode",
    "[unit][app][trailer]") {
  auto shell = makeShell("trailer-record.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  shell->setTrailerCamera(TrailerCamera::Auto);  // recording must switch to Authored
  shell->trailerGoto(1, 0);
  Action orbit;
  orbit.kind = ActionKind::Orbit;
  orbit.dx = 0.5f;
  (void)shell->session()->apply(orbit);
  shell->settings().geometryInvert = true;
  shell->recordCameraKeyframe(8.0f);
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);
  const std::size_t ply = shell->session()->game().moveHistory().size();
  const CameraKeyframe* kf = shell->cameraTrack().find({"cylinder", ply, 2});
  REQUIRE(kf != nullptr);
  CHECK(kf->invert);
  // The ordinary-game cue is distinct.
  CHECK(shell->cameraTrack().find({"cylinder", ply, 0}) == nullptr);
}

TEST_CASE("changing a setting mid-trailer does not hand the camera back to the player",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-apply.conf");
  shell->settings().cameraMode = "route";
  shell->settings().followStrength = 1.0f;
  shell->startAttract(defaultAttractSequence());
  shell->applySettings();
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the standard opener is short and the whole chain is one continuous game",
          "[unit][app][trailer]") {
  const AttractSequence seq = defaultAttractSequence();
  REQUIRE_FALSE(seq.empty());
  CHECK(seq.front().variant == "standard");
  // "By move 20 people will click off": the flat board gets at most 20 plies.
  CHECK(seq.front().moves.size() <= 20);
  CHECK(seq.front().dwell >= 0.0f);  // and plays briskly
  CHECK(seq.front().dwell < 0.4f);
  for (std::size_t i = 1; i < seq.size(); ++i) {
    CAPTURE(i);
    CHECK(seq[i].continuesPosition);
    CHECK(seq[i].moves.size() <= 10);
  }
}

TEST_CASE("the layer colour swap is a persisted setting, off by default",
          "[unit][app][trailer]") {
  const auto path = tempSettings("layer-swap.conf");
  Settings s;
  CHECK_FALSE(s.swapLayerColors);
  s.swapLayerColors = true;
  REQUIRE(s.save(path).has_value());
  CHECK(Settings::load(path).swapLayerColors);
}

TEST_CASE("trailer look overrides survive the beat cut and clear on stop",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-override.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerFlatPieces(true);
  shell->setTrailerGhost(0.5f);
  run(*shell, 400.0f);  // across several cuts
  CHECK(shell->settings().shapePieceCylinders);
  CHECK(shell->settings().geometryGhost < 0.99f);
  shell->stopAttract();
  CHECK_FALSE(shell->settings().shapePieceCylinders);
  CHECK(shell->settings().geometryGhost == 1.0f);
}

TEST_CASE("the trailer previews each move for the configured time",
          "[unit][app][trailer][preview]") {
  auto shell = makeShell("trailer-preview.conf");
  CHECK(shell->settings().trailerMovePreview == 0.3f);
  shell->settings().trailerMovePreview = 9.0f;
  shell->settings().sanitize();
  CHECK(shell->settings().trailerMovePreview == 0.5f);
  shell->settings().trailerMovePreview = 0.3f;
  shell->startAttract(defaultAttractSequence());
  bool sawPreview = false;
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 600 && !sawPreview; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    sawPreview = shell->session()->selected() != kInvalidCell;
  }
  CHECK(sawPreview);
}

TEST_CASE("a beat jump asked for by the interface waits for the next frame",
          "[unit][app][trailer]") {
  // The interface holds a reference to the session for the rest of its frame; a jump
  // replaces the session, so doing it mid-frame left the panel on a dead one and crashed.
  auto shell = makeShell("trailer-defer.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  const Session* before = shell->session();
  shell->requestTrailerJumpBeat(+1);
  CHECK(shell->session() == before);  // nothing happened yet
  CHECK(shell->attractStep() == 0);
  CHECK(shell->advanceAttract(1.0f / 60.0f));  // applied here, reported as a cut
  CHECK(shell->attractStep() == 1);
  shell->requestTrailerJumpBeat(-1);
  CHECK(shell->advanceAttract(1.0f / 60.0f));
  CHECK(shell->attractStep() == 0);
  // Jumping backwards from the first beat wraps to the last.
  shell->requestTrailerJumpBeat(-1);
  CHECK(shell->advanceAttract(1.0f / 60.0f));
  CHECK(shell->attractStep() == shell->attractCount() - 1);
  // A request outside edit mode, or after the trailer stops, is dropped.
  shell->setTrailerEditing(false);
  shell->requestTrailerJumpBeat(+1);
  shell->stopAttract();
  CHECK_FALSE(shell->advanceAttract(1.0f / 60.0f));
}

namespace {

/// A keyframe on the trailer's first beat, `ply` moves in, with a given yaw and morph.
CameraKeyframe trailerKey(std::size_t beat, std::size_t ply, std::size_t sub, float yaw,
                          float formed = 1.0f, const char* variant = "standard") {
  CameraKeyframe kf;
  kf.cue = {variant, ply, beat + 1, sub};
  kf.camera.yaw = yaw;
  kf.camera.pitch = 0.8f;
  kf.camera.distance = 12.0f;
  kf.formed = formed;
  return kf;
}

}  // namespace

TEST_CASE("between keyframes the trailer camera keeps moving smoothly",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-spline.conf");
  shell->settings().watchDwell = 0.3f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  for (const auto& [ply, yaw] : {std::pair{1, 0.0f}, std::pair{5, 0.6f},
                                 std::pair{9, 0.2f}, std::pair{13, 1.0f}}) {
    shell->cameraTrack().set(trailerKey(0, static_cast<std::size_t>(ply), 0, yaw));
  }
  constexpr float kStep = 1.0f / 60.0f;
  float prev = shell->session()->camera().yaw;
  int still = 0, frames = 0;
  float worst = 0.0f;
  for (int i = 0; i < 60 * 40 && shell->attractStep() == 0; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    const std::size_t ply = shell->session()->game().moveHistory().size();
    const float yaw = shell->session()->camera().yaw;
    if (ply >= 2 && ply <= 12) {  // strictly between the first and last keyframe
      ++frames;
      if (std::abs(yaw - prev) < 1e-7f) ++still;
      worst = std::max(worst, std::abs(yaw - prev));
    }
    prev = yaw;
  }
  REQUIRE(frames > 100);
  // A chain of eased legs stops dead at every keyframe; a spline never does.
  CHECK(still < frames / 20);
  CHECK(worst < 0.02f);  // no jump from frame to frame
}

TEST_CASE("the 3-D morph is part of a trailer keyframe and a beat still ends",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-morph.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  shell->cameraTrack().set(trailerKey(1, 1, 0, 0.3f, 0.2f, "cylinder"));
  shell->cameraTrack().set(trailerKey(1, 9, 0, 0.3f, 0.8f, "cylinder"));
  bool sawLow = false, sawHigh = false;
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 60 * 120 && shell->attractStep() < 3; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    if (shell->attractStep() == 1) {
      const std::size_t ply = shell->session()->game().moveHistory().size();
      if (ply == 1)
        sawLow = sawLow || std::abs(shell->settings().geometryFormed - 0.2f) < 0.05f;
      if (ply == 9)
        sawHigh = sawHigh || std::abs(shell->settings().geometryFormed - 0.8f) < 0.05f;
    }
  }
  CHECK(sawLow);
  CHECK(sawHigh);
  CHECK(shell->attractStep() >=
        3);  // the authored morph did not stop the beats advancing
}

TEST_CASE("fractional steps move through a move on the chosen grid",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-grid.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  CHECK(shell->trailerGrid() == 1);
  shell->setTrailerGrid(4);
  CHECK(shell->trailerGrid() == 4);
  shell->setTrailerGrid(3);  // not a grid: refused
  CHECK(shell->trailerGrid() == 4);
  const std::size_t ply = shell->session()->game().moveHistory().size();
  CHECK(shell->trailerEditSub() == 0);
  CHECK(shell->trailerStepFraction(+1));
  CHECK(shell->trailerEditSub() == 2);  // a quarter, in eighths
  CHECK(shell->trailerStepFraction(+1));
  CHECK(shell->trailerEditSub() == 4);
  CHECK(shell->session()->game().moveHistory().size() == ply);
  for (int i = 0; i < 2; ++i) CHECK(shell->trailerStepFraction(+1));
  // Four quarters later it is the next move, back at its start.
  CHECK(shell->trailerEditSub() == 0);
  CHECK(shell->session()->game().moveHistory().size() == ply + 1);
  CHECK(shell->trailerStepFraction(-1));
  CHECK(shell->trailerEditSub() == 6);
  CHECK(shell->session()->game().moveHistory().size() == ply);
  // A keyframe recorded here is on the fractional cue.
  shell->setTrailerGrid(8);
  CHECK(shell->trailerStepFraction(-1));
  shell->recordCameraKeyframe(2.0f);
  CHECK(shell->cameraTrack().find({"standard", ply, 1, 5}) != nullptr);
}

TEST_CASE("restarting the trailer goes back to the first beat", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-restart.conf");
  shell->settings().watchDwell = 0.05f;
  shell->startAttract(defaultAttractSequence());
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 60 * 60 && shell->attractStep() < 2; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  REQUIRE(shell->attractStep() >= 2);
  shell->setTrailerCamera(TrailerCamera::Authored);
  const Session* before = shell->session();
  shell->requestTrailerRestart();
  CHECK(shell->session() == before);    // deferred: the interface holds the session
  CHECK(shell->advanceAttract(kStep));  // reported as a cut so the front end re-frames
  CHECK(shell->attractStep() == 0);
  CHECK(shell->currentVariant() == "standard");
  CHECK(shell->attracting());
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);  // the mode is kept
  CHECK(shell->session()->game().moveHistory().size() == 1);
}

TEST_CASE("a zoom-in between two keyframes lands between them, not at the close one",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-zoom.conf");
  shell->settings().watchDwell = 0.3f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  CameraKeyframe far = trailerKey(0, 1, 0, 0.5f);
  far.camera.distance = 40.0f;
  CameraKeyframe near = trailerKey(0, 9, 0, 0.5f);
  near.camera.distance = 10.0f;
  CameraKeyframe mid = trailerKey(0, 5, 0, 0.5f);
  mid.camera.distance = 25.0f;
  shell->cameraTrack().set(far);
  shell->cameraTrack().set(mid);
  shell->cameraTrack().set(near);
  constexpr float kStep = 1.0f / 60.0f;
  float atMid = -1.0f;
  float lo = 1e9f;
  for (int i = 0; i < 60 * 40 && shell->attractStep() == 0; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    const std::size_t ply = shell->session()->game().moveHistory().size();
    const float d = shell->session()->camera().distance;
    lo = std::min(lo, d);
    if (ply == 5 && atMid < 0.0f) atMid = d;
  }
  // The pull-back factor is 1 outside the pause screen, so the distances are the keys'.
  CHECK(atMid > 22.0f);
  CHECK(atMid < 28.0f);
  CHECK(lo >= 9.5f);  // never closer than the nearest key
}

TEST_CASE("the authored camera (and so edit mode) owns the camera",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-owns.conf");
  CHECK_FALSE(shell->trailerOwnsCamera());  // no trailer
  shell->startAttract(defaultAttractSequence());
  CHECK_FALSE(shell->trailerOwnsCamera());  // Auto
  shell->setTrailerCamera(TrailerCamera::Authored);
  CHECK_FALSE(
      shell->trailerOwnsCamera());  // authored but nothing keyed: it frames itself
  CHECK(shell->trailerAutoFrames());
  shell->cameraTrack().set(trailerKey(0, 3, 0, 0.5f));
  CHECK(shell->trailerOwnsCamera());  // keyframes make it the author's
  CHECK_FALSE(shell->trailerAutoFrames());
  shell->cameraTrack().clear();
  shell->setTrailerCamera(TrailerCamera::Auto);
  shell->setTrailerEditing(true);  // edit mode selects Authored and owns the camera
  CHECK(shell->trailerOwnsCamera());
}

TEST_CASE("the trailer timeline maps time to a beat, move and fraction and back",
          "[unit][app][trailer][timeline]") {
  auto shell = makeShell("trailer-timeline.conf");
  const AttractSequence seq = defaultAttractSequence();
  shell->startAttract(seq);
  float total = 0.0f;
  for (const AttractStep& s : seq) total += static_cast<float>(s.moves.size() + 1);
  CHECK(shell->trailerLength() == total);
  CHECK(shell->beatOffset(0) == 0.0f);
  CHECK(shell->beatOffset(1) == static_cast<float>(seq[0].moves.size() + 1));

  shell->setTrailerGrid(8);
  for (const auto& [beat, ply, sub] : {std::tuple{0, 1, 0}, std::tuple{0, 5, 3},
                                       std::tuple{2, 4, 7}, std::tuple{5, 10, 0}}) {
    const float g = shell->beatOffset(static_cast<std::size_t>(beat)) +
                    static_cast<float>(ply) + static_cast<float>(sub) / 8.0f;
    const TrailerCue cue = shell->trailerCueAt(g);
    CAPTURE(beat, ply, sub);
    CHECK(cue.beat == static_cast<std::size_t>(beat));
    CHECK(cue.ply == static_cast<std::size_t>(ply));
    CHECK(cue.sub == static_cast<std::size_t>(sub));
  }
  // Snapped to the grid in use.
  shell->setTrailerGrid(4);
  const float base = shell->beatOffset(1) + 3.0f;
  CHECK(shell->trailerCueAt(base + 0.30f).sub == 2);
  CHECK(shell->trailerCueAt(base + 0.40f).sub == 4);
  CHECK(shell->trailerCueAt(base + 0.90f).ply == 4);  // rounds up into the next move
  CHECK(shell->trailerCueAt(base + 0.90f).sub == 0);
  // Out of range clamps rather than wandering off the trailer.
  CHECK(shell->trailerCueAt(-5.0f).beat == 0);
  CHECK(shell->trailerCueAt(total + 50.0f).beat == seq.size() - 1);
  // The very end of a beat is its last slot, not a wrap back to its start.
  const TrailerCue lastCue = shell->trailerCueAt(total + 50.0f, 8);
  CHECK(lastCue.ply == seq.back().moves.size());
  CHECK(lastCue.sub == 7);
}

TEST_CASE("keyframe times for the timeline are in order and offset by beat",
          "[unit][app][trailer][timeline]") {
  auto shell = makeShell("trailer-keytimes.conf");
  shell->startAttract(defaultAttractSequence());
  shell->cameraTrack().set(trailerKey(1, 3, 4, 0.1f, 1.0f, "cylinder"));
  shell->cameraTrack().set(trailerKey(0, 2, 0, 0.1f));
  shell->cameraTrack().set(trailerKey(0, 2, 2, 0.1f));
  // A keyframe for another variant's beat, or an ordinary-game one, is not on the
  // timeline.
  shell->cameraTrack().set(trailerKey(1, 3, 0, 0.1f, 1.0f, "torus"));
  CameraKeyframe loose = trailerKey(0, 1, 0, 0.1f);
  loose.cue.beat = 0;
  shell->cameraTrack().set(loose);
  const std::vector<float> times = shell->trailerKeyTimes();
  REQUIRE(times.size() == 3);
  CHECK(times[0] == 2.0f);
  CHECK(times[1] == 2.25f);
  CHECK(times[2] == shell->beatOffset(1) + 3.5f);
}

TEST_CASE("a timeline click seeks a frame later", "[unit][app][trailer][timeline]") {
  auto shell = makeShell("trailer-seek.conf");
  shell->startAttract(defaultAttractSequence());

  shell->setTrailerEditing(true);
  // Within the beat: same session, the move and fraction set.
  const Session* before = shell->session();
  shell->requestTrailerSeek(0, 6, 4);
  CHECK(shell->session() == before);
  CHECK(shell->advanceAttract(1.0f / 60.0f) == false);
  CHECK(shell->session()->game().moveHistory().size() == 6);
  CHECK(shell->trailerEditSub() == 4);
  // Across beats: replays the chain, reports a cut.
  shell->requestTrailerSeek(2, 3, 2);
  CHECK(shell->advanceAttract(1.0f / 60.0f));
  CHECK(shell->attractStep() == 2);
  CHECK(shell->session()->game().moveHistory().size() == 3);
  CHECK(shell->trailerEditSub() == 2);
  CHECK(shell->trailerTime() == shell->beatOffset(2) + 3.25f);
}

TEST_CASE("a beat's own morph plays until its first keyframe takes over",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-morphgate.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  // Keys only in the cylinder beat, from move 6; a formed value of 1 would, held across
  // the whole trailer, erase every beat's reveal.
  shell->cameraTrack().set(trailerKey(1, 6, 0, 0.3f, 1.0f, "cylinder"));
  shell->cameraTrack().set(trailerKey(1, 8, 0, 0.3f, 1.0f, "cylinder"));
  constexpr float kStep = 1.0f / 60.0f;
  float cylinderStart = -1.0f;
  float torusStart = -1.0f;
  for (int i = 0; i < 60 * 120 && shell->attractStep() < 3; ++i) {
    shell->session()->advanceAnimation(kStep);
    const bool cut = shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    if (cut && shell->attractStep() == 1 && cylinderStart < 0.0f) {
      cylinderStart = shell->settings().geometryFormed;
    }
    if (cut && shell->attractStep() == 2 && torusStart < 0.0f) {
      torusStart = shell->settings().geometryFormed;
    }
  }
  // Both beats begin part-way up their roll (the driver's `formedFrom`), not at the
  // key's 1.
  CHECK(cylinderStart < 0.9f);  // the opener ended flat; the roll begins from there
  CHECK(torusStart > 0.0f);
  CHECK(torusStart < 0.9f);
}

TEST_CASE("the trailer's morph and move speeds are their own settings",
          "[unit][app][trailer][speed]") {
  Settings s;
  CHECK(s.trailerMorphSpeed == 0.5f);  // calmer than the raw morph rate
  CHECK(s.trailerMoveSpeed == 1.0f);
  s.trailerMorphSpeed = 99.0f;
  s.trailerMoveSpeed = 0.0f;
  s.sanitize();
  CHECK(s.trailerMorphSpeed == 4.0f);
  CHECK(s.trailerMoveSpeed == 0.25f);
  s.trailerMorphSpeed = 2.5f;
  s.trailerMoveSpeed = 0.5f;
  const auto path = tempSettings("trailer-speeds.conf");
  REQUIRE(s.save(path).has_value());
  const Settings back = Settings::load(path);
  CHECK(back.trailerMorphSpeed == 2.5f);
  CHECK(back.trailerMoveSpeed == 0.5f);
}

TEST_CASE(
    "the move speed scales the trailer's piece animation, the morph speed its morph",
    "[unit][app][trailer][speed]") {
  const auto firstMoveSeconds = [](float moveSpeed) {
    auto shell = makeShell("trailer-movespeed.conf");
    shell->settings().animationSpeed = 1.0f;
    shell->settings().trailerMoveSpeed = moveSpeed;
    shell->startAttract(defaultAttractSequence());
    return shell->session()->animation().duration();
  };
  const float normal = firstMoveSeconds(1.0f);
  const float fast = firstMoveSeconds(2.0f);
  REQUIRE(normal > 0.0f);
  CHECK(fast == Catch::Approx(normal * 0.5f).epsilon(0.02));

  // Frames the cylinder beat's reveal takes to finish, from the cut into it.
  const auto framesToForm = [](float morphSpeed) {
    auto shell = makeShell("trailer-morphspeed.conf");
    shell->settings().watchDwell = 0.05f;
    shell->settings().trailerMorphSpeed = morphSpeed;
    shell->startAttract(defaultAttractSequence());
    while (shell->attractStep() < 1) {
      shell->session()->advanceAnimation(1.0f / 60.0f);
      shell->advanceAttract(1.0f / 60.0f);
    }
    int frames = 0;
    while (shell->settings().geometryFormed < 0.999f && frames < 60 * 60) {
      shell->session()->advanceAnimation(1.0f / 60.0f);
      shell->advanceAttract(1.0f / 60.0f);
      ++frames;
    }
    return frames;
  };
  const int slow = framesToForm(1.0f);
  const int quick = framesToForm(2.0f);
  CHECK(quick < slow);
  CHECK(quick <= slow / 2 + 2);
}

TEST_CASE("pausing freezes the trailer without entering edit mode",
          "[unit][app][trailer][pause]") {
  auto shell = makeShell("trailer-pause.conf");
  shell->startAttract(defaultAttractSequence());
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 90; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  shell->setTrailerPaused(true);
  CHECK(shell->trailerPaused());
  CHECK_FALSE(shell->trailerEditing());
  const std::size_t beat = shell->attractStep();
  const std::size_t index = shell->session()->watchIndex();
  const float time = shell->trailerTime();
  for (int i = 0; i < 60 * 300; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  CHECK(shell->attractStep() == beat);
  CHECK(shell->session()->watchIndex() == index);
  CHECK(shell->trailerTime() == time);
  shell->setTrailerPaused(false);
  for (int i = 0; i < 60 * 5; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  CHECK(shell->trailerTime() > time);
}

TEST_CASE("clicking the timeline while playing moves there and keeps playing",
          "[unit][app][trailer][pause]") {
  auto shell = makeShell("trailer-seekplay.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  constexpr float kStep = 1.0f / 60.0f;
  shell->requestTrailerSeek(2, 4, 4);
  CHECK(shell->advanceAttract(kStep));  // across beats: a cut
  CHECK(shell->attractStep() == 2);
  CHECK(shell->session()->game().moveHistory().size() == 4);
  CHECK_FALSE(shell->trailerEditing());
  // Half way through the move, as asked.
  CHECK(shell->trailerTime() >= shell->beatOffset(2) + 4.45f);
  CHECK(shell->trailerTime() < shell->beatOffset(2) + 4.6f);
  // And it keeps playing from there.
  for (int i = 0; i < 60 * 4; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  CHECK(shell->trailerTime() > shell->beatOffset(2) + 5.0f);
  // Seeking while paused lands and stays paused.
  shell->setTrailerPaused(true);
  shell->requestTrailerSeek(0, 7, 0);
  shell->advanceAttract(kStep);
  CHECK(shell->attractStep() == 0);
  CHECK(shell->session()->game().moveHistory().size() == 7);
  const float t = shell->trailerTime();
  for (int i = 0; i < 120; ++i) shell->advanceAttract(kStep);
  CHECK(shell->trailerTime() == t);
}

TEST_CASE("keys step to the next and previous keyframe", "[unit][app][trailer][keys]") {
  auto shell = makeShell("trailer-keynav.conf");
  shell->settings().watchDwell = 0.05f;
  shell->startAttract(defaultAttractSequence());
  shell->cameraTrack().set(trailerKey(0, 3, 0, 0.1f));
  shell->cameraTrack().set(trailerKey(0, 7, 4, 0.2f));
  shell->cameraTrack().set(trailerKey(1, 2, 0, 0.3f, 1.0f, "cylinder"));  // the cylinder beat
  constexpr float kStep = 1.0f / 60.0f;
  shell->setTrailerPaused(true);
  CHECK(shell->requestTrailerSeekKey(+1));
  shell->advanceAttract(kStep);
  CHECK(shell->trailerTime() == 3.0f);
  CHECK(shell->requestTrailerSeekKey(+1));
  shell->advanceAttract(kStep);
  CHECK(shell->trailerTime() == 7.5f);  // a fractional keyframe is a stop too
  CHECK(shell->requestTrailerSeekKey(+1));
  CHECK(shell->advanceAttract(kStep));  // into the next beat: a cut
  CHECK(shell->attractStep() == 1);
  CHECK(shell->trailerTime() == shell->beatOffset(1) + 2.0f);
  CHECK_FALSE(shell->requestTrailerSeekKey(+1));  // no later keyframe
  CHECK(shell->requestTrailerSeekKey(-1));
  shell->advanceAttract(kStep);
  CHECK(shell->trailerTime() == 7.5f);
  CHECK(shell->attractStep() == 0);
  CHECK(shell->requestTrailerSeekKey(-1));
  shell->advanceAttract(kStep);
  CHECK(shell->trailerTime() == 3.0f);
  CHECK_FALSE(shell->requestTrailerSeekKey(-1));
}

TEST_CASE("a keyframe added while the trailer plays lands on the nearest eighth",
          "[unit][app][trailer][keys]") {
  auto shell = makeShell("trailer-keyadd.conf");
  shell->settings().watchDwell = 0.6f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 60 * 2; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  const float here = shell->trailerTime();
  const TrailerCue cue = shell->trailerCueAt(here, 8);
  shell->recordCameraKeyframe(2.0f);
  CHECK(shell->cameraTrack().find({"standard", cue.ply, cue.beat + 1, cue.sub}) !=
        nullptr);
  CHECK(shell->trailerKeyTimes().size() == 1);
  // Delete takes the keyframe nearest the playhead, not only one exactly on it.
  for (int i = 0; i < 1; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  CHECK(shell->eraseCameraKeyframe());
  CHECK(shell->cameraTrack().empty());
  CHECK_FALSE(shell->eraseCameraKeyframe());
}

TEST_CASE("space pauses and resumes; touching the camera pauses into edit mode",
          "[unit][app][trailer][keys]") {
  auto shell = makeShell("trailer-space.conf");
  shell->startAttract(defaultAttractSequence());
  shell->toggleTrailerPause();
  CHECK(shell->trailerPaused());
  shell->toggleTrailerPause();
  CHECK_FALSE(shell->trailerPaused());
  shell->setTrailerEditing(true);
  shell->toggleTrailerPause();  // leaves edit mode and plays
  CHECK_FALSE(shell->trailerEditing());
  CHECK_FALSE(shell->trailerPaused());

  // Grabbing the camera while keyframes would take it back pauses into edit mode.
  shell->cameraTrack().set(trailerKey(0, 3, 0, 0.1f));
  shell->trailerUserTookCamera();
  CHECK(shell->trailerEditing());
  shell->setTrailerEditing(false);
  // The chase camera is the player's own business: left alone.
  shell->setTrailerCamera(TrailerCamera::Follow);
  shell->trailerUserTookCamera();
  CHECK_FALSE(shell->trailerEditing());
  // And outside the trailer it does nothing at all.
  shell->stopAttract();
  shell->trailerUserTookCamera();
  CHECK_FALSE(shell->trailerEditing());
}

TEST_CASE("the camera is carried across a cut, not reset to the new beat's default",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-carry.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  Action orbit;
  orbit.kind = ActionKind::Orbit;
  orbit.dx = 1.1f;
  orbit.dy = -0.3f;
  (void)shell->session()->apply(orbit);
  Action zoom;
  zoom.kind = ActionKind::Zoom;
  zoom.dx = 1.5f;
  (void)shell->session()->apply(zoom);
  const view::OrbitCamera before = shell->session()->camera();
  constexpr float kStep = 1.0f / 60.0f;
  bool cut = false;
  for (int i = 0; i < 60 * 120 && !cut; ++i) {
    shell->session()->advanceAnimation(kStep);
    cut = shell->advanceAttract(kStep);
  }
  REQUIRE(cut);
  REQUIRE(shell->attractStep() == 1);
  const view::OrbitCamera after = shell->session()->camera();
  // The cut from the ordinary board to the shape renderer's flat board is a half turn,
  // so the picture stays the same.
  CHECK(after.yaw == Catch::Approx(before.yaw + 3.14159265f));
  CHECK(after.pitch == before.pitch);
  CHECK(after.distance == before.distance);
}

TEST_CASE("the standard opener hands a flat cylinder beat the same picture",
          "[unit][app][trailer]") {
  const AttractSequence seq = defaultAttractSequence();
  REQUIRE(seq.size() >= 2);
  CHECK(seq[0].variant == "standard");
  CHECK(seq[1].variant == "cylinder");
  CHECK(seq[1].formedFrom == 0.0f);  // the roll starts from the same flat board
  CHECK(seq[1].formedTarget == 1.0f);
  // The slow morphs are slow on purpose: the torus's bend is the one that used to wind
  // up.
  CHECK(seq[2].morphSpeed < seq[1].morphSpeed);
}

TEST_CASE("a keyframe at a beat's first move does not erase the 3-D torus's spread",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-spread.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  for (const std::size_t ply : {std::size_t{0}, std::size_t{6}}) {
    CameraKeyframe kf = trailerKey(5, ply, 0, 0.3f, 1.0f, "torus3d_8x8");
    kf.shellSpread = 1.0f;
    shell->cameraTrack().set(kf);
  }
  constexpr float kStep = 1.0f / 60.0f;
  bool inBeat = false;
  float first = -1.0f, prev = 0.0f, worst = 0.0f, last = 0.0f;
  for (int i = 0; i < 60 * 400 && shell->attractStep() <= 5; ++i) {
    shell->session()->advanceAnimation(kStep);
    const bool cut = shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    if (inBeat && shell->attractStep() != 5) break;  // the lap wrapped round
    if (shell->attractStep() == 5) {
      const float s = shell->settings().geometryShellSpread;
      if (cut && !inBeat) {
        inBeat = true;
        first = s;
      } else if (inBeat) {
        worst = std::max(worst, std::abs(s - prev));
      }
      prev = s;
      last = s;
    }
  }
  REQUIRE(inBeat);
  CHECK(first < 0.05f);   // the levels open collapsed onto one ring, matching the Klein ring
  // ... and spread without a jump (the test clock runs a move in ~0.25 s, so the hand-over
  // is ~15 frames; a snap would be 1.0 in one frame)
  CHECK(worst < 0.15f);
  CHECK(last > 0.5f);
}

TEST_CASE("the cylinder's roll blends into its first keyframe instead of snapping shut",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-cylsnap.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  shell->cameraTrack().set(trailerKey(1, 1, 0, 0.3f, 1.0f, "cylinder"));
  constexpr float kStep = 1.0f / 60.0f;
  bool inBeat = false;
  float first = -1.0f, prev = 0.0f, worst = 0.0f;
  for (int i = 0; i < 60 * 200 && shell->attractStep() <= 1; ++i) {
    shell->session()->advanceAnimation(kStep);
    const bool cut = shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    if (shell->attractStep() == 1) {
      const float f = shell->settings().geometryFormed;
      if (cut && !inBeat) {
        inBeat = true;
        first = f;
      } else if (inBeat) {
        worst = std::max(worst, std::abs(f - prev));
      }
      prev = f;
    }
  }
  REQUIRE(inBeat);
  CHECK(first < 0.05f);
  CHECK(worst < 0.15f);  // a snap shut would be about 1.0 in one frame
}

TEST_CASE("the live keyframe blend takes the short way round", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-livelap.conf");
  REQUIRE(shell->startGame("torus").has_value());
  shell->settings().geometrySlideU = 30.0f;
  CameraKeyframe kf = trailerKey(0, 0, 0, 0.3f, 1.0f, "torus");
  kf.cue = {"torus", 0, 0, 0};
  kf.slideU = 0.0f;
  kf.morphSpeed = 2.0f;
  shell->cameraTrack().set(kf);
  constexpr float kStep = 1.0f / 60.0f;
  float prev = 30.0f, travel = 0.0f;
  for (int i = 0; i < 600; ++i) {
    shell->advanceCameraTrack(kStep);
    travel += std::abs(shell->settings().geometrySlideU - prev);
    prev = shell->settings().geometrySlideU;
  }
  CHECK(prev == Catch::Approx(32.0f).margin(1e-3));
  CHECK(travel < 2.01f);
}
