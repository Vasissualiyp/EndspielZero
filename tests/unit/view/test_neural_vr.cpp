// SPDX-License-Identifier: GPL-3.0-or-later
//
// M25.5 the neural VR: which games the room cannot hold, the handoff into and out of the
// implant's space, the neural code and relayouts as light. All pure in explicit time.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/variants.hpp"
#include "view/layout.hpp"
#include "view/neural_vr.hpp"
#include "view/rig_deploy.hpp"
#include "view/wake.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Approx;

namespace {
std::vector<Placement> boards(int n) {
  std::vector<Placement> out;
  for (int b = 0; b < n; ++b)
    for (int c = 0; c < 4; ++c)
      out.push_back({static_cast<CellId>(b * 4 + c), static_cast<float>(c % 2 + 3 * b),
                     static_cast<float>(c / 2), 0, static_cast<std::uint32_t>(b)});
  return out;
}
}  // namespace

TEST_CASE("neural vr: games with more slabs than the rack's bays go to VR",
          "[unit][view][vr]") {
  // The recorded list (M25.5 Status): slabs per default view, and the verdict.
  const std::map<std::string, std::pair<int, bool>> expected{
      {"standard", {1, false}},      {"cylinder", {1, false}},
      {"torus", {1, false}},         {"mobius", {1, false}},
      {"klein", {1, false}},         {"mirrorbox", {1, false}},
      {"atomic", {1, false}},        {"atomic_torus", {1, false}},
      {"mustcapture", {1, false}},   {"charged", {1, false}},
      {"cube5", {5, false}},         {"hyper4", {16, true}},
      {"hyper4_8x8", {8, false}},    {"t6", {256, true}},
      {"torus3d", {4, false}},       {"torus3d_8x8", {4, false}},
      {"torus3d_twist", {4, false}},
  };
  for (const auto& [name, want] : expected) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const int slabs = deployedSlabCount(layout(v.dims, cfg));
    CAPTURE(slabs);
    CHECK(slabs == want.first);
    CHECK(needsVr(v, cfg) == want.second);
    CHECK(needsVr(layout(v.dims, cfg)) == (slabs > kRackBays));
  }
}

TEST_CASE("neural vr: a variant that can add slabs enters VR at the start (M25.7)",
          "[unit][view][vr]") {
  // Decided from the rules, not the placement count: a temporal or multiverse axis lets a
  // move create boards, so the game cannot stay in the room however few it starts with.
  const std::map<std::string, bool> grows{
      {"standard", false},      {"cylinder", false},      {"torus", false},
      {"mobius", false},        {"klein", false},         {"mirrorbox", false},
      {"atomic", false},        {"atomic_torus", false},  {"mustcapture", false},
      {"charged", false},       {"cube5", false},         {"hyper4", false},
      {"hyper4_8x8", false},    {"t6", false},            {"torus3d", false},
      {"torus3d_8x8", false},   {"torus3d_twist", false}, {"5d", true},
  };
  for (const auto& [name, want] : grows) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    CHECK(canAddSlabs(v) == want);
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const bool bays = deployedSlabCount(layout(v.dims, cfg)) > kRackBays;
    CHECK(needsVr(v, cfg) == (bays || want));
  }
  // Even with a rack that would hold the whole starting layout.
  const VariantSpec v = test::loadVariant("5d");
  CHECK(needsVr(v, ViewConfig::forBoard(v.dims), 1000));
}

TEST_CASE("neural vr: the rack holds eight boards, the ninth crosses into VR",
          "[unit][view][vr]") {
  CHECK(kRackBays == 8);
  CHECK(deployedSlabCount(boards(8)) == 8);
  CHECK_FALSE(needsVr(boards(8)));
  // A 5D game appending its ninth board crosses the limit mid-game.
  CHECK(needsVr(boards(9)));
  CHECK_FALSE(needsVr(boards(9), 9));
  // The 5d variant starts on one board: the front end asks with the visible boards.
  const VariantSpec v = test::loadVariant("5d");
  CHECK(deployedSlabCount(layout(v.dims, ViewConfig::forBoard(v.dims))) > kRackBays);
}

