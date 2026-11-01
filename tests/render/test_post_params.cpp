// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "render/post_params.hpp"

using namespace cb::render;

TEST_CASE("the post shoulder preserves authored colours and compresses highlights",
          "[render][post]") {
  for (int i = 0; i <= 800; ++i) {
    const float x = static_cast<float>(i) / 1000.0f;
    CHECK(std::bit_cast<std::uint32_t>(toneMap(x)) == std::bit_cast<std::uint32_t>(x));
  }
  CHECK(toneMap(1.0f) > .85f);
  CHECK(std::abs(toneMap(std::nextafter(.8f, 1.0f)) - .8f) < 1e-6f);
  float previous = 0;
  for (int i = 0; i <= 10000; ++i) {
    const float y = toneMap(static_cast<float>(i) / 100.0f);
    CHECK(std::isfinite(y));
    CHECK(y >= previous);
    CHECK(y <= 1);
    previous = y;
  }
  CHECK(toneMap(std::numeric_limits<float>::max()) <= 1);
}

TEST_CASE("bloom levels are bounded and use the shorter target dimension",
          "[render][post]") {
  CHECK(bloomLevelCount(640, 360) == 5);
  CHECK(bloomLevelCount(1280, 720) == 6);
  CHECK(bloomLevelCount(1920, 1080) == 6);
  CHECK(bloomLevelCount(3840, 2160) == 6);
  CHECK(bloomLevelCount(360, 640) == 5);
  CHECK(bloomLevelCount(1, 1) == 3);
}

TEST_CASE("post noise uses explicit stepped time and a bounded triangular dither",
          "[render][post]") {
  CHECK(postFrameSeed(0) == 0);
  CHECK(postFrameSeed(.04) == 0);
  CHECK(postFrameSeed(1.0 / 24) == 1);
  CHECK(postFrameSeed(.75) == 18);
  CHECK(postFrameSeed(-1) == 0);
  CHECK(postFrameSeed(std::numeric_limits<double>::infinity()) == 0);
  double mean = 0;
  for (std::uint32_t y = 0; y < 128; ++y)
    for (std::uint32_t x = 0; x < 128; ++x) {
      const float n = postDither(x, y, 18);
      CHECK(std::abs(n) <= 1.0f / 255);
      CHECK(n == postDither(x, y, 18));
      mean += static_cast<double>(n);
    }
  CHECK(std::abs(mean / (128 * 128)) < .0001);
  CHECK(postDither(10, 20, 18) != postDither(10, 20, 19));
}
#endif
