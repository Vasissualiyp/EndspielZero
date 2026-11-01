// SPDX-License-Identifier: GPL-3.0-or-later
// Board presentation effects (M23.10): the per-cell fx records the boot writes, and the
// frames they produce on a real device.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <cmath>

#include "app/fx_timeline.hpp"
#include "app/session.hpp"
#include "render/board_fx.hpp"
#include "render/board_renderer.hpp"
#include "render/offscreen_target.hpp"
#include "render/vulkan_context.hpp"
#include "support/variants.hpp"
#include "view/board_boot.hpp"
#include "view/check_ripple.hpp"
#include "view/game_end_fx.hpp"
#include "view/layout.hpp"

using namespace cb;
using namespace cb::render;

namespace {
std::unique_ptr<app::Session> standardSession() {
  auto s = app::Session::create(test::loadVariant("standard"));
  REQUIRE(s.has_value());
  return std::move(*s);
}
CellId at(const app::Session& s, int file, int rank) {
  Coord c(s.variant().dims.dims());
  c.c[0] = static_cast<std::int16_t>(file);
  c.c[1] = static_cast<std::int16_t>(rank);
  return s.variant().dims.toCell(c);
}
}  // namespace

TEST_CASE("boot fx: near ranks are on while far ranks are dark", "[render][fx][boot]") {
  const auto s = standardSession();
  InstanceSet set;
  BoardFxState st;
  st.bootAge = 0.7f;
  applyBoardFx(set, *s, st);
  REQUIRE(set.cellFx.size() == s->snapshot().cellCount() + 1);
  const view::CellFx neutral;
  CHECK(set.cellFx[0].power == neutral.power);
  CHECK(set.cellFx[at(*s, 3, 0) + 1].power >= 1.0f);
  CHECK(set.cellFx[at(*s, 3, 7) + 1].power == view::kBootDarkLevel);
  // The far pieces are not there yet; the near ones are.
  CHECK(set.cellFx[at(*s, 3, 7) + 1].reveal == 0.0f);
  CHECK(set.cellFx[at(*s, 0, 0) + 1].reveal > 0.0f);
  // Empty cells never hold a reveal back.
  CHECK(set.cellFx[at(*s, 3, 4) + 1].reveal >= 1.0f);
  CHECK(set.fxGlobals.bezel == 1.0f);
  CHECK(set.fxGlobals.backdrop == 1.0f);  // past its ramp
  st.bootAge = 0.3f;
  applyBoardFx(set, *s, st);
  CHECK(set.fxGlobals.backdrop < 1.0f);
  CHECK(set.fxGlobals.bezel == 1.0f);
  st.bootAge = 0.1f;
  applyBoardFx(set, *s, st);
  CHECK(set.fxGlobals.bezel < 1.0f);
  // No state, no records: the renderer binds its neutral one.
  applyBoardFx(set, *s, BoardFxState{});
  CHECK(set.cellFx.empty());
}

TEST_CASE("boot fx: calm and non-LED boards take the calm form", "[render][fx][boot]") {
  const auto s = standardSession();
  InstanceSet set;
  BoardFxState st;
  st.bootAge = 0.2f;
  st.intensity = 0.0f;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 3, 0) + 1].power == set.cellFx[at(*s, 3, 7) + 1].power);
  CHECK(set.cellFx[at(*s, 3, 0) + 1].power <= 1.0f);
  st.intensity = 1.0f;
  st.ledCells = false;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 3, 0) + 1].power == set.cellFx[at(*s, 3, 7) + 1].power);
}

TEST_CASE("the timeline boots a new session, a reset and a console request, not an undo",
          "[app][fx][boot]") {
  auto s = standardSession();
  app::FxTimeline t;
  // Not eligible yet (an overture is playing): the boot waits.
  t.observe(s.get(), 1.0, false, false);
  CHECK_FALSE(t.booting(1.0));
  t.observe(s.get(), 2.0, true, false);
  REQUIRE(t.bootAge(2.5).has_value());
  CHECK(*t.bootAge(2.5) == 0.5f);
  CHECK_FALSE(t.booting(2.0 + static_cast<double>(view::kBootSeconds) + 0.01));
  // A click completes it.
  t.skipBoot();
  CHECK_FALSE(t.booting(2.1));
  // A move and its undo do not boot.
  REQUIRE(s->playMoveText("e2e4").has_value());
  app::Action undo;
  undo.kind = app::ActionKind::Undo;
  REQUIRE(s->apply(undo).has_value());
  t.observe(s.get(), 10.0, true, false);
  CHECK_FALSE(t.booting(10.1));
  // Reset and the console's request do.
  app::Action reset;
  reset.kind = app::ActionKind::Reset;
  REQUIRE(s->apply(reset).has_value());
  t.observe(s.get(), 20.0, true, false);
  CHECK(t.booting(20.1));
  t.skipBoot();
  s->requestBoot();
  t.observe(s.get(), 30.0, true, false);
  CHECK(t.booting(30.1));
  // The trailer swallows it.
  s->requestBoot();
  t.observe(s.get(), 40.0, true, true);
  CHECK_FALSE(t.booting(40.1));
  // A replacement session is new, whatever its serial.
  auto s2 = standardSession();
  t.observe(s2.get(), 50.0, true, false);
  CHECK(t.booting(50.1));
}

