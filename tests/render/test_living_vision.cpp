// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include "render/board_renderer.hpp"
#include "render/lab_scene.hpp"
#include "render/ui.hpp"
#include "view/living_vision.hpp"

using namespace cb;
using namespace cb::render;
namespace {
struct LivingFixture {
  SDL_Window* window{};
  Result<VulkanContext> ctx;
  Result<std::unique_ptr<Ui>> ui;
  Result<BoardRenderer> renderer;
  Result<OffscreenTarget> target;
  LivingFixture():ctx(makeContext()),ui(Ui::create(*ctx,window,view::implantTheme(view::Theme::manifold()))),
      renderer(BoardRenderer::create(*ctx)),target(OffscreenTarget::create(*ctx,640,360)) {
    REQUIRE(ui); REQUIRE(renderer); REQUIRE(target);
    renderer->setTheme(view::Theme::manifold());
  }
  Result<VulkanContext> makeContext() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"); REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    window=SDL_CreateWindow("living",640,360,SDL_WINDOW_HIDDEN); REQUIRE(window);
    auto c=VulkanContext::create(); REQUIRE(c); return c;
  }
  ~LivingFixture() {
    ui->reset(); // destroy ImGui before SDL, and before the context
    SDL_DestroyWindow(window); SDL_Quit();
  }
  std::vector<std::uint8_t> frame(ImplantParams p, bool button=false) {
    (*ui)->newFrame();
    auto* dl=ImGui::GetForegroundDrawList();
    if(button) dl->AddRectFilled({540,64},{548,80},IM_COL32_WHITE);
    else for(int row=0;row<5;++row) {
      const float y=100+float(row)*30;
      // Real separated glyph strokes, allowing both spatial gradient and band tests.
      for(int glyph=0;glyph<12;++glyph) dl->AddText({310+float(glyph)*12,y},IM_COL32_WHITE,"E");
    }
    (*ui)->endFrame();
    renderer->setImplant(p);
    REQUIRE(renderer->render(*target,{},view::OrbitCamera{},[&](VkCommandBuffer cmd){(*ui)->record(cmd);}).has_value());
    auto pixels=target->readPixels(); REQUIRE(pixels); return *pixels;
  }
};
int diff(const auto& a,const auto& b,int x,int y) {
  int d=0; const auto i=std::size_t((y*640+x)*4);
  for(std::size_t c=0;c<3;++c) d=std::max(d,std::abs(int(a[i+c])-int(b[i+c])));
  return d;
}
}
TEST_CASE("living warp matches CPU input mapping and leaves tools exact", "[render][living][gpu]") {
  LivingFixture f; ImplantParams p;
  p.living=1; p.warpK=view::kLensWarpK; p.pixelScale=1.f/3;
  p.gaze[0]=544; p.gaze[1]=72; p.veil=0;
  auto pixels=f.frame(p,true);
  double sx=0,sy=0,weight=0;
  for(int y=55;y<90;++y) for(int x=525;x<555;++x) {
    const double w=std::max(0,int(pixels[std::size_t((y*640+x)*4)])-80);
    sx+=(double(x)+.5)*w; sy+=(double(y)+.5)*w; weight+=w;
  }
  REQUIRE(weight>0);
  const auto src=view::lensWarpSource({float(sx/weight),float(sy/weight)},{640,360},p.warpK);
  CHECK(std::abs(src[0]-544)<1); CHECK(std::abs(src[1]-72)<1);
  p.toolCount=1; p.toolRects[0]={0,0,640,360};
  const auto tool=f.frame(p,true); p.living=0; p.warpK=0;
  CHECK(tool==f.frame(p,true));
  const auto messages=f.ctx->takeValidationMessages(); CAPTURE(messages); CHECK(messages.empty());
}
TEST_CASE("BCI lock-in and let-go carry content without sparks or escaped records", "[render][living][gpu]") {
  LivingFixture f; ImplantParams p;
  p.pixelScale=1.f/3; p.panelCount=1;
  p.panels[0]={{295,90,470,245},1,0,0,17};
  p.panels[0].origin[0]=320; p.panels[0].origin[1]=110;
  const auto settled=f.frame(p);
  auto energy=[](const auto& pixels,int x0,int y0,int x1,int y1) {
    double e=0;
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
      const auto i=std::size_t((y*640+x)*4);
      e+=std::pow(double(pixels[i])-pixels[i+4],2)+std::pow(double(pixels[i])-pixels[i+640*4],2);
    }
    return e;
  };
  for(int n=0;n<=9;++n) {
    p.panels[0].age=float(n)*.05f; p.panels[0].form=std::min(1.f,p.panels[0].age/.45f);
    const auto forming=f.frame(p);
    int outside=0,excess=0;
    for(int y=0;y<360;++y) for(int x=0;x<640;++x) {
      if(x<287 || x>=478 || y<82 || y>=253) outside=std::max(outside,diff(settled,forming,x,y));
      else {
        int maximum=0;
        for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx)
          maximum=std::max(maximum,int(settled[std::size_t(((y+dy)*640+x+dx)*4)]));
        excess=std::max(excess,int(forming[std::size_t((y*640+x)*4)])-maximum);
      }
    }
    CHECK(outside<=2); CHECK(excess<=64);
    if(n==2) {
      // Five text rows remain distinguishable before their fine strokes resolve.
      int peaks=0;
      for(int y=95;y<239;++y) {
        auto band=[&](int yy) {int total=0;for(int x=310;x<455;++x) total+=forming[std::size_t((yy*640+x)*4)];return total;};
        if(band(y)>band(y-1) && band(y)>=band(y+1)) ++peaks;
      }
      CHECK(peaks>=5);
    }
    if(n==4) CHECK(energy(forming,310,98,355,117)>energy(forming,410,218,455,237));
  }
  p.panels[0].form=1; p.panels[0].age=.1f; p.panels[0].dissolve=.1f/.22f;
  const auto leaving=f.frame(p);
  int outside=0;
  for(int y=0;y<360;++y) for(int x=0;x<640;++x)
    if(x<287 || x>=478 || y<82 || y>=253) outside=std::max(outside,diff(settled,leaving,x,y));
  CHECK(outside<=2);
  p.panels[0].age=.15f; p.panels[0].dissolve=.15f/.22f;
  const auto melting=f.frame(p);
  CHECK(energy(melting,310,98,355,117)>energy(melting,410,218,455,237));
  p.panels[0].settled=1; p.panels[0].dissolve=0;
  p.living=1; p.uiFocus=1; p.veil=.45f;
  const auto focused=f.frame(p);
  p.uiFocus=0;
  const auto away=f.frame(p);
  CHECK(focused!=away);
  p.uiFocus=1; p.tremor[0]=.25f; p.heartbeat=1.06f;
  const auto breathing=f.frame(p);
  int coreDiff=0; std::size_t glowDiff=0;
  for(int y=95;y<240;++y) for(int x=300;x<465;++x) {
    const auto i=std::size_t((y*640+x)*4);
    if(settled[i]>245) coreDiff=std::max(coreDiff,diff(focused,breathing,x,y));
    else glowDiff+=diff(focused,breathing,x,y)>0;
  }
  CHECK(coreDiff==0); CHECK(glowDiff>0);
  const auto messages=f.ctx->takeValidationMessages(); CAPTURE(messages); CHECK(messages.empty());
}
TEST_CASE("living veil dims the room softly around a settled panel", "[render][living][gpu]") {
  LivingFixture f; ImplantParams p;
  p.pixelScale=1.f/3; p.panelCount=1; p.living=1; p.veil=0;
  p.panels[0]={{295,90,470,245},1,0,0,17}; p.panels[0].settled=1;
  const auto bare=f.frame(p);
  p.veil=.45f;
  const auto veiled=f.frame(p);
  // Just inside the rect, below the text rows: darker (never painted brighter).
  const auto at=[](const auto& px,int x,int y,int c) {return int(px[std::size_t((y*640+x)*4+c)]);};
  int dimmer=0;
  for(int c=0;c<3;++c) dimmer+=at(bare,380,242,c)-at(veiled,380,242,c);
  CHECK(dimmer>0);
  for(int c=0;c<3;++c) CHECK(at(veiled,380,242,c)<=at(bare,380,242,c));
  // Far outside the 90 px (30 px here) falloff: untouched.
  int far=0;
  for(int c=0;c<3;++c) far=std::max(far,std::abs(at(veiled,380,320,c)-at(bare,380,320,c)));
  CHECK(far<=1);
  // Smooth: no step bigger than a few levels between neighbours along the falloff.
  int step=0;
  for(int y=246;y<300;++y) step=std::max(step,std::abs(at(veiled,380,y,1)-at(veiled,380,y+1,1)));
  CHECK(step<=3);
  const auto messages=f.ctx->takeValidationMessages(); CAPTURE(messages); CHECK(messages.empty());
}
TEST_CASE("living depth of field blurs the room by depth and is exact at zero", "[render][living][gpu]") {
  LivingFixture f;
  if(!f.ctx->depthSampleable()) { WARN("depth is not sampleable on this device; DoF takes the uniform fallback"); return; }
  auto doc=io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR)/"assets/lab/lab.toml"); REQUIRE(doc);
  auto lab=LabScene::create(*doc); REQUIRE(lab);
  InstanceSet instances;
  f.renderer->setTheme(view::Theme::manifold());
  lab->setFileRanges(f.renderer->addLabMeshes(lab->fileMeshes()));
  lab->appendTo(instances,view::Theme::manifold());
  const auto camera=*lab->station("menu");
  const auto render=[&](std::optional<BoardRenderer::DepthOfField> dof) {
    f.renderer->setDepthOfField(dof);
    REQUIRE(f.renderer->render(*f.target,instances,camera,{}).has_value());
    auto pixels=f.target->readPixels(); REQUIRE(pixels); return *pixels;
  };
  auto energy=[](const auto& pixels) {
    double e=0;
    for(int y=0;y<359;++y) for(int x=0;x<639;++x) {
      const auto i=std::size_t((y*640+x)*4);
      e+=std::pow(double(pixels[i+1])-pixels[i+5],2)+std::pow(double(pixels[i+1])-pixels[i+640*4+1],2);
    }
    return e;
  };
  const auto off=render(std::nullopt);
  const float cpm=kCellsPerMetre;
  // Reading the menu: focus at 0.45 m puts the whole room out of focus.
  const auto reading=render(BoardRenderer::DepthOfField{.45f*cpm,view::cocScaleFor(cpm),2.4f/3,0});
  CHECK(energy(reading)<energy(off));
  // A zero circle of confusion is the identity: the pass leaves the frame untouched.
  const auto zero=render(BoardRenderer::DepthOfField{.45f*cpm,0,2.4f/3,0});
  CHECK(zero==off);
  const auto messages=f.ctx->takeValidationMessages(); CAPTURE(messages); CHECK(messages.empty());
}
#endif
