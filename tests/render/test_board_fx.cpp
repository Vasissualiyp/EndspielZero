// SPDX-License-Identifier: GPL-3.0-or-later
// Board presentation effects (M23.10): the per-cell fx records the boot writes, and the
// frames they produce on a real device.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <cmath>
#include <cstring>

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
#include "view/piece_tint.hpp"

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

TEST_CASE("shutdown fx: pieces stay before failure, then all go and standby remains",
          "[render][fx][endfx]") {
  const auto s = standardSession();
  BoardFxState st;
  st.end = {view::EndKind::Decisive, Color::White,
            s->game().position().findRoyal(Color::White), true};
  st.winnerColor[0] = 0.2f;
  st.winnerColor[1] = 0.7f;
  st.winnerColor[2] = 0.9f;
  InstanceSet set;
  st.endAge = 3.5f;
  applyBoardFx(set, *s, st);
  for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
    if (s->snapshot().at(c).empty()) continue;
    CHECK(set.cellFx[c + 1].opacity > 0.5f);
    CHECK(set.cellFx[c + 1].reveal >= 1.0f);
  }
  for (float intensity : {0.0f, 1.0f}) {
    st.intensity = intensity;
    for (float age : {60.0f, 3600.0f}) {
      st.endAge = age;
      applyBoardFx(set, *s, st);
      for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
        const auto& fx = set.cellFx[c + 1];
        if (!s->snapshot().at(c).empty())
          CHECK((fx.opacity == 0.0f || fx.reveal == 0.0f));
        else
          CHECK(fx.reveal == 1.0f);
        if (intensity == 0.0f) CHECK(fx.heat == 0.0f);
      }
      CHECK(set.cellFx[st.end.origin + 1].power == Catch::Approx(view::kDecisiveHold));
      CHECK(set.fxGlobals.standbyCell == s->game().position().findRoyal(Color::Black));
      CHECK(set.fxGlobals.standby >= 0.35f);
      CHECK(set.fxGlobals.standby <= 1.0f);
      for (int i = 0; i < 3; ++i)
        CHECK(set.fxGlobals.standbyColor[i] == st.winnerColor[i]);
    }
  }
}

TEST_CASE("shutdown fx: winner king collapses last after standing alone",
          "[render][fx][endfx]") {
  const auto s = standardSession();
  BoardFxState st;
  st.end = {view::EndKind::Decisive, Color::White,
            s->game().position().findRoyal(Color::White), true};
  const auto& dist = s->distancesFrom(st.end.origin);
  std::vector<view::ShutdownPiece> pieces;
  CellId farPawn = at(*s, 0, 1);
  for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
    const auto p = s->snapshot().at(c);
    if (!p.empty())
      pieces.push_back({p.colorOf(), s->variant().pieces[p.type].royal, dist[c], c});
    if (c >= 8 && c < 16 && dist[c] > dist[farPawn]) farPawn = c;
  }
  const float first =
      view::shutdownBegins(st.end.kind, *std::max_element(dist.begin(), dist.end()), 1);
  const auto starts = view::shutdownStarts(st.end, pieces, first, 1);
  const CellId winner = s->game().position().findRoyal(Color::Black);
  const auto index = static_cast<std::size_t>(
      std::find_if(pieces.begin(), pieces.end(),
                   [&](const auto& p) { return p.cell == winner; }) -
      pieces.begin());
  InstanceSet set;
  st.endAge = starts[index] - 0.05f;
  applyBoardFx(set, *s, st);
  for (const auto& p : pieces) {
    const auto& fx = set.cellFx[p.cell + 1];
    if (p.cell == winner)
      CHECK(fx.reveal == 1.0f);
    else
      CHECK((fx.reveal == 0.0f || fx.opacity == 0.0f));
  }
  st.endAge = starts[index] + 0.3f;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[winner + 1].reveal > 0.0f);
  CHECK(set.cellFx[winner + 1].reveal < 1.0f);
  CHECK(set.cellFx[farPawn + 1].reveal == 0.0f);
}

