// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces/moveset.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace cb {
namespace {

// ===========================================================================
// Lexer
// ===========================================================================

enum class Tok : std::uint8_t {
  Int,
  Ident,
  LBracket,
  RBracket,
  LParen,
  RParen,
  Comma,
  Colon,
  Caret,
  DotDot,
  Backslash,
  Equals,
  Plus,
  Minus,
  Newline,
  End,
};

struct Token {
  Tok kind{Tok::End};
  std::string text;
  long value{0};
  int line{1};
  int column{1};
};

struct Failure {
  std::string message;
  int line{0};
  int column{0};
};

/// All syntax errors travel as one exception inside this file and become a `Result` at the
/// public boundary, which keeps the recursive descent readable. Nothing escapes.
struct Abort {
  Failure why;
};

[[noreturn]] void fail_at(const Token& t, std::string message) {
  throw Abort{Failure{std::move(message), t.line, t.column}};
}

std::vector<Token> lex(std::string_view text) {
  std::vector<Token> out;
  int line = 1;
  std::size_t lineStart = 0;
  int depth = 0;  // a newline inside brackets continues the expression
  std::size_t i = 0;
  const auto column = [&](std::size_t at) { return static_cast<int>(at - lineStart) + 1; };
  const auto push = [&](Tok k, std::string t, std::size_t at, long v = 0) {
    out.push_back(Token{k, std::move(t), v, line, column(at)});
  };
  while (i < text.size()) {
    const char c = text[i];
    if (c == '\n') {
      if (depth == 0) push(Tok::Newline, "\n", i);
      ++line;
      ++i;
      lineStart = i;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }
    // A comment runs to the end of the line.
    if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
      while (i < text.size() && text[i] != '\n') ++i;
      continue;
    }
    // The union sign, as the single character mathematicians write.
    if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < text.size() &&
        static_cast<unsigned char>(text[i + 1]) == 0x88 &&
        static_cast<unsigned char>(text[i + 2]) == 0xAA) {
      push(Tok::Ident, "U", i);
      i += 3;
      continue;
    }
    if (c >= '0' && c <= '9') {
      std::size_t j = i;
      while (j < text.size() && text[j] >= '0' && text[j] <= '9') ++j;
      push(Tok::Int, std::string(text.substr(i, j - i)), i,
           std::strtol(std::string(text.substr(i, j - i)).c_str(), nullptr, 10));
      i = j;
      continue;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      std::size_t j = i;
      while (j < text.size() &&
             ((text[j] >= 'a' && text[j] <= 'z') || (text[j] >= 'A' && text[j] <= 'Z') ||
              (text[j] >= '0' && text[j] <= '9') || text[j] == '_')) {
        ++j;
      }
      push(Tok::Ident, std::string(text.substr(i, j - i)), i);
      i = j;
      continue;
    }
    switch (c) {
      case '[':
        ++depth;
        push(Tok::LBracket, "[", i);
        break;
      case ']':
        depth = std::max(0, depth - 1);
        push(Tok::RBracket, "]", i);
        break;
      case '(':
        ++depth;
        push(Tok::LParen, "(", i);
        break;
      case ')':
        depth = std::max(0, depth - 1);
        push(Tok::RParen, ")", i);
        break;
      case ',':
        push(Tok::Comma, ",", i);
        break;
      case ':':
        push(Tok::Colon, ":", i);
        break;
      case '^':
        push(Tok::Caret, "^", i);
        break;
      case '\\':
        push(Tok::Backslash, "\\", i);
        break;
      case '=':
        push(Tok::Equals, "=", i);
        break;
      case '+':
        push(Tok::Plus, "+", i);
        break;
      case '-':
        push(Tok::Minus, "-", i);
        break;
      case '.':
        if (i + 1 < text.size() && text[i + 1] == '.') {
          push(Tok::DotDot, "..", i);
          ++i;
          break;
        }
        [[fallthrough]];
      default: {
        Token t;
        t.line = line;
        t.column = column(i);
        throw Abort{Failure{std::string("unexpected character '") + c + "'", t.line, t.column}};
      }
    }
    ++i;
  }
  out.push_back(Token{Tok::End, "", 0, line, column(i)});
  return out;
}

// ===========================================================================
// Evaluator (parse and evaluate in one pass: the language has no values that outlive it)
// ===========================================================================

using MoveSet = std::vector<MoveAtom>;

constexpr int kMaxDepth = 16;        // names defined in terms of names
constexpr long kMaxLoop = 64;        // a bounded union is bounded
constexpr std::size_t kMaxAtoms = 4096;

