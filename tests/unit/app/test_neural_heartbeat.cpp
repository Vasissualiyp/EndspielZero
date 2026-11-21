// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "app/neural_heartbeat.hpp"
#include "app/session.hpp"
#include "support/variants.hpp"
using namespace cb;
TEST_CASE("neural heartbeat follows player check captures end and low clock (M25.1)",
          "[unit][app][implant][neural]") {
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session);
  app::NeuralHeartbeat beat;
  beat.observe(session->get(), 0);
  CHECK(beat.tension() == 0);
  beat.observe(session->get(), 1, 29.9f);
  CHECK(beat.tension() == 25);
  beat.observe(session->get(), 2, 30.f);
  CHECK(beat.tension() == 0);
  REQUIRE((**session).loadFen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1"));
  beat.observe(session->get(), 3);
  CHECK(beat.tension() == 20);
  (**session).setEngineSide(Color::White);
  beat.observe(session->get(), 4);
  CHECK(beat.tension() == 0);
  (**session).setEngineSide(std::nullopt);
  REQUIRE((**session).loadFen("4k3/8/8/8/8/r7/R7/4K3 w - - 0 1"));
  REQUIRE((**session).applyScript("click a2\nclick a3"));
  beat.observe(session->get(), 5);
  CHECK(beat.tension() == Catch::Approx(10));
  beat.observe(session->get(), 15);
  CHECK(beat.tension() < .6f);
  REQUIRE((**session).applyScript("click e8\nclick e7"));
  beat.observe(session->get(), 15.1);
  CHECK(beat.tension() == 0);
  app::Action end;
  end.kind = app::ActionKind::AgreeDraw;
  REQUIRE((**session).apply(end));
  beat.observe(session->get(), 16);
  CHECK(beat.tension() == 50);
  beat.observe(session->get(), 36);
  CHECK(beat.tension() < .2f);
  CHECK(beat.phase().bpm < 65);
}
