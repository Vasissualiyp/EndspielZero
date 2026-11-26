// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cb::io {
struct LabDoc;
}

namespace cb::app {

class Shell;

/// The debug console: a line of text in, a block of text out, and everything the ordinary
/// interface has no button for - setting a position from a FEN, placing a piece, counting
/// nodes, reading the marks and the hash, evaluating a moveset or a rule expression
/// against the running game, changing a setting by name. It owns no window: the interface
/// draws it, a test drives it. Functional, not pretty.
class DebugConsole {
 public:
  /// Run one command line against the shell; the output (possibly several lines) is also
  /// appended to the scrollback along with the command.
  std::string run(Shell& shell, std::string_view line);

  /// Everything said so far, oldest first.
  [[nodiscard]] const std::vector<std::string>& log() const noexcept { return log_; }
  /// The commands entered, oldest first, for the arrow keys.
  [[nodiscard]] const std::vector<std::string>& history() const noexcept {
    return history_;
  }
  /// The GUI supplies the loaded document while the lab is enabled, otherwise null.
  void setLabDoc(const io::LabDoc* doc) noexcept { labDoc_ = doc; }
  [[nodiscard]] std::string_view stationPin() const noexcept { return stationPin_; }
  [[nodiscard]] std::string_view saliencePin() const noexcept { return saliencePin_; }
  /// M24.5: `focus <0..1>|auto` pins the room-light focus level during a game.
  [[nodiscard]] std::optional<float> focusPin() const noexcept { return focusPin_; }
  /// `wallfade on|off`: the screen-door fade of walls between the eye and the board.
  [[nodiscard]] bool wallFade() const noexcept { return wallFade_; }
  /// M24.5b: `skin on|off` switches the lab game's slab skin (default on); `latch <axis>`
  /// asks the front end to flash a seam latch, taken once.
  [[nodiscard]] bool skin() const noexcept { return skin_; }
  void setSkin(bool on) noexcept { skin_ = on; }
  [[nodiscard]] std::optional<int> takeLatch() noexcept {
    auto out = latch_;
    latch_.reset();
    return out;
  }
  /// M24.6: `bridges on|off` draws the light bridges between glued slabs that cannot
  /// touch (default on); `relayout` asks the front end to replay the last layout change,
  /// taken once.
  [[nodiscard]] bool bridges() const noexcept { return bridges_; }
  [[nodiscard]] bool takeRelayout() noexcept {
    const bool out = relayout_;
    relayout_ = false;
    return out;
  }
  /// M25.5: `vr on|off` pins the lab game into or out of the neural VR whatever its slab
  /// count (`vr auto` returns to the count); the front end clears the pin with the
  /// session. `vr status` (or `vr`) prints the GUI's report.
  [[nodiscard]] std::optional<bool> vrPin() const noexcept { return vrPin_; }
  void clearVrPin() noexcept { vrPin_.reset(); }
  void setVrReport(std::function<std::string()> report) { vrReport_ = std::move(report); }
  /// The GUI supplies the `stretch` report (min/max stretch, pass-through count).
  void setSkinReport(std::function<std::string()> report) {
    skinReport_ = std::move(report);
  }
  /// The GUI supplies the `rig` dump while a game is in the lab, otherwise none.
  void setRigReport(std::function<std::string()> report) {
    rigReport_ = std::move(report);
  }
  void clearLog() { log_.clear(); }

  /// The help text: every command, one line each.
  [[nodiscard]] static std::string help();

 private:
  std::string stationPin_, saliencePin_;
  std::optional<float> focusPin_;
  bool wallFade_{true};
  bool skin_{true};
  std::optional<int> latch_;
  bool bridges_{true};
  bool relayout_{false};
  std::optional<bool> vrPin_;
  std::function<std::string()> vrReport_;
  std::function<std::string()> rigReport_;
  std::function<std::string()> skinReport_;
  const io::LabDoc* labDoc_{nullptr};
  std::vector<std::string> log_;
  std::vector<std::string> history_;
};

}  // namespace cb::app
