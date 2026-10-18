// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cb::app {

class Shell;

/// The debug console: a line of text in, a block of text out, and everything the ordinary
/// interface has no button for - setting a position from a FEN, placing a piece, counting
/// nodes, reading the marks and the hash, evaluating a moveset or a rule expression against
/// the running game, changing a setting by name. It owns no window: the interface draws it,
/// a test drives it. Functional, not pretty.
class DebugConsole {
 public:
  /// Run one command line against the shell; the output (possibly several lines) is also
  /// appended to the scrollback along with the command.
  std::string run(Shell& shell, std::string_view line);

  /// Everything said so far, oldest first.
  [[nodiscard]] const std::vector<std::string>& log() const noexcept { return log_; }
  /// The commands entered, oldest first, for the arrow keys.
  [[nodiscard]] const std::vector<std::string>& history() const noexcept { return history_; }
  void clearLog() { log_.clear(); }

  /// The help text: every command, one line each.
  [[nodiscard]] static std::string help();

 private:
  std::vector<std::string> log_;
  std::vector<std::string> history_;
};

}  // namespace cb::app
