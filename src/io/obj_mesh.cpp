// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/obj_mesh.hpp"
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
namespace cb::io {
Result<ObjMesh> parseObj(std::string_view text, std::string_view name) {
  int line = 0;
  const auto error = [&](std::string message) -> Error {
    return {ErrorCode::ParseError, std::string(name) + ": " + message, line};
  };
  try {
    using V = std::array<float, 3>;
    std::vector<V> positions, normals;
    ObjMesh out;
    std::map<std::array<float, 6>, std::uint32_t> unique;
    const auto normalise = [&](V v) {
      const float length = std::hypot(v[0], v[1], v[2]);
      if (!std::isfinite(length) || length < 1e-20f)
        throw error("zero or invalid normal/face");
      for (auto& c : v) c /= length;
      return v;
    };
    const auto index = [&](std::string_view token, std::size_t count) {
      int n = 0;
      auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), n);
      if (ec != std::errc{} || end != token.data() + token.size() || n == 0)
        throw error("invalid index");
      const auto resolved = n < 0 ? static_cast<std::int64_t>(count) + n : n - 1;
      if (resolved < 0 || static_cast<std::uint64_t>(resolved) >= count)
        throw error("index out of range");
      return static_cast<std::size_t>(resolved);
    };
    std::istringstream input{std::string(text)};
    std::string row;
    while (std::getline(input, row)) {
      ++line;
      row.resize(row.find('#') == std::string::npos ? row.size() : row.find('#'));
      std::istringstream fields(row);
      std::string key;
      if (!(fields >> key)) continue;
      if (key == "v" || key == "vn") {
        V v{};
        for (auto& c : v) {
          std::string s;
          fields >> s;
          auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), c);
          if (ec != std::errc{} || end != s.data() + s.size() || !std::isfinite(c))
            throw error("invalid vertex/normal");
        }
        if (key == "vn")
          normals.push_back(normalise(v));
        else
          positions.push_back(v);
      } else if (key == "f") {
        struct Corner {
          std::size_t p;
          std::optional<std::size_t> n;
        };
        std::vector<Corner> face;
        std::string s;
        bool flat = false;
        while (fields >> s) {
          const auto slash = s.find('/');
          Corner c{index(std::string_view(s).substr(0, slash), positions.size()), {}};
          if (slash != std::string::npos) {
            const auto second = s.find('/', slash + 1);
            if (second == std::string::npos || second + 1 == s.size())
              throw error("expected a//n or a/t/n face");
            c.n = index(std::string_view(s).substr(second + 1), normals.size());
          }
          flat = flat || !c.n.has_value();
          face.push_back(c);
        }
        if (face.size() < 3) throw error("face needs at least three vertices");
        V normal{};
        if (flat) {
          // Newell's polygon normal also handles a collinear first triple.
          for (std::size_t i = 0; i < face.size(); ++i) {
            const V& a = positions[face[i].p];
            const V& b = positions[face[(i + 1) % face.size()].p];
            normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
            normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
            normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
          }
          normal = normalise(normal);
        }
        std::vector<std::uint32_t> ids;
        for (const auto& c : face) {
          const auto& p = positions[c.p];
          const auto& n = flat ? normal : normals[*c.n];
          const std::array<float, 6> pair{p[0], p[1], p[2], n[0], n[1], n[2]};
          auto [it, inserted] =
              unique.try_emplace(pair, static_cast<std::uint32_t>(out.positions.size()));
          if (inserted) {
            if (out.positions.size() >= 65535)
              throw error("more than 65,535 output vertices; decimate the model");
            out.positions.push_back(p);
            out.normals.push_back(n);
          }
          ids.push_back(it->second);
        }
        for (std::size_t i = 1; i + 1 < ids.size(); ++i)
          out.indices.insert(out.indices.end(), {ids[0], ids[i], ids[i + 1]});
      } else if (key != "o" && key != "g" && key != "s" && key != "usemtl" &&
                 key != "mtllib" && key != "vt") {
        throw error("unknown keyword '" + key + "'");
      }
    }
    if (out.indices.empty()) throw error("mesh has no faces");
    return out;
  } catch (const Error& e) {
    return std::unexpected(e);
  }
}
}  // namespace cb::io
