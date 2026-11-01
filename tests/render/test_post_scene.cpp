// SPDX-License-Identifier: GPL-3.0-or-later
// The renderer's post-processing path (ADR-0025, M23.10 section 2), end to end: real
// frames through `BoardRenderer::render` / `record` with post on and off, on a real device.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

#include "app/session.hpp"
#include "app/shell.hpp"
#include "render/board_renderer.hpp"
#include "render/offscreen_target.hpp"
#include "render/post_params.hpp"
#include "render/vulkan_context.hpp"
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>

#include "render/ui.hpp"
#endif
#include "support/variants.hpp"
#include "view/theme.hpp"

using namespace cb;
using namespace cb::render;

namespace {
struct Gpu {
  VulkanContext ctx;
  bool available{false};
  Gpu() {
    auto c = VulkanContext::create();
    if (c.has_value()) {
      ctx = std::move(*c);
      available = true;
    }
  }
};
Gpu& gpu() {
  static Gpu g;
  return g;
}

struct Scene {
  VariantSpec variant{test::loadVariant("standard")};
  view::ViewConfig cfg{view::ViewConfig::forBoard(variant.dims)};
  view::OrbitCamera cam{
      view::OrbitCamera::frame(view::boundsOf(view::layout(variant.dims, cfg)))};
  InstanceSet set{BoardRenderer{}.buildInstances(
      view::PositionView::capture(Position::startPosition(variant)), cfg)};
};

/// A solid rectangle in the overlay pass: opaque UI pixels with a known colour.
void overlayRect(VkCommandBuffer cmd, std::int32_t x, std::int32_t y, std::uint32_t w,
                 std::uint32_t h) {
  VkClearAttachment clear{};
  clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  clear.clearValue.color = {{.9f, .1f, .5f, 1}};
  VkClearRect rect{{{x, y}, {w, h}}, 0, 1};
  vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
}

std::vector<std::uint8_t> frame(BoardRenderer& r, const OffscreenTarget& t, const Scene& s,
                                const std::function<void(VkCommandBuffer)>& overlay = {},
                                const std::function<void(VkCommandBuffer)>& background = {}) {
  REQUIRE(r.render(t, s.set, s.cam, overlay, background).has_value());
  auto px = t.readPixels();
  REQUIRE(px.has_value());
  return std::move(*px);
}

float lum(const std::vector<std::uint8_t>& px, std::size_t i) {
  return .2126f * px[i] + .7152f * px[i + 1] + .0722f * px[i + 2];
}
}  // namespace

TEST_CASE("post off is the legacy reference and post on matches a CPU composite",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto target = OffscreenTarget::create(gpu().ctx, 160, 120);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  Scene scene;
  Scene empty;
  empty.set = InstanceSet{};
  auto theme = view::Theme::console();  // plain clear colour, below the shoulder
  theme.grainAmount = 0;
  renderer->setTheme(theme);

  CHECK_FALSE(renderer->hdrScene());  // off by default at the renderer level
  const auto legacy = frame(*renderer, *target, empty);
  renderer->setPost(true, 1);
  REQUIRE(renderer->hdrScene());
  const auto post = frame(*renderer, *target, empty);
  renderer->setPost(false, 1);
  CHECK(frame(*renderer, *target, empty) == legacy);

  const auto ink = theme.ink;
  for (std::uint32_t y = 0; y < 120; ++y)
    for (std::uint32_t x = 0; x < 160; ++x) {
      const float vig = postVignette((static_cast<float>(x) + .5f) / 160,
                                     (static_cast<float>(y) + .5f) / 120);
      const auto i = (y * 160 + x) * 4;
      const float want[3]{ink.r, ink.g, ink.b};
      for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(post[i + static_cast<unsigned>(c)] - want[c] * vig * 255) <= 2);
        if (x == 80 && y == 60) CHECK(std::abs(legacy[i + static_cast<unsigned>(c)] - want[c] * 255) <= 1);
      }
    }
  CHECK(gpu().ctx.takeValidationMessages().empty());
  (void)scene;
}

TEST_CASE("post-processed frames leave opaque overlay pixels and a sharp UI untouched",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto target = OffscreenTarget::create(gpu().ctx, 192, 144);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  Scene scene;
  renderer->setTheme(view::Theme::manifold());
  const auto rect = [](VkCommandBuffer c) { overlayRect(c, 10, 10, 24, 16); };
  const auto at = [](const std::vector<std::uint8_t>& px, int x, int y) {
    const auto i = static_cast<std::size_t>(y * 192 + x) * 4;
    return std::array<std::uint8_t, 4>{px[i], px[i + 1], px[i + 2], px[i + 3]};
  };
  for (const float blur : {0.0f, 1.0f}) {
    renderer->setBlur(blur);
    renderer->setPost(false, 1);
    const auto off = frame(*renderer, *target, scene, rect);
    renderer->setPost(true, 1);
    const auto on = frame(*renderer, *target, scene, rect);
    for (int y = 12; y < 24; ++y)
      for (int x = 12; x < 30; ++x) CHECK(at(on, x, y) == at(off, x, y));
    CHECK(on != off);
  }
  CHECK(gpu().ctx.takeValidationMessages().empty());
}

