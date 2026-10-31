// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

#include "app/session.hpp"
#include "io/fen.hpp"
#include "io/notation.hpp"
#include "render/panel_response.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Approx;

namespace {
struct ResponseFixture {
  PanelResponse response;
  InstanceSet set;
  std::vector<PanelTarget> targets;
  ResponseFixture() {
    set.instances.resize(3);
    for (auto& i : set.instances) {
      i.panelSignal[0] = .125f;
      i.panelSignal[2] = 1;
    }
    targets = {{4, view::Rgba{.2f, .3f, .4f, 1}, PanelDestination::Instance, 1, 1},
               {9, view::Rgba{.4f, .3f, .2f, 1}, PanelDestination::Instance, 2, 1}};
  }
  float sample(double t) {
    response.apply(set, targets, t);
    CHECK(set.instances[0].panelSignal[1] == 0);  // an unrelated piece/rail
    return set.instances[1].panelSignal[1];
  }
};
CellId namedCell(const app::Session& s, const char* name) {
  const auto c = parseCell(s.variant().dims, name);
  REQUIRE(c.has_value());
  return *c;
}
float gain(const InstanceSet& set, const PanelTarget& target) {
  return target.destination == PanelDestination::Instance
             ? set.instances[target.first].panelSignal[1]
             : set.surfaceVertices[target.first].panelSignal[1];
}
}  // namespace

TEST_CASE("LCD response follows real colour events and explicit time",
          "[render][panel-response]") {
  ResponseFixture f;
  CHECK(f.sample(1) == 0);  // first exposure is settled
  f.targets[0].color.r = .6f;
  CHECK(f.sample(2) == Approx(.018f));
  CHECK(f.sample(2) == Approx(.018f));
  CHECK(f.sample(2.06) == Approx(.0151875f));
  CHECK(f.sample(2.12) == Approx(.009f));
  CHECK(f.set.instances[2].panelSignal[1] == 0);
  CHECK(f.sample(2.24) == 0);
  CHECK(f.sample(10) == 0);
  f.targets[0].color.b = .8f;
  CHECK(f.sample(10.1) == Approx(.018f));
  CHECK(f.sample(10.2) < .018f);
  f.targets[0].color.g = .7f;
  CHECK(f.sample(10.2) == Approx(.018f));  // newest event replaces the old one
  CHECK(f.sample(10.44) == Approx(0).margin(1e-7));
}

TEST_CASE("LCD history ignores alpha and tiny changes, and forgets absent cells",
          "[render][panel-response]") {
  ResponseFixture f;
  CHECK(f.sample(0) == 0);
  f.targets[0].color.a = .2f;
  CHECK(f.sample(1) == 0);
  f.targets[0].color.r += .5f / 255;
  CHECK(f.sample(2) == 0);
  f.targets[0].color.r += 2.0f / 255;
  CHECK(f.sample(3) == Approx(.018f));
  f.response.clear();
  CHECK(f.sample(3) == 0);
  f.targets[0].color.r += .1f;
  CHECK(f.sample(4) > 0);
  CHECK(f.sample(3.5) == 0);  // rewind starts settled
  const auto missing = f.targets.front();
  f.targets.erase(f.targets.begin());
  f.sample(4);
  f.targets.insert(f.targets.begin(), missing);
  f.targets.front().color.r += .1f;
  CHECK(f.sample(4.1) == 0);
  CHECK(f.sample(std::numeric_limits<double>::quiet_NaN()) == 0);
}

TEST_CASE("extra no-change redraws do not alter LCD event timing",
          "[render][panel-response]") {
  ResponseFixture sparse, dense;
  sparse.sample(0);
  dense.sample(0);
  sparse.targets[0].color.r = dense.targets[0].color.r = .8f;
  sparse.sample(1);
  dense.sample(1);
  for (const double t : {1.01, 1.04, 1.09, 1.10}) dense.sample(t);
  CHECK(sparse.sample(1.12) == dense.sample(1.12));
  CHECK(sparse.sample(1.24) == dense.sample(1.24));
}

