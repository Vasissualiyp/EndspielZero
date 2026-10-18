// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/rule_parse.hpp"

#include <charconv>
#include <map>
#include <vector>

namespace cb {
namespace {

using rules::Expr;
using rules::ExprOp;

struct Token {
  enum class Kind { Open, Close, Word, Number } kind{Kind::Word};
  std::string text;
  std::int64_t number{0};
};

Result<std::vector<Token>> tokenize(std::string_view text) {
  std::vector<Token> out;
  std::size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      ++i;
      continue;
    }
    if (c == '(') {
      out.push_back(Token{Token::Kind::Open, "(", 0});
      ++i;
      continue;
    }
    if (c == ')') {
      out.push_back(Token{Token::Kind::Close, ")", 0});
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])) == 0 &&
           text[j] != '(' && text[j] != ')') {
      ++j;
    }
    const std::string word(text.substr(i, j - i));
    std::int64_t value = 0;
    const auto* first = word.data();
    const auto* last = word.data() + word.size();
    if (const auto r = std::from_chars(first, last, value);
        r.ec == std::errc{} && r.ptr == last) {
      out.push_back(Token{Token::Kind::Number, word, value});
    } else {
      out.push_back(Token{Token::Kind::Word, word, 0});
    }
    i = j;
  }
  return out;
}

const std::map<std::string, ExprOp>& nullaryNames() {
  static const std::map<std::string, ExprOp> m{
      {"move.from", ExprOp::MoveFrom},
      {"move.to", ExprOp::MoveTo},
      {"move.capture_cell", ExprOp::MoveCaptureCell},
      {"mover.color", ExprOp::MoverColor},
      {"mover.type", ExprOp::MoverType},
      {"is_capture", ExprOp::IsCapture},
      {"cell", ExprOp::CellArg},
      {"side_to_move", ExprOp::SideToMove},
      {"halfmove_clock", ExprOp::HalfmoveClock},
      {"any_capture", ExprOp::AnyCapture},
  };
  return m;
}

const std::map<std::string, ExprOp>& opNames() {
  static const std::map<std::string, ExprOp> m{
      {"not", ExprOp::Not},
      {"and", ExprOp::And},
      {"or", ExprOp::Or},
      {"eq", ExprOp::Eq},
      {"ne", ExprOp::Ne},
      {"lt", ExprOp::Lt},
      {"gt", ExprOp::Gt},
      {"add", ExprOp::Add},
      {"sub", ExprOp::Sub},
      {"type_at", ExprOp::TypeAt},
      {"color_at", ExprOp::ColorAt},
      {"is_empty", ExprOp::IsEmpty},
      {"has_capture_from", ExprOp::HasCaptureFrom},
      {"attacked", ExprOp::Attacked},
  };
  return m;
}

class Parser {
 public:
  Parser(const VariantSpec& v, std::vector<Token> tokens)
      : v_(v), t_(std::move(tokens)) {}

  Result<Expr> parse() {
    auto e = parseOne();
    if (!e.has_value()) return e;
    if (i_ != t_.size()) {
      return fail(
          ErrorCode::ParseError,
          "trailing text after the expression, starting at '" + t_[i_].text + "'");
    }
    return e;
  }

 private:
  Result<Expr> parseOne() {
    if (i_ >= t_.size()) return fail(ErrorCode::ParseError, "expression ended early");
    const Token tok = t_[i_++];

    if (tok.kind == Token::Kind::Number) return Expr::constant(tok.number);
    if (tok.kind == Token::Kind::Close) {
      return fail(ErrorCode::ParseError, "unexpected ')'");
    }
    if (tok.kind == Token::Kind::Word) return atom(tok.text);

    // '(' op args... ')'
    if (i_ >= t_.size() || t_[i_].kind != Token::Kind::Word) {
      return fail(ErrorCode::ParseError, "'(' must be followed by an operator name");
    }
    const std::string op = t_[i_++].text;

    Expr e;
    // Three operators take a *name* first - a field or an axis - which becomes an
    // immediate rather than an argument.
    if (op == "piece_field" || op == "cell_field" || op == "coord" || op == "marked") {
      if (i_ >= t_.size() || t_[i_].kind != Token::Kind::Word) {
        return fail(ErrorCode::ParseError,
                    "'" + op + "' needs a name first, e.g. (" + op + " charge move.to)");
      }
      const std::string name = t_[i_++].text;
      int index = -1;
      if (op == "piece_field") {
        index = v_.findPieceField(name);
        e.op = ExprOp::PieceField;
      } else if (op == "cell_field") {
        index = v_.findCellField(name);
        e.op = ExprOp::CellField;
      } else if (op == "marked") {
        index = v_.findMarkTag(name);
        e.op = ExprOp::Marked;
      } else {
        index = v_.dims.axisIndex(name);
        e.op = ExprOp::CoordOf;
      }
      if (index < 0) {
        return fail(
            ErrorCode::ValidationError,
            "'" + op + "' names '" + name + "', which this variant does not declare");
      }
      e.imm = index;
    } else {
      const auto it = opNames().find(op);
      if (it == opNames().end()) {
        return fail(ErrorCode::ParseError, "unknown operator '" + op + "'");
      }
      e.op = it->second;
    }

    while (i_ < t_.size() && t_[i_].kind != Token::Kind::Close) {
      auto arg = parseOne();
      if (!arg.has_value()) return arg;
      e.args.push_back(std::move(*arg));
    }
    if (i_ >= t_.size()) return fail(ErrorCode::ParseError, "missing ')'");
    ++i_;  // consume ')'

    const int want = rules::arity(e.op);
    if (want >= 0 && static_cast<int>(e.args.size()) != want) {
      return fail(ErrorCode::ValidationError,
                  "'" + op + "' takes " + std::to_string(want) + " argument(s), not " +
                      std::to_string(e.args.size()));
    }
    return e;
  }

  Result<Expr> atom(const std::string& word) {
    if (const auto it = nullaryNames().find(word); it != nullaryNames().end()) {
      return Expr::nullary(it->second);
    }
    if (word.starts_with("piece:")) {
      const std::string name = word.substr(6);
      const PieceTypeId id = v_.findPiece(name);
      if (id == kNoPiece) {
        return fail(ErrorCode::ValidationError, "no piece is called '" + name + "'");
      }
      return Expr::constant(id);
    }
    if (word.starts_with("color:")) {
      const std::string name = word.substr(6);
      if (name == "white") return Expr::constant(static_cast<std::int64_t>(Color::White));
      if (name == "black") return Expr::constant(static_cast<std::int64_t>(Color::Black));
      return fail(ErrorCode::ValidationError,
                  "colour must be white or black, not '" + name + "'");
    }
    if (word == "empty") return Expr::constant(kNoPiece);
    if (word == "true") return Expr::constant(1);
    if (word == "false") return Expr::constant(0);
    return fail(ErrorCode::ParseError,
                "unknown name '" + word +
                    "'. Bare names are things like move.to, cell, is_capture, or a "
                    "reference such as piece:queen or color:white");
  }

  const VariantSpec& v_;
  std::vector<Token> t_;
  std::size_t i_{0};
};

}  // namespace

Result<rules::Expr> parseRuleExpr(const VariantSpec& v, std::string_view text) {
  auto tokens = tokenize(text);
  if (!tokens.has_value()) return fail(tokens.error().code, tokens.error().message);
  if (tokens->empty()) return fail(ErrorCode::ParseError, "empty expression");
  Parser parser(v, std::move(*tokens));
  return parser.parse();
}

}  // namespace cb