TEST_CASE("shutdown fx: review masks the end, leaving it returns to the current age",
          "[render][fx][endfx]") {
  const auto s = standardSession();
  BoardFxState st;
  st.end = {view::EndKind::Decisive, Color::White,
            s->game().position().findRoyal(Color::White), true};
  st.endAge = 60.0f;
  st.reviewing = true;
  InstanceSet set;
  applyBoardFx(set, *s, st);
  const view::CellFx neutral;
  for (const auto& fx : set.cellFx) CHECK(std::memcmp(&fx, &neutral, sizeof(fx)) == 0);
  CHECK(set.fxGlobals.standbyCell == kInvalidCell);
  CHECK_FALSE(set.standbyLed.has_value());
  st.reviewing = false;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[st.end.origin + 1].reveal == 0.0f);
  CHECK(set.fxGlobals.standbyCell != kInvalidCell);
}

TEST_CASE(
    "shutdown fx: compressed hold collapses together without standby; undo restores",
    "[render][fx][endfx]") {
  const auto s = standardSession();
  BoardFxState st;
  st.end = {view::EndKind::Decisive, Color::White,
            s->game().position().findRoyal(Color::White), false};
  st.endAge = 60.0f;
  st.compressedShutdown = true;
  InstanceSet set;
  for (float intensity : {0.0f, 1.0f}) {
    st.intensity = intensity;
    for (float progress : {0.3f, 0.97f}) {
      st.holdProgress = progress;
      applyBoardFx(set, *s, st);
      CHECK(set.fxGlobals.standbyCell == kInvalidCell);
      for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
        if (s->snapshot().at(c).empty()) continue;
        const auto& fx = set.cellFx[c + 1];
        if (progress < 0.4f)
          CHECK(fx.reveal >= 1.0f);
        else
          CHECK((fx.opacity == 0.0f || fx.reveal == 0.0f));
      }
    }
  }
  st.endAge.reset();
  st.compressedShutdown = false;
  st.intensity = 1.0f;
  for (float age : {0.0f, 0.5f}) {
    st.powerUpAge = age;
    applyBoardFx(set, *s, st);
    CHECK(set.fxGlobals.standbyCell == kInvalidCell);
    for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
      if (s->snapshot().at(c).empty()) continue;
      const auto& fx = set.cellFx[c + 1];
      CHECK(fx.reveal == (age == 0.0f ? 0.0f : 1.0f));
      if (age == 0.5f) CHECK(fx.opacity == 1.0f);
    }
  }
}

