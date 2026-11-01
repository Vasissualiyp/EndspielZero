// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/check_ripple.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {

RippleCell checkRipple(int dist, float age, float intensity) {
  RippleCell r;
  if (intensity < 0.5f || dist < 0 || !(age >= 0.0f) || age >= kRippleSeconds) return r;
  const float dt = age - static_cast<float>(dist) / kRippleCellsPerSecond;
  const float x = dt / kRippleSigma;
  if (std::abs(x) > 4.0f) return r;
  // The tail of the wave dies out rather than being cut at the end of the second.
  const float tail = 1.0f - std::clamp((age - 0.75f) / (kRippleSeconds - 0.75f), 0.0f, 1.0f);
  const float amp = std::exp(-0.5f * x * x) * tail /
                    (1.0f + kRippleFalloff * static_cast<float>(dist));
  r.amp = amp;
  r.phase = std::clamp(x / 3.0f, -1.0f, 1.0f);
  r.tint = kRippleTintMax * amp;
  return r;
}

}  // namespace cb::view