TEST_CASE("neural vr: entering - deploy, code, handshake, eyes close, the horizon",
          "[unit][view][vr]") {
  // The one slab deploys exactly as a standard game's (one departure slot).
  CHECK(kVrSlabLock == Approx(deployLastLock(1)));
  const auto at = [](float t) { return vrEnter(t, 1); };
  CHECK(at(0).room);
  CHECK(at(0).slabAge == Approx(0));
  CHECK(at(2).slabAge == Approx(2));
  CHECK(at(0).code == 0);
  CHECK(at(kVrSlabLock - .01f).code == 0);
  // The code is on, and pulses, for about 1.5 s after the lock.
  float lo = 1, hi = 0;
  for (float t = kVrSlabLock + kVrCodeRise; t < kVrLidsCloseAt; t += .01f) {
    lo = std::min(lo, at(t).code);
    hi = std::max(hi, at(t).code);
    CHECK(at(t).room);
    CHECK(at(t).lidOpen == 1);
  }
  CHECK(hi > .95f);
  CHECK(lo < .8f);
  CHECK(lo > .3f);
  CHECK(kVrLidsCloseAt - kVrSlabLock == Approx(1.5f).margin(.31f));
  // The implant answers before the eyes close.
  CHECK(at(kVrHandshakeAt - .01f).handshake < 0);
  CHECK(at(kVrHandshakeAt + .2f).handshake == Approx(.2f));
  CHECK(kVrHandshakeAt < kVrLidsCloseAt);
  // The eyes close (lids only fall), then darkness, then the horizon opens.
  float last = 1;
  for (float t = kVrLidsCloseAt; t <= kVrLidsCloseAt + kVrLidsCloseSeconds; t += .01f) {
    CHECK(at(t).lidOpen <= last + 1e-6f);
    last = at(t).lidOpen;
  }
  const float shut = kVrLidsCloseAt + kVrLidsCloseSeconds;
  CHECK(at(shut + .01f).lidOpen == 0);
  CHECK_FALSE(at(shut + .01f).room);
  CHECK(at(shut + .01f).dark == 1);
  CHECK(at(shut + kVrDarkSeconds - .01f).horizon == Approx(0).margin(.02f));
  CHECK(at(kVrEnterSeconds - kVrHorizonSeconds * .5f).horizon > .2f);
  CHECK(at(kVrEnterSeconds - kVrHorizonSeconds * .5f).horizon < .8f);
  const auto inside = at(kVrEnterSeconds);
  CHECK(inside.done);
  CHECK_FALSE(inside.room);
  CHECK(inside.horizon == 1);
  CHECK(inside.dark == 0);
  CHECK(inside.lidOpen == 1);
  CHECK(at(99).done);
}

TEST_CASE("neural vr: the handoff is continuous", "[unit][view][vr]") {
  for (const bool enter : {true, false}) {
    const float total = enter ? kVrEnterSeconds : kVrLeaveSeconds;
    VrState prev = enter ? vrEnter(0, 1) : vrLeave(0, 1);
    for (float t = .005f; t <= total + .1f; t += .005f) {
      CAPTURE(enter, t);
      const VrState s = enter ? vrEnter(t, 1) : vrLeave(t, 1);
      CHECK(std::abs(s.code - prev.code) < .05f);
      CHECK(std::abs(s.lidOpen - prev.lidOpen) < .05f);
      CHECK(std::abs(s.dark - prev.dark) < .05f);
      CHECK(std::abs(s.horizon - prev.horizon) < .05f);
      // The picture underneath only changes behind closed eyes or full darkness.
      if (s.room != prev.room) CHECK((s.lidOpen < .02f || s.dark > .98f));
      prev = s;
    }
  }
}

TEST_CASE("neural vr: leaving - eyes open on the room, the code fades, then the stow",
          "[unit][view][vr]") {
  const auto at = [](float t) { return vrLeave(t, 1); };
  CHECK_FALSE(at(0).room);
  CHECK(at(0).dark == 0);
  CHECK(at(kVrLeaveDark - .01f).dark > .9f);
  // The room comes back behind closed lids, with the slab hanging, showing the code.
  const auto back = at(kVrLeaveDark + .01f);
  CHECK(back.room);
  CHECK(back.lidOpen < .05f);
  CHECK(back.code == 1);
  CHECK(back.slabAge < 0);
  float last = 0;
  for (float t = kVrLeaveDark; t < kVrLeaveDark + kVrLidsOpenSeconds; t += .01f) {
    CHECK(at(t).lidOpen >= last - 1e-6f);
    last = at(t).lidOpen;
  }
  CHECK(at(kVrLeaveDark + kVrLidsOpenSeconds).lidOpen == 1);
  // The implant activates again.
  bool handshake = false;
  for (float t = 0; t < kVrLeaveSeconds; t += .05f) handshake |= at(t).handshake >= 0;
  CHECK(handshake);
  // The code fades and the board is off by the end of the lead; the stow follows.
  CHECK(at(kVrLeaveSeconds).code == 0);
  CHECK(at(kVrLeaveSeconds).room);
  CHECK(at(kVrLeaveSeconds).done);
}

