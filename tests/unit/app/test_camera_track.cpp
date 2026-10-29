// SPDX-License-Identifier: GPL-3.0-or-later
//
// M13.9 Part E: the camera-keyframe authoring scaffold. `CameraTrack` itself is tested
// as pure data (set/erase/ordering, and the text form round-trips); `Shell`'s own
// record/erase/advance is tested the way the rest of this file tests the shell -
// headless, driving actions and a clock, never a window.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "app/camera_track.hpp"
#include "app/shell.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Shell> makeShell(const std::filesystem::path& settings) {
  auto shell = Shell::create({"standard", "cylinder"}, settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}

std::filesystem::path tempSettings(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "endspielzero-tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::filesystem::remove(path);
  std::filesystem::remove(path.parent_path() / "camera_track.txt");
  return path;
}

CameraKeyframe sample(std::string variant, std::size_t ply) {
  CameraKeyframe kf;
  kf.cue = {std::move(variant), ply};
  kf.camera.target = {1.0f, 2.0f, 3.0f};
  kf.camera.distance = 12.5f;
  kf.camera.yaw = 0.7f;
  kf.camera.pitch = 0.5f;
  kf.camera.roll = 0.1f;
  kf.camera.fovY = 0.8f;
  kf.formed = 0.6f;
  kf.ghost = 0.4f;
  kf.shellSpread = 0.3f;
  kf.slideU = 0.2f;
  kf.slideV = -0.1f;
  kf.flatPieces = true;
  kf.morphSpeed = 3.5f;
  return kf;
}

}  // namespace

TEST_CASE("a camera track keeps at most one keyframe per cue, sorted", "[unit][app]") {
  CameraTrack track;
  track.set(sample("torus", 4));
  track.set(sample("cylinder", 10));
  track.set(sample("cylinder", 2));
  REQUIRE(track.keyframes().size() == 3);
  // Sorted by variant, then ply - not insertion order.
  CHECK(track.keyframes()[0].cue.variant == "cylinder");
  CHECK(track.keyframes()[0].cue.ply == 2);
  CHECK(track.keyframes()[1].cue.variant == "cylinder");
  CHECK(track.keyframes()[1].cue.ply == 10);
  CHECK(track.keyframes()[2].cue.variant == "torus");

  // Setting again at an existing cue replaces it in place rather than adding a second.
  CameraKeyframe replaced = sample("cylinder", 2);
  replaced.morphSpeed = 9.0f;
  track.set(replaced);
  REQUIRE(track.keyframes().size() == 3);
  const CameraKeyframe* found = track.find({"cylinder", 2});
  REQUIRE(found != nullptr);
  CHECK(found->morphSpeed == 9.0f);

  CHECK(track.find({"cylinder", 7}) == nullptr);
  CHECK(track.erase({"cylinder", 2}));
  CHECK_FALSE(track.erase({"cylinder", 2}));  // already gone
  CHECK(track.keyframes().size() == 2);
}

TEST_CASE("a camera track's text form round-trips every field", "[unit][app]") {
  CameraTrack track;
  track.set(sample("cylinder", 3));
  track.set(sample("torus", 0));
  const std::string text = cameraTrackText(track);

  const auto parsed = parseCameraTrack(text);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->keyframes().size() == 2);
  for (const CameraKeyframe& want : track.keyframes()) {
    CAPTURE(want.cue.variant, want.cue.ply);
    const CameraKeyframe* got = parsed->find(want.cue);
    REQUIRE(got != nullptr);
    CHECK(got->camera.target.x == want.camera.target.x);
    CHECK(got->camera.target.y == want.camera.target.y);
    CHECK(got->camera.target.z == want.camera.target.z);
    CHECK(got->camera.distance == want.camera.distance);
    CHECK(got->camera.yaw == want.camera.yaw);
    CHECK(got->camera.pitch == want.camera.pitch);
    CHECK(got->camera.roll == want.camera.roll);
    CHECK(got->camera.fovY == want.camera.fovY);
    CHECK(got->formed == want.formed);
    CHECK(got->ghost == want.ghost);
    CHECK(got->shellSpread == want.shellSpread);
    CHECK(got->slideU == want.slideU);
    CHECK(got->slideV == want.slideV);
    CHECK(got->flatPieces == want.flatPieces);
    CHECK(got->morphSpeed == want.morphSpeed);
  }
}