TEST_CASE("boot frames: dark at the start, lit rank by rank, identical once settled",
          "[render][gpu][fx][boot]") {
  auto ctx = VulkanContext::create();
  if (!ctx.has_value()) SKIP("no Vulkan device");
  auto target = OffscreenTarget::create(*ctx, 320, 200);
  auto renderer = BoardRenderer::create(*ctx);
  REQUIRE(target.has_value());
  REQUIRE(renderer.has_value());
  renderer->setTheme(view::Theme::manifold());
  const auto s = standardSession();
  const view::OrbitCamera cam = view::OrbitCamera::frame(
      view::boundsOf(view::layout(s->variant().dims, s->viewConfig())));

  const auto shot = [&](std::optional<float> age) {
    InstanceSet set = renderer->buildInstances(s->snapshot(), s->viewConfig());
    BoardFxState st;
    st.bootAge = age;
    applyBoardFx(set, *s, st);
    REQUIRE(renderer->render(*target, set, cam).has_value());
    auto px = target->readPixels();
    REQUIRE(px.has_value());
    return std::move(*px);
  };
  const auto mean = [](const std::vector<std::uint8_t>& px) {
    double sum = 0;
    for (std::size_t i = 0; i < px.size(); i += 4) sum += px[i] + px[i + 1] + px[i + 2];
    return sum / static_cast<double>(px.size() / 4);
  };
  const auto none = shot(std::nullopt);
  const auto start = shot(0.0f);
  const auto half = shot(0.5f);
  const auto settled = shot(view::kBootSeconds);
  CHECK(mean(start) < mean(half));
  CHECK(mean(half) < mean(none));
  REQUIRE(settled.size() == none.size());
  int worst = 0;
  for (std::size_t i = 0; i < none.size(); ++i)
    worst = std::max(worst, std::abs(static_cast<int>(none[i]) - static_cast<int>(settled[i])));
  CHECK(worst <= 2);
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("check ripple fx: records follow the wave and nothing else", "[render][fx][ripple]") {
  const auto s = standardSession();
  InstanceSet set;
  BoardFxState st;
  st.rippleOrigin = at(*s, 0, 2);
  st.rippleAge = 2.0f / view::kRippleCellsPerSecond;
  applyBoardFx(set, *s, st);
  REQUIRE(set.cellFx.size() == s->snapshot().cellCount() + 1);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp > 0.3f);       // two steps away: at its peak
  CHECK(set.cellFx[at(*s, 2, 2) + 1].tintMix > 0.0f);
  CHECK(set.cellFx[at(*s, 6, 2) + 1].rippleAmp == 0.0f);      // six steps: not yet
  CHECK(set.cellFx[at(*s, 6, 2) + 1].power == 1.0f);          // light untouched
  CHECK(set.fxGlobals.tint[0] == st.tint[0]);
  st.intensity = 0.0f;                                         // calm: no ripple
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp == 0.0f);
  st.intensity = 1.0f;
  st.ledCells = false;                                         // non-LED: none either
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp == 0.0f);
}

TEST_CASE("end fx: the wave, the hold and the sides of a finished game", "[render][fx][endfx]") {
  auto s = standardSession();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) REQUIRE(s->playMoveText(m).has_value());
  REQUIRE(s->endEvent().has_value());
  InstanceSet set;
  BoardFxState st;
  st.end = *s->endEvent();
  st.endAge = 3.0f;
  applyBoardFx(set, *s, st);
  const CellId king = at(*s, 4, 0);
  CHECK(set.cellFx[king + 1].power == 1.0f);                              // the mated king
  CHECK(set.cellFx[at(*s, 0, 4) + 1].power == view::kDecisiveHold);       // an empty far cell
  CHECK(set.cellFx[at(*s, 0, 4) + 1].glitch == 0.0f);
  // White lost: its pieces destabilise, black's are solid and brighter.
  CHECK(set.cellFx[at(*s, 0, 0) + 1].opacity == Catch::Approx(view::kLoserOpacity).margin(0.05));
  CHECK(set.cellFx[at(*s, 0, 7) + 1].opacity == 1.0f);
  CHECK(set.cellFx[at(*s, 0, 7) + 1].gain == Catch::Approx(view::kWinnerGain));
  // At the start of the wave only the origin has been reached.
  st.endAge = 0.05f;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 4, 1) + 1].power == 1.0f);   // one step, arrives after 0.11 s
  CHECK(set.cellFx[at(*s, 4, 0) + 1].power == 1.0f);   // the mated king keeps its light
  // A draw dims every cell alike and leaves both armies at 0.85.
  st.end.kind = view::EndKind::Draw;
  st.endAge = 3.0f;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 0, 4) + 1].power == view::kDrawHold);
  CHECK(set.cellFx[at(*s, 0, 0) + 1].opacity == Catch::Approx(view::kDrawOpacity));
  CHECK(set.cellFx[at(*s, 0, 7) + 1].opacity == Catch::Approx(view::kDrawOpacity));
  // The power-up brings it back.
  st.endAge.reset();
  st.powerUpAge = view::kPowerUpSeconds;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 0, 4) + 1].power == 1.0f);
  CHECK(set.cellFx[at(*s, 0, 0) + 1].opacity == 1.0f);
}