TEST_CASE("duplicate patches observe one cell event and do not light their sidewalls",
          "[render][panel-response]") {
  ResponseFixture f;
  f.set.surfaceVertices.resize(6);
  for (const auto i : {0u, 1u, 3u, 4u}) {
    f.set.surfaceVertices[i].panelSignal[0] = .125f;
    f.set.surfaceVertices[i].panelSignal[2] = 1;
  }
  f.targets = {{4, {.2f, .3f, .4f, 1}, PanelDestination::Surface, 0, 3},
               {4, {.2f, .3f, .4f, 1}, PanelDestination::Surface, 3, 3}};
  f.response.apply(f.set, f.targets, 0);
  f.targets[0].color.r = f.targets[1].color.r = .8f;
  f.response.apply(f.set, f.targets, 1);
  for (const auto i : {0u, 1u, 3u, 4u})
    CHECK(f.set.surfaceVertices[i].panelSignal[1] == Approx(.018f));
  CHECK(f.set.surfaceVertices[2].panelSignal[1] == 0);
  CHECK(f.set.surfaceVertices[5].panelSignal[1] == 0);
}

TEST_CASE("panel metadata follows real flat and curved cell colours and final indices",
          "[render][panel-response]") {
  for (const char* variant : {"standard", "torus", "klein"}) {
    auto session = app::Session::create(test::loadVariant(variant));
    REQUIRE(session.has_value());
    auto& s = **session;
    const auto theme = view::Theme::manifold();
    BoardRenderer renderer;
    renderer.setTheme(theme);
    std::vector<PanelTarget> flatTargets;
    const auto flat = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams(),
                                              nullptr, {}, {}, {}, &flatTargets);
    const auto plain = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    REQUIRE(flat.instances.size() == plain.instances.size());
    CHECK(std::memcmp(flat.instances.data(), plain.instances.data(),
                      flat.instances.size() * sizeof(Instance)) == 0);
    REQUIRE(flatTargets.size() == s.variant().dims.cellCount());
    for (const auto& target : flatTargets) {
      REQUIRE(target.destination == PanelDestination::Instance);
      REQUIRE(target.first < flat.instances.size());
      const auto& instance = flat.instances[target.first];
      CHECK(instance.material == 7);
      CHECK(instance.color[0] == target.color.r);
      CHECK(instance.color[1] == target.color.g);
      const auto& placements = s.placements();
      const auto found =
          std::find_if(placements.begin(), placements.end(),
                       [&](const auto& p) { return p.cell == target.cell; });
      REQUIRE(found != placements.end());
      CHECK(instance.center[0] == found->x);
      CHECK(instance.center[1] == found->y);
    }
#ifdef CB_HAVE_IMGUI
    if (std::string_view(variant) != "standard") {
      auto options = renderer.options();
      options.surface = true;
      renderer.setOptions(options);
      std::vector<PanelTarget> targets;
      auto set = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams(),
                                         nullptr, {}, {}, {}, &targets);
      REQUIRE_FALSE(targets.empty());
      std::size_t covered = 0;
      for (const auto& target : targets) {
        CHECK(target.destination == PanelDestination::Surface);
        CHECK(target.first == covered);
        covered += target.count;
        REQUIRE(covered <= set.surfaceVertices.size());
        const auto match =
            std::find_if(flatTargets.begin(), flatTargets.end(),
                         [&](const auto& t) { return t.cell == target.cell; });
        REQUIRE(match != flatTargets.end());
        CHECK(target.color.array() == match->color.array());
      }
      CHECK(covered == set.surfaceVertices.size());
    }
#endif
    renderer.setTheme(view::Theme::console());
    (void)renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams(), nullptr, {},
                                  {}, {}, &flatTargets);
    CHECK(flatTargets.empty());
  }
}

