// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/lab_doc.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <toml++/toml.hpp>
#include "io/obj_mesh.hpp"
namespace cb::io {
const LabStation* LabDoc::station(std::string_view name) const noexcept {
  for (const auto& s : stations)
    if (s.name == name) return &s;
  return nullptr;
}
std::optional<std::array<float, 3>> LabDoc::anchor(std::string_view name) const {
  for (const auto& a : anchors)
    if (a.first == name) return a.second;
  return {};
}
namespace {
int lineOf(const toml::node& n) {
  return static_cast<int>(n.source().begin.line);
}
struct Reader {
  std::string context;
  [[noreturn]] void bad(const toml::node& n, const std::string& message) const {
    throw Error{ErrorCode::ValidationError, context + ": " + message, lineOf(n)};
  }
  const toml::node& need(const toml::table& t, std::string_view key) const {
    if (const auto* n = t.get(key)) return *n;
    bad(t, "missing '" + std::string(key) + "'");
  }
  std::string string(const toml::node& n) const {
    auto s = n.value<std::string>();
    if (!s) bad(n, "expected string");
    return *s;
  }
  float number(const toml::node& n) const {
    auto x = n.value<double>();
    if (!x || !std::isfinite(*x) || !std::isfinite(static_cast<float>(*x)))
      bad(n, "expected finite number");
    return static_cast<float>(*x);
  }
  std::array<float, 3> triple(const toml::node& n) const {
    const auto* a = n.as_array();
    if (!a || a->size() != 3) bad(n, "expected three numbers");
    return {number(*a->get(0)), number(*a->get(1)), number(*a->get(2))};
  }
  view::Rgba color(const toml::node& n, bool linearHex = false) const {
    if (n.is_string()) {
      const auto s = string(n);
      std::uint32_t rgb{};
      if (s.size() != 7 || s[0] != '#') bad(n, "color needs #RRGGBB");
      auto [end, ec] = std::from_chars(s.data() + 1, s.data() + 7, rgb, 16);
      if (ec != std::errc{} || end != s.data() + 7) bad(n, "invalid color");
      auto c = view::Rgba::hex(rgb);
      if (linearHex) {
        const auto linear = [](float x) {
          return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
        };
        c.r = linear(c.r);
        c.g = linear(c.g);
        c.b = linear(c.b);
      }
      return c;
    }
    const auto c = triple(n);
    for (auto x : c)
      if (x < 0 || x > 1) bad(n, "color outside 0..1");
    return {c[0], c[1], c[2], 1};
  }
  void keys(const toml::table& t, std::initializer_list<std::string_view> allowed) const {
    for (const auto& [k, v] : t)
      if (std::find(allowed.begin(), allowed.end(), k.str()) == allowed.end())
        bad(v, "unknown key '" + std::string(k.str()) + "'");
  }
  LabMesh mesh(const toml::node& n, bool fallback = false) const {
    const auto s = string(n);
    if (s == "box") return LabMesh::Box;
    if (s == "bevel") return LabMesh::Bevel;
    if (s == "cylinder") return LabMesh::Cylinder;
    if (!fallback && s == "slab") return LabMesh::Slab;
    if (!fallback && s.starts_with("file:") && s.size() > 5) return LabMesh::File;
    bad(n, "unknown mesh '" + s + "'");
  }
};
}  // namespace
Result<LabDoc> parseLabDoc(std::string_view text, std::string_view sourceName) {
  try {
    auto root = toml::parse(text, sourceName);
    Reader r{std::string(sourceName)};
    r.keys(root, {"version", "anchors", "part", "station", "light"});
    if (r.need(root, "version").value<int>() != 1)
      r.bad(r.need(root, "version"), "version must be 1");
    LabDoc doc;
    if (const auto* n = root.get("anchors")) {
      const auto* anchors = n->as_table();
      if (!anchors) r.bad(*n, "anchors must be a table");
      for (const auto& [k, v] : *anchors) doc.anchors.emplace_back(k.str(), r.triple(v));
    }
    std::set<std::string> names;
    if (const auto* n = root.get("part")) {
      const auto* list = n->as_array();
      if (!list) r.bad(*n, "part must be an array of tables");
      for (const auto& item : *list) {
        const auto* t = item.as_table();
        if (!t) r.bad(item, "part must be a table");
        LabPart p;
        p.name = r.string(r.need(*t, "name"));
        r.context = std::string(sourceName) + " part '" + p.name + "'";
        if (p.name.empty() ||
            p.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") !=
                std::string::npos)
          r.bad(item, "invalid name");
        if (!names.insert(p.name).second) r.bad(item, "duplicate name");
        r.keys(*t, {"name", "mesh", "pos", "size", "rot", "material", "color", "ao",
                    "group", "fallback", "focus"});
        p.mesh = r.mesh(r.need(*t, "mesh"));
        p.pos = r.triple(r.need(*t, "pos"));
        p.size = r.triple(r.need(*t, "size"));
        for (float s : p.size)
          if (s <= 0) r.bad(r.need(*t, "size"), "size must be positive");
        if (auto* v = t->get("rot")) p.rot = r.triple(*v);
        if (auto* v = t->get("focus")) p.focus = r.string(*v);
        if (auto* v = t->get("ao")) {
          p.ao = r.number(*v);
          if (p.ao < 0 || p.ao > 1) r.bad(*v, "ao outside 0..1");
        }
        if (auto* v = t->get("group")) p.group = r.string(*v);
        if (auto* v = t->get("fallback")) p.fallback = r.mesh(*v, true);
        if (auto* v = t->get("material")) {
          const auto s = r.string(*v);
          const std::array<std::string_view, 7> materials{
              "matte", "metal", "glass", "glow", "hologram", "led", "sky"};
          auto it = std::find(materials.begin(), materials.end(), s);
          if (it == materials.end()) r.bad(*v, "unknown material '" + s + "'");
          p.material = static_cast<LabMaterial>(it - materials.begin());
        }
        if (auto* v = t->get("color")) p.color = r.color(*v);
        if (p.mesh == LabMesh::File) {
          const auto path = std::filesystem::path(r.string(r.need(*t, "mesh")).substr(5));
          if (path.is_absolute()) r.bad(r.need(*t, "mesh"), "file path must be relative");
          p.file = std::filesystem::absolute(
                       std::filesystem::path(sourceName).parent_path() / path)
                       .lexically_normal();
          std::ifstream file(p.file);
          if (!file) {
            if (!p.fallback)
              r.bad(r.need(*t, "mesh"),
                    "missing OBJ '" + p.file.string() + "' (OBJ line 0)");
            std::fprintf(stderr, "lab: part '%s': missing %s; using fallback\n",
                         p.name.c_str(), p.file.c_str());
            p.mesh = *p.fallback;
          } else {
            std::ostringstream buffer;
            buffer << file.rdbuf();
            const auto mesh = parseObj(buffer.str(), p.file.string());
            if (!mesh) r.bad(r.need(*t, "mesh"), mesh.error().format());
          }
        }
        doc.parts.push_back(std::move(p));
      }
    }
    names.clear();
    if (const auto* n = root.get("light")) {
      r.context = std::string(sourceName) + " lights";
      const auto* list = n->as_array();
      if (!list) r.bad(*n, "light must be an array of tables");
      if (list->size() > 8) r.bad(*list->get(8), "at most 8 lights");
      for (const auto& item : *list) {
        const auto* t = item.as_table();
        if (!t) r.bad(item, "light must be a table");
        LabLight l;
        l.name = r.string(r.need(*t, "name"));
        r.context = std::string(sourceName) + " light '" + l.name + "'";
        if (l.name.empty() || !names.insert(l.name).second)
          r.bad(item, "empty or duplicate name");
        r.keys(*t, {"name", "kind", "pos", "dir", "cone", "soft", "color", "intensity",
                    "radius", "group"});
        const auto kind = r.string(r.need(*t, "kind"));
        if (kind == "spot")
          l.kind = LabLight::Kind::Spot;
        else if (kind != "point")
          r.bad(r.need(*t, "kind"), "unknown light kind");
        l.pos = r.triple(r.need(*t, "pos"));
        if (auto* v = t->get("color")) l.color = r.color(*v, true);
        if (auto* v = t->get("intensity")) l.intensity = r.number(*v);
        l.radius = r.number(r.need(*t, "radius"));
        if (l.radius <= 0) r.bad(r.need(*t, "radius"), "radius must be positive");
        if (l.intensity < 0)
          r.bad(r.need(*t, "intensity"), "intensity must be nonnegative");
        if (auto* v = t->get("group")) l.group = r.string(*v);
        if (l.group != "room" && l.group != "machine")
          r.bad(r.need(*t, "group"), "group must be room or machine");
        if (l.kind == LabLight::Kind::Spot) {
          l.dir = r.triple(r.need(*t, "dir"));
          if (l.dir == std::array<float, 3>{})
            r.bad(r.need(*t, "dir"), "spot direction must be nonzero");
          if (auto* v = t->get("cone")) l.cone = r.number(*v);
          if (auto* v = t->get("soft")) l.soft = r.number(*v);
          if (l.cone <= 0 || l.cone > 89)
            r.bad(r.need(*t, "cone"), "cone outside (0,89]");
          if (l.soft < 0 || l.soft > 45) r.bad(r.need(*t, "soft"), "soft outside [0,45]");
        }
        doc.lights.push_back(std::move(l));
      }
    }
    names.clear();
    if (const auto* n = root.get("station")) {
      const auto* list = n->as_array();
      if (!list) r.bad(*n, "station must be an array of tables");
      for (const auto& item : *list) {
        const auto* t = item.as_table();
        if (!t) r.bad(item, "station must be a table");
        LabStation s;
        s.name = r.string(r.need(*t, "name"));
        r.context = std::string(sourceName) + " station '" + s.name + "'";
        r.keys(*t, {"name", "target", "distance", "yaw", "pitch", "fov"});
        if (!names.insert(s.name).second) r.bad(item, "duplicate name");
        s.target = r.triple(r.need(*t, "target"));
        s.distance = r.number(r.need(*t, "distance"));
        s.yaw = r.number(r.need(*t, "yaw"));
        s.pitch = r.number(r.need(*t, "pitch"));
        s.fov = r.number(r.need(*t, "fov"));
        if (s.distance <= 0) r.bad(r.need(*t, "distance"), "distance must be positive");
        if (s.fov <= 10 || s.fov >= 120) r.bad(r.need(*t, "fov"), "fov outside (10,120)");
        if (s.pitch <= -89 || s.pitch >= 89)
          r.bad(r.need(*t, "pitch"), "pitch outside (-89,89)");
        doc.stations.push_back(std::move(s));
      }
    }
    return doc;
  } catch (const toml::parse_error& e) {
    return fail(ErrorCode::ParseError, std::string(e.description()),
                static_cast<int>(e.source().begin.line));
  } catch (const Error& e) {
    return std::unexpected(e);
  }
}
Result<LabDoc> loadLabDoc(const std::filesystem::path& file) {
  std::ifstream input(file);
  if (!input)
    return fail(ErrorCode::ParseError, "cannot open lab '" + file.string() + "'");
  std::ostringstream text;
  text << input.rdbuf();
  return parseLabDoc(text.str(), file.string());
}
}  // namespace cb::io