TEST_CASE(
    "shutdown standby: one neutral glow at the winner's slab corner, none on a shape",
    "[render][gpu][fx][endfx]") {
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  auto target = OffscreenTarget::create(*ctx, 320, 200);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  renderer->setTheme(view::Theme::manifold());
  for (const char* name : {"standard", "torus"}) {
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    BoardOptions options;
    options.surface = std::string_view(name) == "torus";
    renderer->setOptions(options);
    BoardFxState st;
    st.end = {view::EndKind::Decisive, Color::White,
              (*s)->game().position().findRoyal(Color::White), true};
    const auto placements = view::layout((*s)->variant().dims, (*s)->viewConfig());
    const auto cam = view::OrbitCamera::frame(view::boundsOf(placements));
    auto set = renderer->buildInstances((*s)->snapshot(), (*s)->viewConfig());
    const auto before = set.instances.size();
    st.endAge = 3.5f;
    applyBoardFx(set, **s, st);
    CHECK_FALSE(set.standbyLed.has_value());
    st.endAge = 60.0f;
    applyBoardFx(set, **s, st);
    CHECK(set.size() == before + (options.surface ? 0 : 1));
    if (!options.surface) {
      REQUIRE(set.standbyLed.has_value());
      const auto& led = *set.standbyLed;
      CHECK(led.material == 5.0f);
      CHECK(led.fxCell == 0.0f);
      const auto cell = set.fxGlobals.standbyCell;
      const auto pl = std::find_if(placements.begin(), placements.end(),
                                   [&](const auto& p) { return p.cell == cell; });
      REQUIRE(pl != placements.end());
      // The king is in the middle of its home rank: the nearest *corner* is 3.5 cells
      // away in x. The specified half-cell offset pins that corner unambiguously.
      CHECK(led.center[0] == Catch::Approx(7.5f));
      CHECK(led.center[1] == Catch::Approx(7.5f));
      CHECK(led.center[2] ==
            Catch::Approx(pl->z - BoardRenderer::cellHalfExtent().z + 0.02f));
      REQUIRE(renderer->render(*target, set, cam).has_value());
    } else
      CHECK_FALSE(set.standbyLed.has_value());
  }
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("seam hover brightens one axis and dims the other", "[render][seams]") {
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  REQUIRE(renderer.has_value());
  for (const auto& theme : {view::Theme::console(), view::Theme::manifold()}) {
    renderer->setTheme(theme);
    for (const char* name : {"klein", "mirrorbox"}) {
      CAPTURE(name);
      auto s = app::Session::create(test::loadVariant(name));
      REQUIRE(s.has_value());
      const auto build = [&](int axis) {
        BoardOptions options;
        options.seamHighlightAxis = axis;
        renderer->setOptions(options);
        return renderer->buildInstances((*s)->snapshot(), (*s)->viewConfig(),
                                        &(*s)->seams());
      };
      const auto base = build(-1);
      for (int axis : {0, 1}) {
        const auto hover = build(axis);
        REQUIRE(base.instances.size() == hover.instances.size());
        std::size_t changed = 0;
        for (std::size_t i = 0; i < base.instances.size(); ++i) {
          const auto& a = base.instances[i];
          const auto& b = hover.instances[i];
          Instance restored = b;
          std::copy_n(a.color, 4, restored.color);
          CHECK(std::memcmp(&a, &restored, sizeof(Instance)) == 0);
          if (std::memcmp(a.color, b.color, sizeof(a.color)) == 0) continue;
          ++changed;
          const int railAxis = a.scale[0] < a.scale[1] ? 0 : 1;
          const float gain = railAxis == axis ? kSeamHoverGain : kSeamHoverDim;
          for (int c = 0; c < 3; ++c)
            CHECK(b.color[c] == Catch::Approx(a.color[c] * gain));
          CHECK(b.color[3] == a.color[3]);
        }
        if (std::string_view(name) == "mirrorbox") {
          CHECK(changed == 0);
          CHECK(std::memcmp(base.instances.data(), hover.instances.data(),
                            base.instances.size() * sizeof(Instance)) == 0);
        } else {
          CHECK(changed == (*s)->seams().faces().size());
        }
      }
    }
  }
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("shutdown calm frames: zero opacity leaves no piece fragments in either theme",
          "[render][gpu][fx][endfx]") {
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  auto target = OffscreenTarget::create(*ctx, 320, 200);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  const auto s = standardSession();
  const auto cam = view::OrbitCamera::frame(
      view::boundsOf(view::layout(s->variant().dims, s->viewConfig())));
  for (const auto& theme : {view::Theme::console(), view::Theme::manifold()}) {
    renderer->setTheme(theme);
    auto set = renderer->buildInstances(s->snapshot(), s->viewConfig());
    BoardFxState st;
    st.end = {view::EndKind::Decisive, Color::White,
              s->game().position().findRoyal(Color::White), true};
    st.endAge = 60;
    st.intensity = 0;
    applyBoardFx(set, *s, st);
    // Isolate the projections and their footprints from the independent shadow pass.
    set.shadows.clear();
    for (auto& batch : set.shadowBatches) batch.count = 0;
    REQUIRE(renderer->render(*target, set, cam).has_value());
    const auto faded = target->readPixels();
    REQUIRE(faded.has_value());
    for (auto& fx : set.cellFx) fx.reveal = 0;
    REQUIRE(renderer->render(*target, set, cam).has_value());
    const auto absent = target->readPixels();
    REQUIRE(absent.has_value());
    CHECK(std::equal(faded->begin(), faded->end(), absent->begin(), absent->end()));
  }
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("shutdown hold: travelling handoff copies share their piece's shutdown record",
          "[render][fx][endfx]") {
  BoardRenderer renderer;
  renderer.setTheme(view::Theme::manifold());
  for (const bool surface : {false, true}) {
    auto s = app::Session::create(test::loadVariant(surface ? "torus" : "standard"));
    REQUIRE(s.has_value());
    REQUIRE((*s)->playMoveText(surface ? "e2e3" : "e2e4").has_value());
    (*s)->setMoveProgress(0.3f);
    REQUIRE((*s)->animation().active());
    BoardOptions options;
    options.surface = surface;
    renderer.setOptions(options);
    auto set = renderer.buildInstances((*s)->snapshot(), (*s)->viewConfig(),
                                       &(*s)->seams(), &(*s)->animation());
    BoardFxState st;
    st.end = {view::EndKind::Decisive, Color::White, 0, false};
    st.endAge = 60;
    st.compressedShutdown = true;
    st.holdProgress = 0.97f;
    applyBoardFx(set, **s, st);
    std::size_t projections = 0;
    for (const auto* group : {&set.instances, &set.flourish})
      for (const auto& in : *group) {
        if (static_cast<int>(in.material) != 4) continue;
        ++projections;
        REQUIRE(in.fxCell > 0);
        const auto index = static_cast<std::size_t>(in.fxCell) / 2;
        REQUIRE(index < set.cellFx.size());
        CHECK(set.cellFx[index].reveal == 0);
      }
    CHECK(projections >= 32);
  }
}

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
    worst = std::max(worst,
                     std::abs(static_cast<int>(none[i]) - static_cast<int>(settled[i])));
  CHECK(worst <= 2);
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("check ripple fx: records follow the wave and nothing else",
          "[render][fx][ripple]") {
  const auto s = standardSession();
  InstanceSet set;
  BoardFxState st;
  st.rippleOrigin = at(*s, 0, 2);
  st.rippleAge = 2.0f / view::kRippleCellsPerSecond;
  applyBoardFx(set, *s, st);
  REQUIRE(set.cellFx.size() == s->snapshot().cellCount() + 1);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp > 0.3f);  // two steps away: at its peak
  CHECK(set.cellFx[at(*s, 2, 2) + 1].tintMix > 0.0f);
  CHECK(set.cellFx[at(*s, 6, 2) + 1].rippleAmp == 0.0f);  // six steps: not yet
  CHECK(set.cellFx[at(*s, 6, 2) + 1].power == 1.0f);      // light untouched
  CHECK(set.fxGlobals.tint[0] == st.tint[0]);
  st.intensity = 0.0f;  // calm: no ripple
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp == 0.0f);
  st.intensity = 1.0f;
  st.ledCells = false;  // non-LED: none either
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 2, 2) + 1].rippleAmp == 0.0f);
}

