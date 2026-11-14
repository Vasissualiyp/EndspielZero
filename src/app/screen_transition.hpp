// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/shell.hpp"

namespace cb::app {

/// The screen changes that cross between the menus and the game (M23.11 section 6). Every
/// other change keeps the shell's old pane move. They are pure in an age in seconds on
/// the shell's one transition clock (`Ui::enter_`, which only runs at a different rate
/// per move: there is no second clock).
enum class ScreenMove : std::uint8_t {
  Station,     ///< both screens are lab menu screens: the camera flies between their
               ///< stations (M24.4a)
  Cut,         ///< any other change: the old menu pane move
  MenuToGame,  ///< New game, Continue, load, Watch: the menu traces off, the boot follows
  GameToPause,  ///< the pause panel traces on over the blur
  PauseToGame,  ///< the pause panel traces off while the blur releases
  GameToMenu,   ///< a quick power-down of the board, then the menu traces on
  Deploy,  ///< lab: menu to game - slabs fly from the rack, the camera follows (M24.5)
  Stow,    ///< lab: game to menu - shutdown, slabs back to the rack, the menu forms
};

inline constexpr float kMenuOffSeconds = 0.3f;
inline constexpr float kPauseOnSeconds = 0.3f;
inline constexpr float kPauseOffSeconds = 0.2f;
inline constexpr float kPowerDownSeconds = 0.8f;  ///< the board's compressed power-down
inline constexpr float kMenuOnSeconds = 0.3f;
inline constexpr float kCalmFadeSeconds = 0.3f;  ///< every calm move is one cross-fade
/// M24.5: a deploy lasts max(3.0, lastLock + 1.0), which is 3.0 for up to four mast
/// groups; the menu dissolves in its first 0.3 s and the HUD forms after the last lock.
inline constexpr float kDeploySeconds = 3.0f;
inline constexpr float kDeployMenuOffSeconds = 0.3f;
inline constexpr float kDeployDefaultLock = 1.62f;  ///< one mast group's lock
/// A stow: the compressed shutdown (live games only), the flight home while the slabs
/// return, then the projector grows and the menu forms.
inline constexpr float kStowShutdownSeconds = 1.0f;
inline constexpr float kStowFlightMinSeconds = 1.8f;
inline constexpr float kStowMenuSeconds = 1.0f;
inline constexpr float kStowSeconds = kStowFlightMinSeconds + kStowMenuSeconds;

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

[[nodiscard]] MoveFx moveFx(ScreenMove move, float age, float intensity,
                            float seconds) noexcept;
/// `lock` is where a rig move hands over: the deploy's last slab lock (the HUD forms
/// after it) or the stow's arrival home (the menu forms after it).
[[nodiscard]] MoveFx moveFx(ScreenMove move, float age, float intensity, float seconds,
                            float lock) noexcept;

/// The age on the end power-down's own timeline (`view::powerDown`) that a move's
/// `powerDown` progress stands for: the decisive look with no hold, compressed.
[[nodiscard]] float endAgeAt(float powerDownProgress, int maxDistance) noexcept;

}  // namespace cb::app
