// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/screen_transition.hpp"

using namespace cb::app;
using Catch::Approx;

TEST_CASE("screen changes are classified into the four section 6 moves", "[transition]") {
  CHECK(classifyMove(Screen::MainMenu, Screen::Game) == ScreenMove::MenuToGame);
  CHECK(classifyMove(Screen::NewGame, Screen::Game) == ScreenMove::MenuToGame);
  CHECK(classifyMove(Screen::Game, Screen::Paused) == ScreenMove::GameToPause);
  CHECK(classifyMove(Screen::Paused, Screen::Game) == ScreenMove::PauseToGame);
  CHECK(classifyMove(Screen::Paused, Screen::MainMenu) == ScreenMove::GameToMenu);
  CHECK(classifyMove(Screen::Game, Screen::MainMenu) == ScreenMove::GameToMenu);
  CHECK(classifyMove(Screen::MainMenu, Screen::Settings) == ScreenMove::Cut);
  CHECK(classifyMove(Screen::Paused, Screen::Settings) == ScreenMove::Cut);
}

TEST_CASE("each move has the specified duration and traces monotonically",
          "[transition]") {
  CHECK(moveSeconds(ScreenMove::MenuToGame, 1) == Approx(0.3f));
  CHECK(moveSeconds(ScreenMove::GameToPause, 1) == Approx(0.3f));
  CHECK(moveSeconds(ScreenMove::PauseToGame, 1) == Approx(0.2f));
  CHECK(moveSeconds(ScreenMove::GameToMenu, 1) == Approx(0.8f + 0.3f));
  CHECK(moveSeconds(ScreenMove::Cut, 1) == 0.0f);

  float last = 2;
  for (float a = 0; a < 0.3f; a += 0.01f) {  // the menu traces off
    const auto fx = moveFx(ScreenMove::MenuToGame, a, 1);
    CHECK(fx.leaveTrace <= last);
    last = fx.leaveTrace;
  }
  CHECK(moveFx(ScreenMove::MenuToGame, 0, 1).leaveTrace == Approx(1.0f));
  last = -1;
  for (float a = 0; a < 0.3f; a += 0.01f) {  // the pause panel traces on
    const auto fx = moveFx(ScreenMove::GameToPause, a, 1);
    CHECK(fx.arriveTrace >= last);
    last = fx.arriveTrace;
  }
  CHECK(moveFx(ScreenMove::GameToPause, 0, 1).arriveTrace == Approx(0.0f));
  CHECK(moveFx(ScreenMove::PauseToGame, 0.1f, 1).leaveTrace < 1.0f);
}

TEST_CASE("game to menu powers the board down, then traces the menu on", "[transition]") {
  const auto mid = moveFx(ScreenMove::GameToMenu, 0.4f, 1);
  CHECK(mid.holdsBoard);
  CHECK(mid.powerDown == Approx(0.5f));
  CHECK(mid.arriveTrace == 0.0f);
  const auto menu = moveFx(ScreenMove::GameToMenu, 0.95f, 1);
  CHECK_FALSE(menu.holdsBoard);
  CHECK(menu.arriveTrace > 0.0f);
  CHECK(menu.arriveTrace < 1.0f);
  // The decisive look with no delay, compressed: age 0 is the start of the wave.
  CHECK(endAgeAt(0, 4) < endAgeAt(1, 4));
}

TEST_CASE("every move ends in exactly the state of the cut", "[transition]") {
  for (const auto move : {ScreenMove::MenuToGame, ScreenMove::GameToPause,
                          ScreenMove::PauseToGame, ScreenMove::GameToMenu}) {
    for (const float intensity : {1.0f, 0.0f}) {
      CAPTURE(static_cast<int>(move), intensity);
      const float total = moveSeconds(move, intensity);
      for (const float age : {total, total + 0.5f, 100.0f}) {
        const auto fx = moveFx(move, age, intensity);
        CHECK(fx.done);
        CHECK(fx.arriveTrace == 1.0f);
        CHECK(fx.arriveAlpha == 1.0f);
        CHECK(fx.leaveAlpha == 0.0f);
        CHECK(fx.powerDown == 0.0f);
        CHECK_FALSE(fx.holdsBoard);
      }
    }
  }
}

TEST_CASE("calm moves are one cross-fade with no trace or power-down", "[transition]") {
  for (const auto move : {ScreenMove::MenuToGame, ScreenMove::GameToPause,
                          ScreenMove::PauseToGame, ScreenMove::GameToMenu}) {
    CHECK(moveSeconds(move, 0.4f) == Approx(kCalmFadeSeconds));
    for (float a = 0.0f; a < kCalmFadeSeconds; a += 0.02f) {
      const auto fx = moveFx(move, a, 0.4f);
      CHECK(fx.leaveTrace == 1.0f);
      CHECK(fx.arriveTrace == 1.0f);
      CHECK_FALSE(fx.holdsBoard);
      CHECK(fx.powerDown == 0.0f);
      CHECK(fx.leaveAlpha + fx.arriveAlpha == Approx(1.0f));
    }
  }
}

TEST_CASE("a skip is an age past the end", "[transition]") {
  const auto fx = moveFx(ScreenMove::GameToMenu, 1e9f, 1);
  CHECK(fx.done);
  CHECK(fx.arriveAlpha == 1.0f);
}