struct Binding {
  std::string name;
  long value;
};

class Evaluator {
 public:
  Evaluator(const MovesetContext& ctx, int depth) : ctx_(ctx), depth_(depth) {}

  /// Evaluate a whole text: statements, or one expression.
  MoveSet run(std::string_view text) {
    tokens_ = lex(text);
    pos_ = 0;
    MoveSet last;
    bool any = false;
    skipNewlines();
    while (peek().kind != Tok::End) {
      // `name = expression` defines a name for the lines below; anything else is the value.
      if (peek().kind == Tok::Ident && peek(1).kind == Tok::Equals) {
        const std::string name = peek().text;
        pos_ += 2;
        defs_[name] = expression();
        last = defs_[name];
      } else {
        last = expression();
      }
      any = true;
      if (peek().kind != Tok::Newline && peek().kind != Tok::End) {
        fail_at(peek(), "expected the end of the line, found '" + describe(peek()) + "'");
      }
      skipNewlines();
    }
    if (!any) fail_at(peek(), "a moveset needs at least one move");
    return last;
  }

 private:
  const MovesetContext& ctx_;
  int depth_;
  std::vector<Token> tokens_;
  std::size_t pos_{0};
  std::map<std::string, MoveSet> defs_;
  std::vector<Binding> loopVars_;

