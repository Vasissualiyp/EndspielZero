// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/debug_console.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/session.hpp"
#include "app/settings.hpp"
#include "app/shell.hpp"
#include "io/fen.hpp"
#include "io/moveset_library.hpp"
#include "io/notation.hpp"
#include "io/rule_parse.hpp"
#include "movegen/movegen.hpp"
#include "pieces/moveset.hpp"
#include "rules/vm.hpp"

namespace cb::app {
namespace {

std::vector<std::string> words(std::string_view line) {
  std::vector<std::string> out;
  std::istringstream in{std::string(line)};
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

/// Everything after the first word, trimmed: the argument of a command that takes free
/// text.
std::string rest(std::string_view line) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string_view::npos) return {};
  const auto space = line.find_first_of(" \t", start);
  if (space == std::string_view::npos) return {};
  const auto arg = line.find_first_not_of(" \t", space);
  return arg == std::string_view::npos ? std::string{} : std::string(line.substr(arg));
}

std::optional<int> asInt(const std::string& s) {
  int v = 0;
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
  return v;
}

std::string cellText(const VariantSpec& v, CellId c) {
  return c == kInvalidCell ? std::string("-") : cellName(v.dims, c);
}

/// The settings as the file spells them, one `key = value` per line.
std::string settingsText(const Settings& s) {
  const auto path =
      std::filesystem::temp_directory_path() / "chessbox-debug-settings.conf";
  if (!s.save(path).has_value()) return {};
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string perftLines(const VariantSpec& v, const Position& start, int depth,
                       bool divide) {
  Position p = start;
  const MoveGen gen(v);
  std::ostringstream out;
  if (!divide) {
    out << gen.perft(p, depth);
    return out.str();
  }
  MoveList moves(v.moveUpperBound());
  gen.generateLegal(p, moves);
  std::uint64_t total = 0;
  for (const Move& m : moves) {
    Undo u;
    p.make(m, u);
    const std::uint64_t n = depth <= 1 ? 1 : gen.perft(p, depth - 1);
    p.unmake(m, u);
    out << moveText(v, m) << ": " << n << '\n';
    total += n;
  }
  out << "total: " << total;
  return out.str();
}

std::string execute(Shell& shell, const std::string& line) {
  const std::vector<std::string> w = words(line);
  if (w.empty()) return {};
  const std::string& cmd = w[0];
  Session* session = shell.session();
  const auto needGame = [&]() -> std::string {
    return session == nullptr ? "no game is running: use 'load <variant>'"
                              : std::string{};
  };

  if (cmd == "help" || cmd == "?") return DebugConsole::help();

  if (cmd == "dev" || cmd == "post") {
    if (w.size() > 2 || (w.size() == 2 && w[1] != "on" && w[1] != "off"))
      return "usage: " + cmd + " [on|off]";
    bool& enabled = cmd == "dev" ? shell.settings().devOverlay : shell.settings().postProcess;
    enabled = w.size() == 1 ? !enabled : w[1] == "on";
    const std::string label = cmd == "dev" ? "dev overlay: " : "post processing: ";
    return label + (enabled ? "on" : "off");
  }

  if (cmd == "load") {
    if (w.size() < 2) return "usage: load <variant>";
    const auto ok = shell.startGame(w[1]);
    return ok.has_value() ? "started " + w[1] : ok.error().format();
  }
  if (cmd == "goto") {
    if (w.size() < 2) return "usage: goto menu|game|paused|settings|editor|newgame";
    static const std::pair<const char*, Screen> kScreens[]{
        {"menu", Screen::MainMenu},  {"game", Screen::Game},
        {"paused", Screen::Paused},  {"settings", Screen::Settings},
        {"editor", Screen::Editor},  {"newgame", Screen::NewGame},
        {"welcome", Screen::Welcome}};
    for (const auto& [name, screen] : kScreens) {
      if (w[1] == name) {
        shell.go(screen);
        return std::string("now on ") + name;
      }
    }
    return "no such screen: " + w[1];
  }
  if (cmd == "get" || cmd == "set") {
    const std::string text = settingsText(shell.settings());
    std::string key = w.size() > 1 ? w[1] : std::string{};
    if (key == "devOverlay") key = "dev_overlay";
    if (key == "postProcess") key = "post_process";
    if (key == "bloomScale") key = "bloom_scale";
    if (cmd == "get") {
      if (w.size() < 2) return text;
      std::istringstream in(text);
      std::string l;
      while (std::getline(in, l)) {
        if (l.rfind(key + " =", 0) == 0) return l;
      }
      return "no setting called " + w[1];
    }
    if (w.size() < 3) return "usage: set <setting> <value>";
    // Change it the way the file would: the old text, the new line after it (later lines
    // win), read back through the settings loader.
    const auto path = std::filesystem::temp_directory_path() / "chessbox-debug-set.conf";
    {
      std::ofstream out(path, std::ios::trunc);
      out << text << '\n' << key << " = " << rest(rest(line)) << '\n';
    }
    shell.settings() = Settings::load(path);
    return w[1] + " is now " + rest(rest(line));
  }

  if (const std::string none = needGame(); !none.empty()) return none;
  const VariantSpec& v = session->variant();
  const Game& game = session->game();
  const Position& pos = game.position();

  if (cmd == "info") {
    std::ostringstream out;
    out << v.name << ": " << static_cast<int>(v.dims.dims()) << " axes, "
        << v.dims.cellCount() << " cells, " << v.pieces.size() - 1 << " pieces, id "
        << v.variantId() << "\n";
    out << "mark tags:";
    for (const auto& t : v.markTags) out << ' ' << t.name << (t.blocks ? "(blocks)" : "");
    out << "\ncompound moves: " << v.compounds.size()
        << ", castle templates: " << v.castles.size()
        << ", directions: " << v.dirTable.size() << "\n";
    for (std::size_t i = 1; i < v.pieces.size(); ++i) {
      const PieceTypeDef& p = v.pieces[i];
      out << "  " << p.symbol << ' ' << p.name << (p.royal ? " (royal)" : "")
          << (p.tracksMoved ? " [tracks moved]" : "") << ": " << p.atoms.size()
          << " moves\n";
      for (const MoveAtom& a : p.atoms) out << "      " << a.toString() << '\n';
    }
    return out.str();
  }
  if (cmd == "fen") return toFen(pos);
  if (cmd == "hash") {
    std::ostringstream out;
    out << "hash " << pos.hash() << " (recomputed " << pos.computeHash() << ")"
        << (pos.hash() == pos.computeHash() ? " ok" : " MISMATCH");
    return out.str();
  }
  if (cmd == "setfen") {
    const std::string fen = rest(line);
    if (fen.empty()) return "usage: setfen <fen>";
    const auto ok = session->loadFen(fen);
    return ok.has_value() ? "position set" : ok.error().format();
  }
  if (cmd == "moves") {
    std::ostringstream out;
    const auto& legal = game.legalMoves();
    out << legal.size() << " legal moves\n";
    for (const Move& m : legal) {
      out << "  " << moveText(v, m);
      if (m.isCapture()) out << " x" << cellText(v, m.captureCell);
      if (m.isCastle()) out << " castle";
      out << '\n';
    }
    return out.str();
  }
  if (cmd == "play") {
    if (w.size() < 2) return "usage: play <move>";
    const auto ok = session->playMoveText(w[1]);
    return ok.has_value() ? "played " + w[1] : ok.error().format();
  }
  if (cmd == "boot") {
    session->requestBoot();  // the front end stamps it on its next frame
    return "booting the board";
  }
  if (cmd == "undo" || cmd == "reset") {
    Action a;
    a.kind = cmd == "undo" ? ActionKind::Undo : ActionKind::Reset;
    const auto ok = session->apply(a);
    return ok.has_value() ? "ok" : ok.error().format();
  }
  if (cmd == "script") {
    const auto ok = session->applyScript(rest(line));
    return ok.has_value() ? "ok" : ok.error().format();
  }
  if (cmd == "perft" || cmd == "divide") {
    const auto depth = w.size() > 1 ? asInt(w[1]) : std::nullopt;
    if (!depth || *depth < 1 || *depth > 8) return "usage: " + cmd + " <depth 1..8>";
    return perftLines(v, pos, *depth, cmd == "divide");
  }
  if (cmd == "marks") {
    std::ostringstream out;
    if (pos.marks().empty()) return "no marks";
    for (const Mark& m : pos.marks()) {
      out << v.markTags[m.tag].name << " at " << cellText(v, m.cell) << ", "
          << (m.ttl == 0 ? std::string("forever") : std::to_string(m.ttl) + " plies left")
          << ", left by " << (m.owner == 0 ? "white" : "black");
      if (m.victim != kInvalidCell) out << ", exposes " << cellText(v, m.victim);
      out << '\n';
    }
    return out.str();
  }
  if (cmd == "unmoved") {
    std::ostringstream out;
    int n = 0;
    for (CellId c = 0; c < v.dims.cellCount(); ++c) {
      const Piece p = pos.at(c);
      if (p.empty() || !v.pieces[p.type].tracksMoved) continue;
      out << cellText(v, c) << ' ' << v.pieces[p.type].name
          << (p.hasMoved() ? " moved" : " unmoved") << '\n';
      ++n;
    }
    return n == 0 ? std::string("no piece type here tracks whether it has moved")
                  : out.str();
  }
  if (cmd == "attacked") {
    if (w.size() < 2) return "usage: attacked <cell>";
    const auto cell = parseCell(v.dims, w[1]);
    if (!cell.has_value()) return cell.error().format();
    const MoveGen gen(v);
    const Color other = opponent(pos.sideToMove());
    return std::string(w[1]) +
           (gen.isAttacked(pos, *cell, other) ? " is attacked by "
                                              : " is not attacked by ") +
           (other == Color::White ? "white" : "black");
  }
  if (cmd == "place" || cmd == "remove" || cmd == "side") {
    Position p = pos;
    if (cmd == "side") {
      if (w.size() < 2 || (w[1] != "w" && w[1] != "b")) return "usage: side w|b";
      p.setSideToMove(w[1] == "w" ? Color::White : Color::Black);
    } else {
      if (w.size() < (cmd == "place" ? 3u : 2u))
        return "usage: place <cell> <symbol> | remove <cell>";
      const auto cell = parseCell(v.dims, w[1]);
      if (!cell.has_value()) return cell.error().format();
      if (cmd == "remove") {
        p.removeAt(*cell);
      } else {
        const char sym = w[2][0];
        const PieceTypeId type =
            v.findPieceBySymbol(static_cast<char>(std::toupper(sym)));
        if (type == kNoPiece) return "no piece has the symbol " + w[2];
        p.place(*cell, type,
                std::isupper(static_cast<unsigned char>(sym)) != 0 ? Color::White
                                                                   : Color::Black);
      }
    }
    const auto ok = session->loadFen(toFen(p));
    return ok.has_value() ? toFen(p) : ok.error().format();
  }
  if (cmd == "moveset") {
    const std::string text = rest(line);
    if (text.empty())
      return "usage: moveset <text>, e.g. moveset U r in 1..D : [1 x r]^k";
    std::vector<std::string> axes;
    for (std::size_t i = 0; i < v.dims.dims(); ++i) axes.push_back(v.dims.name(i));
    MovesetContext ctx;
    ctx.dims = v.dims.dims();
    ctx.axisNames = axes;
    ctx.orientAxis = v.orientationAxis;
    for (std::size_t i = 1; i < v.markTags.size(); ++i)
      ctx.markTags.push_back(v.markTags[i].name);
    ctx.lookup = [](std::string_view n) { return libraryMoveset(n); };
    const auto atoms = evalMoveset(text, ctx);
    if (!atoms.has_value()) return atoms.error().format();
    std::ostringstream out;
    std::size_t dirs = 0;
    for (const MoveAtom& a : *atoms) {
      const auto d = expandAtomFor(a, v.dims.dims(), v.orientationAxis, Color::White);
      dirs += d.size();
      out << "  " << a.toString() << " -> " << d.size() << " directions\n";
    }
    out << atoms->size() << " moves, " << dirs << " directions on this board";
    return out.str();
  }
  if (cmd == "expr") {
    const std::string text = rest(line);
    if (text.empty()) return "usage: expr <rule expression>, e.g. expr (is_empty e4)";
    const auto parsed = parseRuleExpr(v, text);
    if (!parsed.has_value()) return parsed.error().format();
    Position copy = pos;
    const MoveGen gen(v);
    rules::RuleEnv env;
    env.pos = &copy;
    env.mover = pos.sideToMove();
    env.isAttacked = [&](CellId c, Color by) { return gen.isAttacked(copy, c, by); };
    const rules::RuleEngine engine{rules::RuleSet{}};
    std::size_t steps = 0;
    return std::to_string(engine.eval(*parsed, env, steps));
  }
  return "unknown command '" + cmd + "' - try help";
}

}  // namespace

std::string DebugConsole::help() {
  return "load <variant>            start a variant\n"
         "goto <screen>            menu game paused settings editor newgame welcome\n"
         "get [<setting>]          show settings; set <setting> <value> changes one\n"
         "dev [on|off]             developer overlay; no argument toggles it\n"
         "post [on|off]            post processing; no argument toggles it\n"
         "boot                     replay the board's power-on sequence\n"
         "info                     variant summary: pieces, moves, marks, compounds\n"
         "fen | setfen <fen>       show or replace the position\n"
         "place <cell> <symbol>    put a piece (Q white, q black); remove <cell>; side "
         "w|b\n"
         "moves | play <move>      list legal moves; play one (e2e4)\n"
         "undo | reset | script <t>  take back, restart, or run click scripts\n"
         "perft <n> | divide <n>   count nodes\n"
         "hash                     position hash and its recomputation\n"
         "marks | unmoved          marks on the board; which pieces have moved\n"
         "attacked <cell>          is the cell attacked by the side not to move\n"
         "moveset <text>           evaluate moveset text on this board\n"
         "expr <s-expression>      evaluate a rule expression on the position\n"
         "clear                    empty the scrollback";
}

std::string DebugConsole::run(Shell& shell, std::string_view line) {
  const std::string text(line);
  if (words(text).empty()) return {};
  history_.push_back(text);
  log_.push_back("> " + text);
  std::string out = words(text)[0] == "clear" ? std::string{} : execute(shell, text);
  if (words(text)[0] == "clear") log_.clear();
  std::istringstream lines(out);
  std::string l;
  while (std::getline(lines, l)) log_.push_back(l);
  return out;
}

}  // namespace cb::app
