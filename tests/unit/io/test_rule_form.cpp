// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.2.3: fields and rules edited through the document's form serialize to a variant
// that is the hand-written one, and every op the form offers is one the loader reads.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "io/rule_catalogue.hpp"
#include "io/variant_doc.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("charged's fields and rules rebuilt through the form are charged",
          "[unit][io]") {
  for (const char* name : {"charged", "atomic", "mustcapture"}) {
    CAPTURE(name);
    const std::string text = readFile(test::variantPath(name));
    auto source = VariantDoc::parse(text, name);
    REQUIRE(source.has_value());
    const auto pieceFields = source->fields(false);
    const auto rules = source->rules();
    REQUIRE_FALSE(rules.empty());

    // Strip them, then put them back the way the form does.
    auto doc = VariantDoc::parse(text, name);
    REQUIRE(doc.has_value());
    doc->setRules({});
    doc->setFields(false, {});
    CHECK(doc->rules().empty());
    doc->setFields(false, pieceFields);
    doc->setRules(rules);
    CHECK(doc->rules() == rules);

    const auto rebuilt = loadVariantToml(doc->serialize(), name);
    const auto original = loadVariantToml(text, name);
    REQUIRE(rebuilt.has_value());
    REQUIRE(original.has_value());
    CHECK(rebuilt->variantId() == original->variantId());
  }
}

TEST_CASE("every op the form offers is one the loader reads", "[unit][io]") {
  const std::string text = readFile(test::variantPath("charged"));
  for (const EffectSpec& spec : effectCatalogue()) {
    CAPTURE(spec.op);
    auto doc = VariantDoc::parse(text, "charged");
    REQUIRE(doc.has_value());
    doc->setFields(true, {{"charge", 0, 0, 8, true}});  // so set_cell_field has one too
    VariantDoc::EffectInfo e;
    e.op = std::string(spec.op);
    for (const EffectKey& k : spec.keys) {
      std::string value = "move.to";
      if (k.kind == "piece") value = "knight";
      if (k.kind == "field") value = "charge";
      if (k.kind == "outcome") value = "draw";
      if (k.kind == "number") value = "1";
      if (k.key == "affects" || k.key == "color" || k.key == "value") value = "1";
      e.args.emplace_back(std::string(k.key), value);
    }
    const std::string when = spec.op == "forbid_move" ? "on_move_filter" : "on_move_end";
    doc->setRules({VariantDoc::RuleInfo{"probe", when, "", {e}}});
    const auto loaded = loadVariantToml(doc->serialize(), "probe");
    if (!loaded.has_value()) FAIL(loaded.error().format());
  }
  for (const std::string_view when : ruleTriggers()) {
    CAPTURE(when);
    auto doc = VariantDoc::parse(text, "charged");
    REQUIRE(doc.has_value());
    // forbid_move only means anything before the move, repeat_turn only after it.
    const std::string op = when == "on_move_filter" ? "forbid_move" : "repeat_turn";
    doc->setRules({VariantDoc::RuleInfo{"probe", std::string(when), "", {{op, {}}}}});
    const auto loaded = loadVariantToml(doc->serialize(), "probe");
    if (!loaded.has_value()) FAIL(loaded.error().format());
  }
}

TEST_CASE("a rule the loader refuses is refused with its own words", "[unit][io]") {
  const std::string text = readFile(test::variantPath("charged"));
  auto doc = VariantDoc::parse(text, "charged");
  REQUIRE(doc.has_value());
  doc->setRules({VariantDoc::RuleInfo{
      "bad", "on_move_end", "(frobnicate move.to)", {{"repeat_turn", {}}}}});
  const auto loaded = loadVariantToml(doc->serialize(), "bad");
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().message.find("rule 'bad' condition") != std::string::npos);
}