  [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
    return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)];
  }
  const Token& take() { return tokens_[std::min(pos_++, tokens_.size() - 1)]; }
  void skipNewlines() {
    while (peek().kind == Tok::Newline) ++pos_;
  }
  static std::string describe(const Token& t) {
    return t.kind == Tok::End ? "the end" : t.kind == Tok::Newline ? "the end of the line" : t.text;
  }
  const Token& expect(Tok k, const char* what) {
    if (peek().kind != k) fail_at(peek(), std::string("expected ") + what + ", found '" + describe(peek()) + "'");
    return take();
  }
  [[nodiscard]] bool isWord(const char* w, std::size_t ahead = 0) const {
    return peek(ahead).kind == Tok::Ident && peek(ahead).text == w;
  }

  // ---- sets ---------------------------------------------------------------

  static void add(MoveSet& into, const MoveAtom& a) {
    for (const MoveAtom& have : into) {
      if (have == a) return;
    }
    into.push_back(a);
  }

  MoveSet expression() {
    MoveSet result = difference();
    // A line may continue with `U` or `\` at its start, which reads naturally for a long
    // union written one term per line.
    while (true) {
      std::size_t save = pos_;
      skipNewlines();
      if (isWord("U") && !(peek(1).kind == Tok::Ident && peek(2).kind == Tok::Ident &&
                            peek(2).text == "in")) {
        take();
        for (const MoveAtom& a : difference()) add(result, a);
        if (result.size() > kMaxAtoms) fail_at(peek(), "that moveset is too large");
        continue;
      }
      pos_ = save;
      break;
    }
    return result;
  }

  MoveSet difference() {
    MoveSet result = primary();
    while (true) {
      std::size_t save = pos_;
      skipNewlines();
      if (peek().kind == Tok::Backslash) {
        const Token op = take();
        const MoveSet minus = primary();
        MoveSet kept;
        for (const MoveAtom& a : result) {
          if (std::find(minus.begin(), minus.end(), a) == minus.end()) kept.push_back(a);
        }
        if (kept.size() == result.size()) {
          fail_at(op, "that difference removes nothing - the moves on the right are not "
                      "among those on the left");
        }
        result = std::move(kept);
        continue;
      }
      pos_ = save;
      break;
    }
    return result;
  }

  MoveSet primary() {
    skipNewlines();
    const Token& t = peek();
    if (t.kind == Tok::LParen) {
      take();
      MoveSet inner = expression();
      skipNewlines();
      expect(Tok::RParen, "')'");
      return inner;
    }
    // A bounded union: `U r in 1..D : body`.
    if (isWord("U") && peek(1).kind == Tok::Ident && peek(2).kind == Tok::Ident &&
        peek(2).text == "in") {
      take();
      const Token var = take();
      take();  // in
      const long lo = intExpr();
      expect(Tok::DotDot, "'..' between the bounds");
      const long hi = intExpr();
      expect(Tok::Colon, "':' before the body");
      if (hi - lo + 1 > kMaxLoop) fail_at(var, "a bounded union runs at most 64 times");
      // Remember where the body starts so each iteration re-reads it with the variable bound.
      const std::size_t bodyStart = pos_;
      MoveSet result;
      std::size_t bodyEnd = pos_;
      if (hi < lo) {
        // An empty range still has a body to step over; read it once with the variable at lo.
        loopVars_.push_back({var.text, lo});
        (void)expression();
        loopVars_.pop_back();
        return result;
      }
      for (long v = lo; v <= hi; ++v) {
        pos_ = bodyStart;
        loopVars_.push_back({var.text, v});
        const MoveSet body = expression();
        loopVars_.pop_back();
        bodyEnd = pos_;
        for (const MoveAtom& a : body) add(result, a);
        if (result.size() > kMaxAtoms) fail_at(var, "that moveset is too large");
      }
      pos_ = bodyEnd;
      return result;
    }
    if (t.kind == Tok::LBracket || isWord("exact") || isWord("expanded")) return templateAtom();
    if (t.kind == Tok::Ident) return reference();
    fail_at(t, "expected a move - [1,2], a name, or ( ... ) - found '" + describe(t) + "'");
  }

  MoveSet reference() {
    const Token name = take();
    if (const auto local = defs_.find(name.text); local != defs_.end()) return local->second;
    for (const Binding& b : loopVars_) {
      if (b.name == name.text) {
        fail_at(name, "'" + name.text + "' is a number here, not a moveset");
      }
    }
    if (depth_ >= kMaxDepth) fail_at(name, "names are defined in terms of each other too deeply");
    if (ctx_.lookup) {
      if (const auto text = ctx_.lookup(name.text)) {
        Evaluator inner(ctx_, depth_ + 1);
        try {
          return inner.run(*text);
        } catch (const Abort& a) {
          fail_at(name, "in '" + name.text + "' (line " + std::to_string(a.why.line) +
                            "): " + a.why.message);
        }
      }
    }
    fail_at(name, "no moveset is called '" + name.text + "'");
  }

  // ---- integers -----------------------------------------------------------

  long intTerm() {
    const Token& t = peek();
    if (t.kind == Tok::Int) {
      take();
      return t.value;
    }
    if (t.kind == Tok::LParen) {
      take();
      const long v = intExpr();
      expect(Tok::RParen, "')'");
      return v;
    }
    if (t.kind == Tok::Ident) {
      for (auto it = loopVars_.rbegin(); it != loopVars_.rend(); ++it) {
        if (it->name == t.text) {
          take();
          return it->value;
        }
      }
      if (t.text == "D") {
        take();
        return ctx_.dims;
      }
    }
    fail_at(t, "expected a number, D, or a union variable, found '" + describe(t) + "'");
  }

  long intExpr() {
    long v = intTerm();
    while (peek().kind == Tok::Plus || peek().kind == Tok::Minus) {
      const bool plus = take().kind == Tok::Plus;
      const long r = intTerm();
      v = plus ? v + r : v - r;
    }
    return v;
  }

  // ---- one move -----------------------------------------------------------

  int axisIndex(const Token& name) const {
    for (std::size_t i = 0; i < ctx_.axisNames.size(); ++i) {
      if (ctx_.axisNames[i] == name.text) return static_cast<int>(i);
    }
    fail_at(name, "no axis is called '" + name.text + "' on this board");
  }

  MoveSet templateAtom() {
    const Token start = peek();
    MoveAtom atom;
    if (isWord("exact")) {
      take();
      atom.kind = MoveKind::Exact;
    } else if (isWord("expanded")) {
      take();
      atom.kind = MoveKind::Expanded;
    }
    expect(Tok::LBracket, "'[' to open the vector");

    const bool literal = atom.kind != MoveKind::Extrapolated;
    std::set<int> pinned;
    std::size_t entry = 0;
    bool first = true;
    skipNewlines();
    while (peek().kind != Tok::RBracket) {
      if (peek().kind == Tok::End) fail_at(peek(), "expected ']' to close the vector");
      if (!first) {
        expect(Tok::Comma, "',' between components, or ']'");
        skipNewlines();
      }
      first = false;
      // A named, signed component: rank:+1.
      if (peek().kind == Tok::Ident && peek(1).kind == Tok::Colon) {
        const Token axisName = take();
        take();
        long sign = 1;
        if (peek().kind == Tok::Plus) {
          take();
        } else if (peek().kind == Tok::Minus) {
          take();
          sign = -1;
        }
        const Token num = expect(Tok::Int, "a number after the axis");
        const int axis = axisIndex(axisName);
        if (!pinned.insert(axis).second) {
          fail_at(axisName, "'" + axisName.text + "' is pinned twice in one move");
        }
        atom.pins.push(Pin{static_cast<std::uint8_t>(axis),
                           static_cast<std::int16_t>(sign * num.value)});
      } else {
        long sign = 1;
        bool signedLit = false;
        if (peek().kind == Tok::Plus) {
          take();
          signedLit = true;
        } else if (peek().kind == Tok::Minus) {
          take();
          sign = -1;
          signedLit = true;
        }
        const Token numTok = peek();
        const long n = intTerm();
        if (literal) {
          // Positional: this entry is the next axis, signed.
          if (n != 0) {
            const int axis = static_cast<int>(entry);
            if (axis >= static_cast<int>(ctx_.dims)) {
              fail_at(numTok, "this vector has more entries than the board has axes");
            }
            if (!pinned.insert(axis).second) {
              fail_at(numTok, "that axis is already given by name in this move");
            }
            atom.pins.push(Pin{static_cast<std::uint8_t>(axis),
                               static_cast<std::int16_t>(sign * n)});
          }
        } else {
          if (signedLit) {
            fail_at(numTok, "a free magnitude is unsigned; pin a direction with axis:+1");
          }
          long copies = 1;
          if (isWord("x")) {
            take();
            copies = intTerm();
          }
          if (n <= 0) fail_at(numTok, "a free magnitude is at least 1");
          if (copies < 1) fail_at(numTok, "a component is repeated at least once");
          for (long c = 0; c < copies; ++c) {
            if (atom.mags.size() >= kMaxDims) {
              fail_at(numTok, "a move has at most " + std::to_string(kMaxDims) + " components");
            }
            atom.mags.push(static_cast<std::int16_t>(n));
          }
        }
        ++entry;
      }
      skipNewlines();
    }
    expect(Tok::RBracket, "']'");

    // The step range.
    std::uint32_t minK = 1;
    std::uint32_t maxK = 1;
    bool rangeGiven = false;
    if (peek().kind == Tok::Caret) {
      take();
      rangeGiven = true;
      if (literal && atom.kind == MoveKind::Exact) {
        fail_at(peek(), "an exact move is taken once and has no step range");
      }
      if (isWord("k")) {
        take();
        maxK = kUnlimited;
      } else {
        const Token at = peek();
        const long lo = intExpr();
        if (peek().kind == Tok::DotDot) {
          take();
          minK = static_cast<std::uint32_t>(std::max(0L, lo));
          if (isWord("inf")) {
            take();
            maxK = kUnlimited;
          } else {
            const long hi = intExpr();
            if (hi < 1) fail_at(at, "a step range must reach at least 1");
            maxK = static_cast<std::uint32_t>(hi);
          }
          if (lo < 1) fail_at(at, "a step range starts at 1 or more");
        } else {
          if (lo < 1) fail_at(at, "a step count is at least 1");
          maxK = static_cast<std::uint32_t>(lo);
        }
      }
    } else if (atom.kind == MoveKind::Expanded) {
      maxK = kUnlimited;  // an expanded move with no range runs as far as it can
    }
    atom.minK = minK;
    atom.maxK = maxK;
    (void)rangeGiven;

    // Flags, in any order, until the move ends.
    std::optional<MoveMode> mode;
    std::optional<bool> flip;
    while (peek().kind == Tok::Ident && peek().text != "U" && peek().text != "x") {
      const Token f = take();
      if (f.text == "slide") {
        mode = MoveMode::Slide;
      } else if (f.text == "leap") {
        mode = MoveMode::Leap;
      } else if (f.text == "hop") {
        mode = MoveMode::Hop;
      } else if (f.text == "may") {
        atom.capture = CapturePolicy::May;
      } else if (f.text == "must") {
        atom.capture = CapturePolicy::Must;
      } else if (f.text == "cannot") {
        atom.capture = CapturePolicy::Cannot;
      } else if (f.text == "flip") {
        flip = true;
      } else if (f.text == "noflip") {
        flip = false;
      } else if (f.text == "forward") {
        atom.oriented = true;
      } else if (f.text == "positive") {
        atom.sign = StepSign::Positive;
      } else if (f.text == "negative") {
        atom.sign = StepSign::Negative;
      } else if (f.text == "both") {
        atom.sign = StepSign::Both;
      } else if (f.text == "fromrank") {
        // Legacy spelling of "only from the home rank", until `unmoved` replaces it.
        const long rank = intTerm();
        if (ctx_.orientAxis < 0) fail_at(f, "'fromrank' needs the variant to declare an orientation axis");
        atom.fromRegion[static_cast<std::size_t>(Color::White)] =
            Region{ctx_.orientAxis, static_cast<std::int16_t>(rank)};
      } else if (f.text == "unmoved") {
        atom.unmoved = true;
      } else if (f.text == "epmark") {
        atom.leavesEnPassant = true;
      } else {
        fail_at(f, "unknown word '" + f.text +
                       "' after a move (known: slide leap hop may must cannot flip noflip "
                       "forward unmoved positive negative both)");
      }
    }

    // Defaults, the same ones `printMoveset` leaves out.
    atom.mode = mode.value_or(defaultMode(atom));
    bool names = false;
    for (const Pin& p : atom.pins) names = names || static_cast<int>(p.axis) == ctx_.orientAxis;
    atom.flip = flip.value_or(ctx_.orientAxis >= 0 && names);
    if (atom.flip && ctx_.orientAxis < 0) {
      fail_at(start, "'flip' needs the variant to declare an orientation axis");
    }
    if (atom.oriented && ctx_.orientAxis < 0) {
      fail_at(start, "'forward' needs the variant to declare an orientation axis");
    }

    auto canon = MoveAtom::canonicalize(atom);
    if (!canon.has_value()) fail_at(start, canon.error().message);
    return MoveSet{*canon};
  }

  static MoveMode defaultMode(const MoveAtom& a) {
    if (a.kind == MoveKind::Exact) return MoveMode::Leap;
    return a.maxK == 1 ? MoveMode::Leap : MoveMode::Slide;
  }

 public:
  static MoveMode defaultModeFor(const MoveAtom& a) { return defaultMode(a); }
};

}  // namespace