TEST_CASE("real selection, move, hint and hover targets drive LCD responses",
          "[render][panel-response]") {
  for (const bool surface : {false, true}) {
    auto session =
        app::Session::create(test::loadVariant(surface ? "cylinder" : "standard"));
    REQUIRE(session.has_value());
    auto& s = **session;
    BoardRenderer renderer;
    renderer.setTheme(view::Theme::manifold());
    auto options = renderer.options();
    options.surface = surface;
    renderer.setOptions(options);
    PanelResponse response;
    std::vector<PanelTarget> targets;
    const auto frame = [&](double t) {
      REQUIRE(response.prepare(&s, renderer.theme(), renderer.options()));
      renderer.setLastMove(kInvalidCell, kInvalidCell);
      const auto& moves = s.game().moveHistory();
      if (!moves.empty()) renderer.setLastMove(moves.back().from, moves.back().to);
      auto set = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams(),
                                         nullptr, {}, {}, {}, &targets);
      const auto raw = set;
      response.apply(set, targets, t);
      auto restored = set;
      for (auto& instance : restored.instances) instance.panelSignal[1] = 0;
      for (auto& vertex : restored.surfaceVertices) vertex.panelSignal[1] = 0;
      CHECK(std::memcmp(raw.instances.data(), restored.instances.data(),
                        raw.instances.size() * sizeof(Instance)) == 0);
      if (!raw.surfaceVertices.empty()) {
        CHECK(std::memcmp(raw.surfaceVertices.data(), restored.surfaceVertices.data(),
                          raw.surfaceVertices.size() * sizeof(MeshVertex)) == 0);
      }
      CHECK(raw.surfaceIndices == restored.surfaceIndices);
      return set;
    };
    const auto allSettled = [&](const auto& set) {
      for (const auto& target : targets) CHECK(gain(set, target) == 0);
    };
    allSettled(frame(0));
    const auto hash = s.game().position().hash();
    const auto selected = namedCell(s, "e2");
    REQUIRE(s.applyScript("click e2").has_value());
    const auto changed = frame(1);
    std::size_t lit = 0;
    for (const auto& target : targets) {
      if (target.cell == selected ||
          std::find(s.snapshot().highlighted().begin(), s.snapshot().highlighted().end(),
                    target.cell) != s.snapshot().highlighted().end()) {
        CHECK(gain(changed, target) == Approx(.018f));
        ++lit;
      } else
        CHECK(gain(changed, target) == 0);
    }
    CHECK(lit >= 3);
    CHECK(s.game().position().hash() == hash);
    CHECK(s.game().plyCount() == 0);
    allSettled(frame(1.3));
    REQUIRE(s.applyScript("click e2").has_value());
    const auto deselected = frame(2);
    CHECK(std::any_of(targets.begin(), targets.end(),
                      [&](const auto& target) { return gain(deselected, target) > 0; }));
    s.setHover(namedCell(s, "d2"), true);
    const auto hovered = frame(2.5);
    CHECK(std::any_of(targets.begin(), targets.end(),
                      [&](const auto& target) { return gain(hovered, target) > 0; }));
    REQUIRE_FALSE(s.snapshot().peekMoves().empty());
    for (const auto& target : targets) {
      if (std::find(s.snapshot().peekMoves().begin(), s.snapshot().peekMoves().end(),
                    target.cell) != s.snapshot().peekMoves().end())
        CHECK(gain(hovered, target) > 0);
    }
    s.setHover(kInvalidCell, false);
    frame(3);
    REQUIRE(s.applyScript("click e2\nclick e4").has_value());
    const auto moved = frame(4);
    CHECK(s.game().plyCount() == 1);
    CHECK(std::any_of(targets.begin(), targets.end(),
                      [&](const auto& target) { return gain(moved, target) > 0; }));
    s.setHover(namedCell(s, "d5"), true);
    const auto hinted = frame(4.05);
    REQUIRE_FALSE(s.snapshot().hintAttackers().empty());
    for (const auto& target : targets) {
      if (std::find(s.snapshot().hintAttackers().begin(),
                    s.snapshot().hintAttackers().end(),
                    target.cell) != s.snapshot().hintAttackers().end())
        CHECK(gain(hinted, target) > 0);
    }
    REQUIRE(s.applyScript("undo").has_value());
    const auto undone = frame(4.1);
    CHECK(s.game().position().hash() == hash);
    CHECK(std::any_of(targets.begin(), targets.end(),
                      [&](const auto& target) { return gain(undone, target) > 0; }));
    const auto fen = toFen(s.game().position());
    REQUIRE(s.loadFen(fen).has_value());
    allSettled(frame(4.1));
    REQUIRE(s.applyScript("click e2").has_value());
    frame(5);
    REQUIRE(s.applyScript("reset").has_value());
    allSettled(frame(5));
  }
}