TEST_CASE("bloom scale controls bloom passes and brightens around emitters",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto target = OffscreenTarget::create(gpu().ctx, 256, 192);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  Scene scene;
  auto theme = view::Theme::manifold();
  theme.grainAmount = 0;
  renderer->setTheme(theme);
  renderer->setPost(true, 0);
  const auto flat = frame(*renderer, *target, scene);
  CHECK(renderer->postStats(0).bloomPasses == 0);
  renderer->setPost(true, 1);
  const auto bloomed = frame(*renderer, *target, scene);
  CHECK(renderer->postStats(0).bloomPasses == 2 * bloomLevelCount(256, 192) - 1);
  double a = 0, b = 0;
  for (std::size_t i = 0; i < flat.size(); i += 4) {
    a += static_cast<double>(lum(flat, i));
    b += static_cast<double>(lum(bloomed, i));
  }
  CHECK(b > a);
  renderer->setPost(true, 0);
  CHECK(frame(*renderer, *target, scene) == flat);
  CHECK(renderer->postStats(0).bloomPasses == 0);
  // A console-theme board has no bloom at all, whatever the scale.
  renderer->setTheme(view::Theme::console());
  renderer->setPost(true, 4);
  (void)frame(*renderer, *target, scene);
  CHECK(renderer->postStats(0).bloomPasses == 0);
  CHECK(gpu().ctx.takeValidationMessages().empty());
}

TEST_CASE("post frames are pure in visual time and grain moves with it",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto target = OffscreenTarget::create(gpu().ctx, 128, 96);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  Scene empty;
  empty.set = InstanceSet{};
  auto theme = view::Theme::console();
  theme.ink = view::Rgba{.4f, .4f, .4f, 1};  // midtone: grain is strongest there
  renderer->setTheme(theme);
  renderer->setPost(true, 0);
  renderer->setVisualTime(1.0);
  const auto a = frame(*renderer, *target, empty);
  CHECK(frame(*renderer, *target, empty) == a);
  renderer->setVisualTime(1.02);  // same 24 Hz step
  CHECK(frame(*renderer, *target, empty) == a);
  renderer->setVisualTime(1.5);
  CHECK(frame(*renderer, *target, empty) != a);
  // Intensity zero removes grain; dither stays, so frames of different steps are equal
  // only where dither agrees - they still differ slightly but by at most the dither.
  BoardOptions calm;
  calm.presentationIntensity = 0;
  renderer->setOptions(calm);
  renderer->setVisualTime(1.0);
  const auto c0 = frame(*renderer, *target, empty);
  renderer->setVisualTime(1.5);
  const auto c1 = frame(*renderer, *target, empty);
  int maxDiff = 0;
  for (std::size_t i = 0; i < c0.size(); ++i)
    maxDiff = std::max(maxDiff, std::abs(static_cast<int>(c0[i]) - static_cast<int>(c1[i])));
  CHECK(maxDiff <= 2);
  CHECK(gpu().ctx.takeValidationMessages().empty());
}

TEST_CASE("post handles resize, odd and tiny targets and BGRA output", "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto rgba = BoardRenderer::create(gpu().ctx);
  auto bgra = BoardRenderer::create(gpu().ctx, VK_FORMAT_B8G8R8A8_UNORM);
  REQUIRE(rgba.has_value());
  REQUIRE(bgra.has_value());
  if (!rgba->postAvailable()) SKIP("device cannot run post");
  Scene scene;
  rgba->setTheme(view::Theme::manifold());
  bgra->setTheme(view::Theme::manifold());
  rgba->setPost(true, 1);
  bgra->setPost(true, 1);
  for (const auto& [w, h] : {std::pair{1u, 1u}, {3u, 2u}, {37u, 29u}, {128u, 96u}, {63u, 200u}, {128u, 96u}}) {
    auto t = OffscreenTarget::create(gpu().ctx, w, h);
    auto tb = OffscreenTarget::create(gpu().ctx, w, h, VK_FORMAT_B8G8R8A8_UNORM);
    REQUIRE(t.has_value());
    REQUIRE(tb.has_value());
    const auto a = frame(*rgba, *t, scene);
    const auto b = frame(*bgra, *tb, scene);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); i += 4) {
      CHECK(a[i] == b[i + 2]);
      CHECK(a[i + 1] == b[i + 1]);
      CHECK(a[i + 2] == b[i]);
    }
  }
  CHECK(gpu().ctx.takeValidationMessages().empty());
}