namespace {
// Mean channel value of a (2r+1)-pixel square around a world point.
double windowMean(const std::vector<std::uint8_t>& px, int w, int h,
                  const view::OrbitCamera& cam, const view::Vec3& p, int r = 3) {
  const auto sp = cam.project(p, static_cast<float>(w) / static_cast<float>(h),
                              static_cast<float>(w), static_cast<float>(h));
  REQUIRE(sp.visible);
  double sum = 0;
  int n = 0;
  for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx) {
      const int x = static_cast<int>(sp.x) + dx, y = static_cast<int>(sp.y) + dy;
      if (x < 0 || y < 0 || x >= w || y >= h) continue;
      const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                             static_cast<std::size_t>(x)) * 4;
      sum += px[i] + px[i + 1] + px[i + 2];
      ++n;
    }
  return sum / n;
}
}  // namespace

TEST_CASE("ripple and end frames: the wave reaches a near cell, not a far one; the hold dims",
          "[render][gpu][fx][ripple][endfx]") {
  auto ctx = VulkanContext::create();
  if (!ctx.has_value()) SKIP("no Vulkan device");
  constexpr int W = 640, H = 400;
  auto target = OffscreenTarget::create(*ctx, W, H);
  auto renderer = BoardRenderer::create(*ctx);
  REQUIRE(target.has_value());
  REQUIRE(renderer.has_value());
  renderer->setTheme(view::Theme::manifold());
  auto s = standardSession();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) REQUIRE(s->playMoveText(m).has_value());
  const auto placements = view::layout(s->variant().dims, s->viewConfig());
  const view::OrbitCamera cam = view::OrbitCamera::frame(view::boundsOf(placements));
  const auto where = [&](CellId c, float dx = 0.0f) {
    for (const auto& p : placements)
      if (p.cell == c) return view::Vec3{p.x + dx, p.y, p.z};
    return view::Vec3{};
  };
  const auto shot = [&](const BoardFxState& st) {
    InstanceSet set = renderer->buildInstances(s->snapshot(), s->viewConfig());
    applyBoardFx(set, *s, st);
    REQUIRE(renderer->render(*target, set, cam).has_value());
    auto px = target->readPixels();
    REQUIRE(px.has_value());
    return std::move(*px);
  };
  const auto base = shot(BoardFxState{});

  BoardFxState rip;
  rip.rippleOrigin = at(*s, 0, 2);
  rip.rippleAge = 2.0f / view::kRippleCellsPerSecond;
  const auto rippled = shot(rip);
  const CellId nearCell = at(*s, 2, 2), farCell = at(*s, 6, 2);
  CHECK(std::abs(windowMean(rippled, W, H, cam, where(nearCell)) -
                 windowMean(base, W, H, cam, where(nearCell))) > 1.0);
  CHECK(std::abs(windowMean(rippled, W, H, cam, where(farCell)) -
                 windowMean(base, W, H, cam, where(farCell))) < 1.0);

  BoardFxState end;
  end.end = *s->endEvent();
  end.endAge = 3.0f;
  const auto held = shot(end);
  const CellId king = at(*s, 4, 0), far = at(*s, 0, 4);
  // The mated king's own cell (beside the piece) keeps its light; a far cell is at ~30 %.
  const double kingRatio = windowMean(held, W, H, cam, where(king, 0.38f), 2) /
                           windowMean(base, W, H, cam, where(king, 0.38f), 2);
  const double farRatio =
      windowMean(held, W, H, cam, where(far)) / windowMean(base, W, H, cam, where(far));
  CHECK(kingRatio > 0.9);
  CHECK(farRatio > 0.22);
  CHECK(farRatio < 0.38);
  CHECK(ctx->takeValidationMessages().empty());
}
#endif
