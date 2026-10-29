// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/camera_track.hpp"

#include <algorithm>
#include <sstream>

namespace cb::app {
namespace {

std::string_view trim(std::string_view s) {
  const std::size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const std::size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

bool less(const CameraCue& a, const CameraCue& b) noexcept {
  if (a.variant != b.variant) return a.variant < b.variant;
  if (a.beat != b.beat) return a.beat < b.beat;
  if (a.ply != b.ply) return a.ply < b.ply;
  return a.sub < b.sub;
}

/// Parse one `key=value` token from a whitespace-split keyframe line. `ok` is cleared on
/// a malformed number rather than thrown past the caller's own loop - one line's typo
/// should not be fatal to every keyframe after it in a hand-edited file.
float parseFloat(std::string_view value, bool& ok) {
  try {
    return std::stof(std::string(value));
  } catch (const std::exception&) {
    ok = false;
    return 0.0f;
  }
}

std::size_t parseSize(std::string_view value, bool& ok) {
  try {
    return static_cast<std::size_t>(std::stoull(std::string(value)));
  } catch (const std::exception&) {
    ok = false;
    return 0;
  }
}

view::Vec3 parseVec3(std::string_view value, bool& ok) {
  view::Vec3 v;
  const std::size_t c1 = value.find(',');
  const std::size_t c2 =
      c1 == std::string_view::npos ? std::string_view::npos : value.find(',', c1 + 1);
  if (c1 == std::string_view::npos || c2 == std::string_view::npos) {
    ok = false;
    return v;
  }
  v.x = parseFloat(value.substr(0, c1), ok);
  v.y = parseFloat(value.substr(c1 + 1, c2 - c1 - 1), ok);
  v.z = parseFloat(value.substr(c2 + 1), ok);
  return v;
}

std::string vec3Text(const view::Vec3& v) {
  std::ostringstream out;
  out << v.x << ',' << v.y << ',' << v.z;
  return out.str();
}

}  // namespace

void CameraTrack::set(CameraKeyframe kf) {
  const auto it = std::find_if(keyframes_.begin(), keyframes_.end(),
                               [&](const CameraKeyframe& k) { return k.cue == kf.cue; });
  if (it != keyframes_.end()) {
    *it = std::move(kf);
    return;
  }
  const auto pos = std::upper_bound(keyframes_.begin(), keyframes_.end(), kf,
                                    [](const CameraKeyframe& a, const CameraKeyframe& b) {
                                      return less(a.cue, b.cue);
                                    });
  keyframes_.insert(pos, std::move(kf));
}

bool CameraTrack::erase(const CameraCue& cue) {
  const auto it = std::find_if(keyframes_.begin(), keyframes_.end(),
                               [&](const CameraKeyframe& k) { return k.cue == cue; });
  if (it == keyframes_.end()) return false;
  keyframes_.erase(it);
  return true;
}

const CameraKeyframe* CameraTrack::find(const CameraCue& cue) const noexcept {
  const auto it = std::find_if(keyframes_.begin(), keyframes_.end(),
                               [&](const CameraKeyframe& k) { return k.cue == cue; });
  return it == keyframes_.end() ? nullptr : &*it;
}

std::string cameraTrackText(const CameraTrack& track) {
  std::ostringstream out;
  out << "# EndspielZero camera track v1\n";
  for (const CameraKeyframe& kf : track.keyframes()) {
    out << "keyframe"
        << " variant=" << kf.cue.variant;
    if (kf.cue.beat != 0) out << " beat=" << kf.cue.beat;
    out << " ply=" << kf.cue.ply;
    if (kf.cue.sub != 0) out << " sub=" << kf.cue.sub;
    out << " target=" << vec3Text(kf.camera.target) << " distance=" << kf.camera.distance
        << " yaw=" << kf.camera.yaw << " pitch=" << kf.camera.pitch
        << " roll=" << kf.camera.roll << " fov=" << kf.camera.fovY
        << " formed=" << kf.formed << " ghost=" << kf.ghost
        << " shellSpread=" << kf.shellSpread << " slideU=" << kf.slideU
        << " slideV=" << kf.slideV << " flatPieces=" << (kf.flatPieces ? 1 : 0)
        << " invert=" << (kf.invert ? 1 : 0) << " speed=" << kf.morphSpeed << '\n';
  }
  return out.str();
}

Result<CameraTrack> parseCameraTrack(std::string_view text) {
  CameraTrack track;
  std::istringstream in{std::string(text)};
  std::string line;
  std::size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    const std::string_view view = trim(line);
    if (view.empty() || view.front() == '#') continue;
    const std::size_t firstSpace = view.find(' ');
    const std::string_view head =
        firstSpace == std::string_view::npos ? view : view.substr(0, firstSpace);
    if (head != "keyframe") {
      return fail(ErrorCode::ParseError, "camera track line " + std::to_string(lineNo) +
                                             ": unknown key '" + std::string(head) + "'");
    }
    CameraKeyframe kf;
    bool ok = true;
    bool sawVariant = false;
    std::string_view rest = firstSpace == std::string_view::npos
                                ? std::string_view{}
                                : view.substr(firstSpace + 1);
    while (!rest.empty()) {
      rest = trim(rest);
      if (rest.empty()) break;
      const std::size_t tokenEnd = rest.find(' ');
      const std::string_view token = rest.substr(0, tokenEnd);
      rest = tokenEnd == std::string_view::npos ? std::string_view{}
                                                : rest.substr(tokenEnd + 1);
      const std::size_t eq = token.find('=');
      if (eq == std::string_view::npos) {
        return fail(ErrorCode::ParseError, "camera track line " + std::to_string(lineNo) +
                                               ": malformed token '" +
                                               std::string(token) + "'");
      }
      const std::string_view key = token.substr(0, eq);
      const std::string_view value = token.substr(eq + 1);
      if (key == "variant") {
        kf.cue.variant = std::string(value);
        sawVariant = !value.empty();
      } else if (key == "beat") {
        kf.cue.beat = parseSize(value, ok);
      } else if (key == "sub") {
        kf.cue.sub = parseSize(value, ok);
      } else if (key == "ply") {
        kf.cue.ply = parseSize(value, ok);
      } else if (key == "target") {
        kf.camera.target = parseVec3(value, ok);
      } else if (key == "distance") {
        kf.camera.distance = parseFloat(value, ok);
      } else if (key == "yaw") {
        kf.camera.yaw = parseFloat(value, ok);
      } else if (key == "pitch") {
        kf.camera.pitch = parseFloat(value, ok);
      } else if (key == "roll") {
        kf.camera.roll = parseFloat(value, ok);
      } else if (key == "fov") {
        kf.camera.fovY = parseFloat(value, ok);
      } else if (key == "formed") {
        kf.formed = parseFloat(value, ok);
      } else if (key == "ghost") {
        kf.ghost = parseFloat(value, ok);
      } else if (key == "shellSpread") {
        kf.shellSpread = parseFloat(value, ok);
      } else if (key == "slideU") {
        kf.slideU = parseFloat(value, ok);
      } else if (key == "slideV") {
        kf.slideV = parseFloat(value, ok);
      } else if (key == "flatPieces") {
        kf.flatPieces = value == "1";
      } else if (key == "invert") {
        kf.invert = value == "1";
      } else if (key == "speed") {
        kf.morphSpeed = parseFloat(value, ok);
      } else {
        return fail(ErrorCode::ParseError, "camera track line " + std::to_string(lineNo) +
                                               ": unknown field '" + std::string(key) +
                                               "'");
      }
    }
    if (!sawVariant) {
      return fail(ErrorCode::ParseError, "camera track line " + std::to_string(lineNo) +
                                             ": keyframe names no variant");
    }
    if (!ok) {
      return fail(ErrorCode::ParseError,
                  "camera track line " + std::to_string(lineNo) + ": malformed number");
    }
    track.set(std::move(kf));
  }
  return track;
}

}  // namespace cb::app