Result<std::vector<MoveAtom>> evalMoveset(std::string_view text, const MovesetContext& ctx) {
  try {
    Evaluator ev(ctx, 0);
    return ev.run(text);
  } catch (const Abort& a) {
    std::string msg = a.why.message;
    if (a.why.column > 0) msg += " (column " + std::to_string(a.why.column) + ")";
    return fail(ErrorCode::ParseError, std::move(msg), a.why.line);
  }
}

// ===========================================================================
// Printer
// ===========================================================================

std::string printMoveset(const std::vector<MoveAtom>& atoms,
                         const std::vector<std::string>& axisNames, int orientAxis) {
  std::string out;
  for (std::size_t n = 0; n < atoms.size(); ++n) {
    const MoveAtom& a = atoms[n];
    if (n != 0) out += " U ";
    if (a.kind == MoveKind::Exact) out += "exact ";
    if (a.kind == MoveKind::Expanded) out += "expanded ";
    out += '[';
    bool first = true;
    const auto sep = [&] {
      if (!first) out += ',';
      first = false;
    };
    const auto axisText = [&](std::uint8_t axis) {
      return axis < axisNames.size() ? axisNames[axis] : "axis" + std::to_string(axis);
    };
    for (const Pin& p : a.pins) {
      sep();
      out += axisText(p.axis) + ":" + (p.value > 0 ? "+" : "") + std::to_string(p.value);
    }
    for (const auto m : a.mags) {
      sep();
      out += std::to_string(m);
    }
    out += ']';
    if (a.kind != MoveKind::Exact) {
      if (a.minK == 1 && a.maxK == kUnlimited) {
        out += "^k";
      } else if (a.minK == 1) {
        out += "^" + std::to_string(a.maxK);
      } else {
        out += "^" + std::to_string(a.minK) + ".." +
               (a.maxK == kUnlimited ? std::string("inf") : std::to_string(a.maxK));
      }
    }
    // Flags that differ from what the reader would assume.
    MoveAtom probe = a;
    const MoveMode dm = Evaluator::defaultModeFor(probe);
    if (a.mode != dm) {
      out += a.mode == MoveMode::Slide ? " slide" : a.mode == MoveMode::Hop ? " hop" : " leap";
    }
    if (a.capture == CapturePolicy::Must) out += " must";
    if (a.capture == CapturePolicy::Cannot) out += " cannot";
    bool names = false;
    for (const Pin& p : a.pins) names = names || static_cast<int>(p.axis) == orientAxis;
    const bool flipDefault = orientAxis >= 0 && names;
    if (a.flip != flipDefault) out += a.flip ? " flip" : " noflip";
    if (a.oriented) out += " forward";
    if (a.kind == MoveKind::Expanded && a.sign != StepSign::Both) {
      out += a.sign == StepSign::Positive ? " positive" : " negative";
    }
    if (a.fromRegion[static_cast<std::size_t>(Color::White)].active()) {
      out += " fromrank " + std::to_string(a.fromRegion[static_cast<std::size_t>(Color::White)].value);
    }
    if (a.unmoved) out += " unmoved";
    if (a.leavesEnPassant) out += " epmark";
  }
  return out;
}

}  // namespace cb