TEST_CASE("LCD reset keys follow palette and layout rather than continuous pose",
          "[render][panel-response]") {
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  auto& s = **session;
  ResponseFixture f;
  auto theme = view::Theme::manifold();
  BoardOptions options;
  const auto prepare = [&] { REQUIRE(f.response.prepare(&s, theme, options)); };
  prepare();
  f.sample(0);
  f.targets[0].color.r = .8f;
  CHECK(f.sample(1) > 0);
  options.surfaceSlideU = 1;
  options.surfaceEvert = .2f;
  options.surfaceGhost = .5f;
  options.pieceHeightScale = .5f;
  REQUIRE(s.applyScript("orbit 0.1 0.2").has_value());
  prepare();
  CHECK(f.sample(1.06) == Approx(.0151875f));
  options.swapLayerColors = true;
  prepare();
  CHECK(f.sample(1.06) == 0);
  f.targets[0].color.r = .2f;
  CHECK(f.sample(2) > 0);
  theme.boardDark.r += .05f;
  prepare();
  CHECK(f.sample(2) == 0);
  f.targets[0].color.r = .8f;
  CHECK(f.sample(3) > 0);
  options.showLegalMoves = false;
  prepare();
  CHECK(f.sample(3) == 0);
  CHECK_FALSE(f.response.prepare(nullptr, theme, options));
  prepare();
  CHECK(f.sample(3) == 0);
  CHECK_FALSE(f.response.prepare(&s, view::Theme::console(), options));
}

TEST_CASE("same-epoch sessions and explicit axis layouts reset LCD history",
          "[render][panel-response]") {
  auto first = app::Session::create(test::loadVariant("cube5"));
  auto second = app::Session::create(test::loadVariant("cube5"));
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE((*first)->presentationEpoch() == (*second)->presentationEpoch());
  ResponseFixture f;
  const auto theme = view::Theme::manifold();
  const BoardOptions options;
  REQUIRE(f.response.prepare(first->get(), theme, options));
  f.sample(0);
  f.targets[0].color.r = .8f;
  REQUIRE(f.sample(1) > 0);
  REQUIRE(f.response.prepare(second->get(), theme, options));
  CHECK(f.sample(1) == 0);
  f.targets[0].color.r = .2f;
  REQUIRE(f.sample(2) > 0);
  REQUIRE((*second)->applyScript("axes file,level").has_value());
  REQUIRE(f.response.prepare(second->get(), theme, options));
  CHECK(f.sample(2) == 0);
  f.targets[0].color.r = .8f;
  REQUIRE(f.sample(3) > 0);
  REQUIRE((*second)->applyScript("axes file,level vertical").has_value());
  REQUIRE((*second)->viewConfig().gridVertical);
  REQUIRE(f.response.prepare(second->get(), theme, options));
  CHECK(f.sample(3) == 0);
}
#endif