TEST_CASE("neural vr: calm crosses through darkness, never nothing", "[unit][view][vr]") {
  for (const bool enter : {true, false}) {
    const auto at = [&](float t) { return enter ? vrEnter(t, 0) : vrLeave(t, 0); };
    const float total = enter ? kVrEnterCalmSeconds : kVrLeaveCalmSeconds;
    CHECK(total <= .7f);
    CHECK(at(0).room == enter);
    CHECK(at(total * .5f).dark == Approx(1).margin(.05f));
    CHECK(at(total).room == !enter);
    CHECK(at(total).done);
    CHECK(at(total).dark == 0);
    for (float t = 0; t < total; t += .01f) {
      CHECK(at(t).lidOpen == 1);
      CHECK(at(t).handshake < 0);
    }
  }
  CHECK(vrEnter(0, 0).slabAge > kVrSlabLock);  // calm hangs the slab at once
}

TEST_CASE("neural vr: 2D closes the eyes, with the code step only from the room",
          "[unit][view][vr]") {
  CHECK(vrBlinkSeconds(true) > vrBlinkSeconds(false));
  for (const bool code : {true, false}) {
    CAPTURE(code);
    const float sw = vrBlinkSwitch(code);
    CHECK(vrBlink(0, code, 1).lidOpen == 1);
    CHECK(vrBlink(sw, code, 1).lidOpen == 0);
    CHECK(vrBlink(vrBlinkSeconds(code), code, 1).lidOpen == 1);
    CHECK(vrBlink(vrBlinkSeconds(code), code, 1).done);
    CHECK(vrBlinkSeconds(code) < 1.5f);
    bool coded = false;
    for (float t = 0; t < sw; t += .01f) coded |= vrBlink(t, code, 1).code > .5f;
    CHECK(coded == code);
  }
  // Calm: a short dip, the eyes stay open.
  CHECK(vrBlink(vrBlinkSwitch(true, 0), true, 0).lidOpen == 1);
  CHECK(vrBlink(vrBlinkSwitch(true, 0), true, 0).dark == Approx(1));
  CHECK(vrBlinkSeconds(true, 0) <= .4f);
}

TEST_CASE("neural vr: eyelids at a given openness match the wake's lids",
          "[unit][view][vr]") {
  const WakeState shut = eyelids(0);
  CHECK(shut.topEdge + shut.bottomEdge >= 1.0f);
  const WakeState open = eyelids(1);
  CHECK(open.topEdge == 0);
  CHECK(open.bottomEdge == 0);
  CHECK(open.curve == 0);
  // The wake's closed instant has the same lids.
  const WakeState w = wakeState(.1f, 1);
  CHECK(eyelids(w.lidOpen).topEdge == Approx(w.topEdge));
  CHECK(eyelids(w.lidOpen).bottomEdge == Approx(w.bottomEdge));
}

TEST_CASE("neural vr: the neural code is structured, organic and alive",
          "[unit][view][vr]") {
  const int n = kNeuralCodeModules;
  int lit = 0, changed = 0, runs = 0;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const bool on = neuralCodeModule(i, j, n, 2.0f);
      lit += on ? 1 : 0;
      changed += on != neuralCodeModule(i, j, n, 2.1f) ? 1 : 0;
      if (i > 0 && on != neuralCodeModule(i - 1, j, n, 2.0f)) ++runs;
    }
  const float fraction = static_cast<float>(lit) / static_cast<float>(n * n);
  CHECK(fraction > .3f);
  CHECK(fraction < .7f);
  // Organic, not noise: lit modules come in blobs and stripes several modules wide.
  CHECK(runs < n * n / 4);
  // Alive but slow: some modules change in a tenth of a second, most do not.
  CHECK(changed > 0);
  CHECK(changed < n * n / 10);
  // Deterministic.
  CHECK(neuralCodeModule(7, 9, n, 1.25f) == neuralCodeModule(7, 9, n, 1.25f));
  // QR-like: three finder squares in the corners (a lit ring around a dark gap around a
  // lit 3 x 3 core), the fourth corner is data.
  for (const auto& [ox, oy] :
       {std::pair{0, 0}, std::pair{n - 7, 0}, std::pair{0, n - 7}}) {
    CAPTURE(ox, oy);
    for (float t : {0.f, 1.f, 3.7f}) {
      CHECK(neuralCodeModule(ox, oy, n, t));
      CHECK(neuralCodeModule(ox + 6, oy + 6, n, t));
      CHECK_FALSE(neuralCodeModule(ox + 1, oy + 1, n, t));
      CHECK(neuralCodeModule(ox + 3, oy + 3, n, t));
    }
  }
}