TEST_CASE("two frame slots can be recorded outstanding and alternate post on and off",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto t0 = OffscreenTarget::create(gpu().ctx, 160, 120);
  auto t1 = OffscreenTarget::create(gpu().ctx, 160, 120);
  REQUIRE(renderer.has_value());
  REQUIRE(t0.has_value());
  REQUIRE(t1.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  Scene scene;
  renderer->setTheme(view::Theme::manifold());
  renderer->setVisualTime(.5);
  renderer->setPost(true, 1);
  const auto ref = frame(*renderer, *t0, scene);
  renderer->setPost(false, 1);
  const auto legacy = frame(*renderer, *t0, scene);
  CHECK(ref != legacy);

  // Both slots recorded before either is submitted, then alternating modes across slots.
  for (const bool firstOn : {true, false}) {
    Result<void> a{}, b{};
    REQUIRE(gpu().ctx.submitAndWait([&](VkCommandBuffer cmd) {
      renderer->setPost(firstOn, 1);
      a = renderer->record(cmd, *t0, scene.set, scene.cam, {}, {}, {}, 0);
      renderer->setPost(!firstOn, 1);
      b = renderer->record(cmd, *t1, scene.set, scene.cam, {}, {}, {}, 1);
      for (const OffscreenTarget* t : {&*t0, &*t1}) {
        VkImageMemoryBarrier2 m{};
        m.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        m.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        m.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        m.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        m.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        m.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        m.image = t->colorImage();
        m.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &m;
        vkCmdPipelineBarrier2(cmd, &dep);
      }
    }).has_value());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const auto p0 = t0->readPixels();
    const auto p1 = t1->readPixels();
    REQUIRE(p0.has_value());
    REQUIRE(p1.has_value());
    CHECK(*p0 == (firstOn ? ref : legacy));
    CHECK(*p1 == (firstOn ? legacy : ref));
  }
  CHECK(gpu().ctx.takeValidationMessages().empty());
}

#ifdef CB_HAVE_IMGUI
TEST_CASE("the real interface field draws behind an HDR scene with clean validation",
          "[render][gpu][post]") {
  if (!gpu().available) SKIP("no Vulkan device");
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdlGuard;
  SDL_Window* window = SDL_CreateWindow("post-field", 256, 192, SDL_WINDOW_HIDDEN);
  REQUIRE(window != nullptr);
  struct WinGuard {
    SDL_Window* window;
    ~WinGuard() { SDL_DestroyWindow(window); }
  } winGuard{window};
  auto shell = app::Shell::create({"standard"},
                                  std::filesystem::path(CB_BINARY_DIR) / "post-field.conf");
  REQUIRE(shell != nullptr);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    try {
      return test::loadVariant(name);
    } catch (const std::exception& e) {
      return fail(ErrorCode::ParseError, e.what());
    }
  });
  REQUIRE(shell->startGame("standard").has_value());
  auto ui = Ui::create(gpu().ctx, window, view::Theme::manifold());
  REQUIRE(ui.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  auto target = OffscreenTarget::create(gpu().ctx, 256, 192);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  if (!renderer->postAvailable()) SKIP("device cannot run post");
  const VariantSpec& v = shell->session()->variant();
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  const InstanceSet set = BoardRenderer{}.buildInstances(shell->session()->snapshot(), cfg);
  (void)gpu().ctx.takeValidationMessages();
  std::size_t changed = 0;
  std::vector<std::uint8_t> plain;
  for (const bool post : {false, true}) {
    renderer->setPost(post, 1);
    for (int i = 0; i < 6; ++i) {
      (*ui)->tick(.15f);
      (*ui)->newFrame();
      (void)(*ui)->build(*shell, 60.0f);
      (*ui)->endFrame();
      REQUIRE(renderer
                  ->render(
                      *target, set, cam, [&](VkCommandBuffer c) { (*ui)->record(c); },
                      [&](VkCommandBuffer c) {
                        (*ui)->recordBackground(c, renderer->hdrScene());
                      })
                  .has_value());
    }
    auto px = target->readPixels();
    REQUIRE(px.has_value());
    if (!post) plain = *px;
    else
      for (std::size_t i = 0; i < px->size(); ++i) changed += (*px)[i] != plain[i];
  }
  CHECK(changed > 0);
  CAPTURE(gpu().ctx.takeValidationMessages());
  CHECK(gpu().ctx.validationErrorCount() == 0);
}
#endif
#endif