TEST_CASE("end fx: the wave, the hold and the sides of a finished game",
          "[render][fx][endfx]") {
  auto s = standardSession();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"})
    REQUIRE(s->playMoveText(m).has_value());
  REQUIRE(s->endEvent().has_value());
  InstanceSet set;
  BoardFxState st;
  st.end = *s->endEvent();
  // Pin this wave/hold test before the new compressed collapse starts (M23.12 §2).
  st.compressedShutdown = true;
  st.holdProgress = 0.3f;
  st.endAge = 9.0f;
  applyBoardFx(set, *s, st);
  const CellId king = at(*s, 4, 0);
  CHECK(set.cellFx[king + 1].power == 1.0f);                         // the mated king
  CHECK(set.cellFx[at(*s, 0, 4) + 1].power == view::kDecisiveHold);  // an empty far cell
  CHECK(set.cellFx[at(*s, 0, 4) + 1].glitch == 0.0f);
  // White lost: its pieces destabilise, black's are solid and brighter.
  CHECK(set.cellFx[at(*s, 0, 0) + 1].opacity ==
        Catch::Approx(view::kLoserOpacity).margin(0.05));
  CHECK(set.cellFx[at(*s, 0, 7) + 1].opacity == 1.0f);
  CHECK(set.cellFx[at(*s, 0, 7) + 1].gain == Catch::Approx(view::kWinnerGain));
  // Inside the delay the board is untouched, losers included (age counts from the
  // decision).
  for (float age : {0.0f, 1.0f, 2.9f}) {
    st.endAge = age;
    applyBoardFx(set, *s, st);
    for (CellId c = 0; c < s->snapshot().cellCount(); ++c) {
      CHECK(set.cellFx[c + 1].power == 1.0f);
      CHECK(set.cellFx[c + 1].glitch == 0.0f);
      CHECK(set.cellFx[c + 1].opacity == 1.0f);
    }
  }
  // Then the wave: just after the delay only the origin has been reached, and the mated
  // king keeps its light throughout.
  st.endAge = view::kEndDelaySeconds + 0.1f;
  applyBoardFx(set, *s, st);
  CHECK(set.cellFx[at(*s, 4, 0) + 1].power == 1.0f);
  CHECK(set.cellFx[at(*s, 4, 4) + 1].power == 1.0f);  // four steps: 1.3 s later
  CHECK(set.cellFx[at(*s, 4, 1) + 1].power < 1.0f + 1e-6f);
  // A draw dims every cell alike and leaves both armies at 0.85.
  st.end.kind = view::EndKind::Draw;
  st.endAge = 9.0f;
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
                             static_cast<std::size_t>(x)) *
                            4;
      sum += px[i] + px[i + 1] + px[i + 2];
      ++n;
    }
  return sum / n;
}
}  // namespace