TEST_CASE("neural vr: a relayout glides every board, nothing teleports",
          "[unit][view][vr]") {
  const Vec3 from{0, 0, 0}, to{10, -4, 2};
  CHECK(length(vrRelayoutPoint(from, to, 0, 1) - from) < 1e-5f);
  CHECK(length(vrRelayoutPoint(from, to, kVrRelayoutSeconds, 1) - to) < 1e-5f);
  CHECK(length(vrRelayoutPoint(from, to, 99, 1) - to) < 1e-5f);
  Vec3 prev = from;
  float top = 0;
  for (float t = .005f; t <= kVrRelayoutSeconds; t += .005f) {
    const Vec3 p = vrRelayoutPoint(from, to, t, 1);
    CHECK(length(p - prev) < .5f);
    top = std::max(top, p.z - (from.z + (to.z - from.z) * t / kVrRelayoutSeconds));
    prev = p;
  }
  CHECK(top > .3f);  // the boards rise as they glide
  CHECK(length(vrRelayoutPoint(from, from, .3f, 1) - from) < 1e-5f);  // still stays
  CHECK(length(vrRelayoutPoint(from, to, .1f, 0) - to) < 1e-5f);      // calm: placed
}

TEST_CASE("neural vr: a mode switches once behind the lids and locks input",
          "[unit][view][vr]") {
  VrSwitch mode;
  CHECK(mode.request(true, true, 10.0));
  CHECK(mode.active(10.1, 1));
  CHECK_FALSE(mode.shown);
  CHECK_FALSE(mode.advance(10.1, 1));
  CHECK(mode.advance(10.0 + static_cast<double>(vrBlinkSwitch(true)) + .001, 1));
  CHECK(mode.shown);
  CHECK_FALSE(mode.advance(10.0 + static_cast<double>(vrBlinkSwitch(true)) + .002, 1));
  CHECK(mode.active(10.0 + static_cast<double>(vrBlinkSwitch(true)) + .002, 1));
  CHECK_FALSE(mode.active(12, 1));
  CHECK(mode.request(false, false, 12));
  CHECK(mode.advance(12.2, 0));
  CHECK_FALSE(mode.shown);
  CHECK_FALSE(mode.active(12.4, 0));
  CHECK(mode.request(true, false, 13));
  mode.skip(13.1, 1);
  CHECK_FALSE(mode.active(13.1, 1));
  CHECK_FALSE(mode.shown);  // the caller applies the pending mode only in advance
  CHECK(mode.advance(13.1, 1));
  CHECK(mode.shown);
  CHECK_FALSE(mode.advance(13.2, 1));
}

TEST_CASE("neural vr: retargeting starts at the currently drawn positions",
          "[unit][view][vr]") {
  const std::vector<Placement> a{{0, 0, 0, 0, 0}, {1, 1, 0, 0, 0}};
  const std::vector<Placement> b{{0, 10, 0, 0, 0}, {1, 11, 0, 0, 0}};
  const std::vector<Placement> c{{0, 0, 10, 0, 0}, {1, 1, 10, 0, 0}};
  const auto halfway = vrRelayoutPlacements(a, b, .45f, 1);
  const auto start = vrRelayoutPlacements(halfway, c, 0, 1);
  REQUIRE(start.size() == halfway.size());
  for (std::size_t i = 0; i < start.size(); ++i) {
    CHECK(start[i].x == halfway[i].x);
    CHECK(start[i].y == halfway[i].y);
    CHECK(start[i].z == halfway[i].z);
  }
}
