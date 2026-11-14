// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screen_transition.hpp"

#include <algorithm>

#include "view/game_end_fx.hpp"

namespace cb::app {

namespace {
float unit(float t) noexcept {
  return std::clamp(t, 0.0f, 1.0f);
}
float smooth(float t) noexcept {
  t = unit(t);
  return t * t * (3.0f - 2.0f * t);
}
bool calm(float intensity) noexcept {
  return !(intensity >= 0.5f);
}
bool isMenuLike(Screen s) noexcept {
  return s == Screen::MainMenu || s == Screen::NewGame || s == Screen::Watch ||
         s == Screen::Welcome;
}
}  // namespace

ScreenMove classifyMove(Screen from, Screen to) noexcept {
  if (isMenuLike(from) && to == Screen::Game) return ScreenMove::MenuToGame;
  if (from == Screen::Game && to == Screen::Paused) return ScreenMove::GameToPause;
  if (from == Screen::Paused && to == Screen::Game) return ScreenMove::PauseToGame;
  if ((from == Screen::Game || from == Screen::Paused) && to == Screen::MainMenu)
    return ScreenMove::GameToMenu;
  return ScreenMove::Cut;
}

float moveSeconds(ScreenMove move, float intensity) noexcept {
  if (move == ScreenMove::Cut) return 0.0f;
  if (calm(intensity)) return kCalmFadeSeconds;
  switch (move) {
    case ScreenMove::MenuToGame:
      return kMenuOffSeconds;
    case ScreenMove::GameToPause:
      return kPauseOnSeconds;
    case ScreenMove::PauseToGame:
      return kPauseOffSeconds;
    case ScreenMove::GameToMenu:
      return kPowerDownSeconds + kMenuOnSeconds;
    case ScreenMove::Deploy:
      return kDeploySeconds;
    case ScreenMove::Stow:
      return kStowSeconds;
    case ScreenMove::Station:
    case ScreenMove::Cut:
      break;
  }
  return 0.0f;
}

MoveFx moveFx(ScreenMove move, float age, float intensity) noexcept {
  return moveFx(move, age, intensity, moveSeconds(move, intensity));
}

MoveFx moveFx(ScreenMove move, float age, float intensity, float total) noexcept {
  const float lock =
      move == ScreenMove::Stow ? total - kStowMenuSeconds : kDeployDefaultLock;
  return moveFx(move, age, intensity, total, lock);
}

MoveFx moveFx(ScreenMove move, float age, float intensity, float total,
              float lock) noexcept {
  MoveFx fx;
  if (move == ScreenMove::Cut || !(age < total)) return fx;  // settled: the cut's state
  fx.done = false;
  age = std::max(0.0f, age);
  if (calm(intensity)) {
    // One cross-fade, no tracing, no power-down.
    const float f = smooth(age / total);
    fx.leaveAlpha = 1.0f - f;
    fx.leaveTrace = 1.0f;
    fx.arriveAlpha = f;
    fx.arriveTrace = 1.0f;
    return fx;
  }
  switch (move) {
    case ScreenMove::Station:
      fx.leaveTrace = fx.arriveTrace = 1;
      fx.leaveAlpha = 1 - smooth(age / (.35f * total));
      fx.arriveAlpha = smooth((age - .55f * total) / (.45f * total));
      break;
    case ScreenMove::MenuToGame: {
      const float f = smooth(age / kMenuOffSeconds);
      fx.leaveTrace = fx.leaveAlpha = 1.0f - f;
      break;
    }
    case ScreenMove::GameToPause: {
      const float f = smooth(age / kPauseOnSeconds);
      fx.arriveTrace = fx.arriveAlpha = f;
      break;
    }
    case ScreenMove::PauseToGame: {
      const float f = smooth(age / kPauseOffSeconds);
      fx.leaveTrace = fx.leaveAlpha = 1.0f - f;
      break;
    }
    case ScreenMove::GameToMenu: {
      // The board powers down first; the menu is not there yet.
      if (age < kPowerDownSeconds) {
        fx.holdsBoard = true;
        fx.powerDown = age / kPowerDownSeconds;
        fx.arriveTrace = fx.arriveAlpha = 0.0f;
      } else {
        const float f = smooth((age - kPowerDownSeconds) / kMenuOnSeconds);
        fx.arriveTrace = fx.arriveAlpha = f;
      }
      break;
    }
    case ScreenMove::Deploy:
      fx.leaveTrace = fx.arriveTrace = 1.0f;
      fx.leaveAlpha = 1.0f - smooth(age / kDeployMenuOffSeconds);
      fx.arriveAlpha = smooth((age - lock) / std::max(0.01f, total - lock));
      break;
    case ScreenMove::Stow:
      fx.leaveTrace = fx.arriveTrace = 1.0f;
      fx.holdsBoard = age < lock;
      fx.arriveAlpha =
          fx.holdsBoard ? 0.0f : smooth((age - lock) / std::max(0.01f, total - lock));
      break;
    case ScreenMove::Cut:
      break;
  }
  return fx;
}

float endAgeAt(float progress, int maxDistance) noexcept {
  // The full decisive look takes the wave's travel, the glitch and the fall; with no
  // delay that is compressed so the whole of it fits `progress` 0..1.
  const float full =
      static_cast<float>(std::max(0, maxDistance)) / view::kPowerDownCellsPerSecond +
      view::kPowerDownGlitchSeconds + view::kPowerDownFallSeconds;
  return view::kEndDelaySeconds + unit(progress) * full;
}

}  // namespace cb::app