TEST_CASE("an unknown field or a malformed number fails to parse", "[unit][app]") {
  CHECK_FALSE(
      parseCameraTrack("keyframe variant=cylinder ply=0 nonsense=1\n").has_value());
  CHECK_FALSE(parseCameraTrack("keyframe variant=cylinder ply=0 distance=not-a-number\n")
                  .has_value());
  CHECK_FALSE(parseCameraTrack("keyframe ply=0\n").has_value());  // no variant
  // Comments and blank lines are ignored, not errors.
  CHECK(parseCameraTrack("# a comment\n\nkeyframe variant=cylinder ply=0\n").has_value());
}

TEST_CASE("recording a keyframe captures the live camera and pose at the current move",
          "[unit][app]") {
  auto shell = makeShell(tempSettings("camera-record.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  CHECK(shell->cameraTrack().empty());

  // Move the player's own camera away from its framed default before recording, so the
  // recorded keyframe is provably *this* camera and not just whatever `frame()` set up.
  Action orbit;
  orbit.kind = ActionKind::Orbit;
  orbit.dx = 0.4f;
  orbit.dy = 0.1f;
  (void)shell->session()->apply(orbit);
  const view::OrbitCamera before = shell->session()->camera();

  shell->settings().geometryFormed = 0.5f;
  shell->settings().geometrySlideU = 0.3f;
  shell->recordCameraKeyframe(4.0f);

  const CameraKeyframe* kf = shell->cameraTrack().find({"standard", 0});
  REQUIRE(kf != nullptr);
  CHECK(kf->camera.yaw == before.yaw);
  CHECK(kf->camera.pitch == before.pitch);
  CHECK(kf->formed == 0.5f);
  CHECK(kf->slideU == 0.3f);
  CHECK(kf->morphSpeed == 4.0f);

  // Persisted immediately, at the same path a fresh shell on this settings file would
  // read on `Shell::create` - no separate "save" step the scaffold's one editing key has
  // to also trigger.
  const std::filesystem::path trackPath = shell->cameraTrackPath();
  REQUIRE(std::filesystem::exists(trackPath));
  std::ifstream in(trackPath);
  std::ostringstream ss;
  ss << in.rdbuf();
  const auto reloaded = parseCameraTrack(ss.str());
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->find({"standard", 0}) != nullptr);
  CHECK(reloaded->find({"standard", 0})->morphSpeed == 4.0f);

  CHECK(shell->eraseCameraKeyframe());
  CHECK(shell->cameraTrack().find({"standard", 0}) == nullptr);
  CHECK_FALSE(shell->eraseCameraKeyframe());
}

TEST_CASE("the camera track eases the view toward a keyframe once its move is reached",
          "[unit][app]") {
  auto shell = makeShell(tempSettings("camera-advance.conf"));
  REQUIRE(shell->startGame("standard").has_value());

  CameraKeyframe kf;
  kf.cue = {"standard", 0};
  kf.camera = shell->session()->camera();
  kf.camera.yaw += 1.0f;  // a camera clearly different from wherever play starts
  kf.camera.distance += 5.0f;
  kf.formed = 0.2f;
  kf.morphSpeed = 1.0f;  // one second, exactly, to ease fully in
  shell->cameraTrack().set(kf);

  // Reaching the cue with no prior blend starts one: the camera has not moved yet (the
  // very first `advanceCameraTrack` call only captures the blend's own start). Compared
  // by `eye()`, not the raw yaw field - the same precedent `slerpCamera`'s own tests set
  // (`test_camera.cpp`), since an angle can wrap to an equivalent value with a different
  // float.
  shell->advanceCameraTrack(0.0f);
  const view::Vec3 startEye = shell->session()->camera().eye();
  CHECK_FALSE(view::length(startEye - kf.camera.eye()) < 1e-3f);

  // Halfway through the one-second ease, the camera is partway there - neither where it
  // started nor yet at the keyframe.
  shell->advanceCameraTrack(0.5f);
  const view::Vec3 midEye = shell->session()->camera().eye();
  CHECK_FALSE(view::length(midEye - startEye) < 1e-3f);
  CHECK_FALSE(view::length(midEye - kf.camera.eye()) < 1e-3f);

  // By one second total, the blend has landed on the keyframe.
  shell->advanceCameraTrack(0.5f);
  CHECK(view::length(shell->session()->camera().eye() - kf.camera.eye()) < 1e-3f);
  CHECK(shell->settings().geometryFormed == kf.formed);

  // Further frames at the same cue do nothing more - the blend stays finished rather than
  // re-triggering or drifting.
  const view::Vec3 settledEye = shell->session()->camera().eye();
  shell->advanceCameraTrack(1.0f);
  CHECK(view::length(shell->session()->camera().eye() - settledEye) < 1e-3f);
}

TEST_CASE("a cue with no keyframe leaves the camera alone", "[unit][app]") {
  auto shell = makeShell(tempSettings("camera-no-keyframe.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  const view::OrbitCamera before = shell->session()->camera();
  shell->advanceCameraTrack(1.0f);  // no track at all
  CHECK(shell->session()->camera().yaw == before.yaw);

  shell->cameraTrack().set(sample("standard", 99));  // a keyframe, but not at this cue
  shell->advanceCameraTrack(1.0f);
  CHECK(shell->session()->camera().yaw == before.yaw);
}

TEST_CASE("a trailer cue carries its beat and the invert pose through the text form",
          "[unit][app][trailer]") {
  CameraTrack track;
  // Klein plays twice in the trailer, so (variant, ply) alone would collide.
  CameraKeyframe a = sample("klein", 0);
  a.cue.beat = 4;
  a.invert = true;
  CameraKeyframe b = sample("klein", 0);
  b.cue.beat = 5;
  CameraKeyframe plain = sample("klein", 0);  // beat 0: an ordinary game
  track.set(a);
  track.set(b);
  track.set(plain);
  REQUIRE(track.keyframes().size() == 3);

  const auto parsed = parseCameraTrack(cameraTrackText(track));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->keyframes().size() == 3);
  const CameraKeyframe* ka = parsed->find({"klein", 0, 4});
  const CameraKeyframe* kb = parsed->find({"klein", 0, 5});
  const CameraKeyframe* kp = parsed->find({"klein", 0, 0});
  REQUIRE(ka != nullptr);
  REQUIRE(kb != nullptr);
  REQUIRE(kp != nullptr);
  CHECK(ka->invert);
  CHECK_FALSE(kb->invert);
}

TEST_CASE("a legacy keyframe line with no beat or invert still parses as beat 0",
          "[unit][app][trailer]") {
  const auto parsed = parseCameraTrack(
      "keyframe variant=torus ply=3 target=0,0,0 distance=9 yaw=0.5 pitch=0.6 roll=0 "
      "fov=0.9 formed=1 ghost=1 shellSpread=1 slideU=0 slideV=0 flatPieces=0 speed=2\n");
  REQUIRE(parsed.has_value());
  const CameraKeyframe* kf = parsed->find({"torus", 3, 0});
  REQUIRE(kf != nullptr);
  CHECK_FALSE(kf->invert);
}

TEST_CASE("the beat is written only when it is not zero", "[unit][app][trailer]") {
  CameraTrack track;
  track.set(sample("torus", 1));
  CHECK(cameraTrackText(track).find("beat=") == std::string::npos);
  CameraKeyframe k = sample("torus", 2);
  k.cue.beat = 2;
  track.set(k);
  CHECK(cameraTrackText(track).find("beat=2") != std::string::npos);
}

TEST_CASE("a fractional cue round-trips and orders within its move",
          "[unit][app][trailer]") {
  CameraTrack track;
  CameraKeyframe a = sample("torus", 3);
  a.cue.beat = 2;
  a.cue.sub = 4;  // half way through the move
  CameraKeyframe b = sample("torus", 3);
  b.cue.beat = 2;
  b.cue.sub = 2;
  CameraKeyframe whole = sample("torus", 3);
  whole.cue.beat = 2;
  track.set(a);
  track.set(whole);
  track.set(b);
  REQUIRE(track.keyframes().size() == 3);
  CHECK(track.keyframes()[0].cue.sub == 0);
  CHECK(track.keyframes()[1].cue.sub == 2);
  CHECK(track.keyframes()[2].cue.sub == 4);
  const auto parsed = parseCameraTrack(cameraTrackText(track));
  REQUIRE(parsed.has_value());
  CHECK(parsed->find({"torus", 3, 2, 4}) != nullptr);
  CHECK(parsed->find({"torus", 3, 2, 2}) != nullptr);
  CHECK(parsed->find({"torus", 3, 2, 0}) != nullptr);
  CHECK(cameraTrackText(CameraTrack{}).find("sub=") == std::string::npos);
}
