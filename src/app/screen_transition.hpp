// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/shell.hpp"

namespace cb::app {

/// The screen changes that cross between the menus and the game (M23.11 section 6). Every
/// other change keeps the shell's old pane move. They are pure in an age in seconds on
/// the shell's one transition clock (`Ui::enter_`, which only runs at a different rate
/// per move: there is no second clock).
enum class ScreenMove : std::uint8_t {
  Cut,         ///< any other change: the old menu pane move
  MenuToGame,  ///< New game, Continue, load, Watch: the menu traces off, the boot follows
  GameToPause,  ///< the pause panel traces on over the blur
  PauseToGame,  ///< the pause panel traces off while the blur releases
  GameToMenu,   ///< a quick power-down of the board, then the menu traces on
};

inline constexpr float kMenuOffSeconds = 0.3f;
inline constexpr float kPauseOnSeconds = 0.3f;
inline constexpr float kPauseOffSeconds = 0.2f;
inline constexpr float kPowerDownSeconds = 0.8f;  ///< the board's compressed power-down
inline constexpr float kMenuOnSeconds = 0.3f;
inline constexpr float kCalmFadeSeconds = 0.3f;  ///< every calm move is one cross-fade

[[nodiscard]] ScreenMove classifyMove(Screen from, Screen to) noexcept;

/// Total seconds of the move; 0 for `Cut`. Below intensity one half every move is a
/// single cross-fade of `kCalmFadeSeconds`.
[[nodiscard]] float moveSeconds(ScreenMove move, float intensity) noexcept;

/// What the departing and arriving panels and the board look like at `age`.
/// `trace` 0..1 is how much of a panel's edge is drawn (the pane-trace of section 5);
/// `alpha` is the content's brightness. Calm moves never trace: they fade, with `trace`
/// at 1. At and after `moveSeconds` this is exactly the settled state of the cut.
struct MoveFx {
  float leaveTrace{0}, leaveAlpha{0};    ///< the screen being left (1 -> 0)
  float arriveTrace{1}, arriveAlpha{1};  ///< the screen arriving (0 -> 1)
  float powerDown{0};      ///< 0..1 progress of the board's power-down, 0 when none runs
  bool holdsBoard{false};  ///< the board stays drawn although the screen has no board
  bool done{true};
};
[[nodiscard]] MoveFx moveFx(ScreenMove move, float age, float intensity) noexcept;

/// The age on the end power-down's own timeline (`view::powerDown`) that a move's
/// `powerDown` progress stands for: the decisive look with no hold, compressed.
[[nodiscard]] float endAgeAt(float powerDownProgress, int maxDistance) noexcept;

}  // namespace cb::app