TEST_CASE(
    "ripple and end frames: the wave reaches a near cell, not a far one; the hold dims",
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
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"})
    REQUIRE(s->playMoveText(m).has_value());
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
  // Preserve this test's original wave-only picture, before the compressed collapse.
  end.compressedShutdown = true;
  end.holdProgress = 0.3f;
  end.endAge = 9.0f;
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

TEST_CASE("a slab that just arrived boots on its own clock (M24.6)",
          "[render][fx][rig]") {
  auto s = app::Session::create(test::loadVariant("standard"));
  REQUIRE(s.has_value());
  const auto count = (*s)->snapshot().cellCount();
  BoardFxState st;
  st.cellBootAge.assign(count, -1.0f);
  // The first rank's cells are a slab that has just locked: dark at age 0.
  for (CellId c = 0; c < 8; ++c) st.cellBootAge[c] = 0.0f;
  InstanceSet set;
  applyBoardFx(set, **s, st);
  REQUIRE(set.cellFx.size() == count + 1);
  for (CellId c = 0; c < count; ++c) {
    CAPTURE(c);
    if (c < 8)
      CHECK(set.cellFx[c + 1].power < 0.5f);
    else
      CHECK(set.cellFx[c + 1].power == 1.0f);
  }
  // Booted after the full boot; the short boot is done in 0.4 s.
  for (CellId c = 0; c < 8; ++c) st.cellBootAge[c] = view::kBootSeconds;
  applyBoardFx(set, **s, st);
  CHECK(set.cellFx[1].power == Catch::Approx(1.0f));
  st.shortBoot = true;
  for (CellId c = 0; c < 8; ++c) st.cellBootAge[c] = kShortBootSeconds;
  applyBoardFx(set, **s, st);
  CHECK(set.cellFx[1].power == Catch::Approx(1.0f));
  for (CellId c = 0; c < 8; ++c) st.cellBootAge[c] = 0.0f;
  applyBoardFx(set, **s, st);
  CHECK(set.cellFx[1].power < 0.5f);
}
