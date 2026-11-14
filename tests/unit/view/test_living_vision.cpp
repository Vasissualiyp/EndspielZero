// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <set>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "view/living_vision.hpp"
using namespace cb::view;
using Catch::Approx;

TEST_CASE("living lens maps input to the shader's source", "[unit][view][living]") {
  const Vec2f size{1920, 1080};
  CHECK(lensWarpSource({960, 540}, size, kLensWarpK) == Vec2f{960, 540});
  for (int i = 0; i <= 100; ++i) {
    const float x = static_cast<float>(i) / 100;
    Vec2f p{960 + 960*x, 540 + 540*x};
    CHECK(lensWarpSource(p, size, 0) == p);
    const auto q = lensWarpSource(p, size, kLensWarpK);
    CHECK(q[0] >= p[0]);
    CHECK(q[1] >= p[1]);
    const auto mirrored = lensWarpSource({1920-p[0], 1080-p[1]}, size, kLensWarpK);
    CHECK(q[0] + mirrored[0] == Approx(1920));
    CHECK(q[1] + mirrored[1] == Approx(1080));
  }
  CHECK(lensWarpSource({1920,1080}, size, kLensWarpK)[0] == Approx(960+960*1.03f));
}
TEST_CASE("living tremor is bounded continuous explicit time and calm safe", "[unit][view][living]") {
  CHECK(tremorOffset(.1, 0, 42) == Vec2f{});
  CHECK(tremorOffset(0, 1, 42) != tremorOffset(.5, 1, 42));
  for (int i=0;i<=60000;++i) {
    const auto p = tremorOffset(static_cast<double>(i)/1000, 1, 42);
    REQUIRE(std::hypot(p[0],p[1]) <= kTremorPixels + 1e-6f);
  }
}
TEST_CASE("living periphery preserves the gaze and viewport centre", "[unit][view][living]") {
  CHECK(eccentricity({100,100},{100,100},{1920,1080})==0);
  CHECK(eccentricity({960,540},{100,100},{1920,1080})==0);
  CHECK(peripheryAmount(eccentricity({1920,1080},{0,0},{1920,1080}))==1);
  float previous=0;
  for(int i=0;i<=100;++i) {
    const float x=static_cast<float>(i)/100;
    const float p=peripheryAmount(eccentricity({960+960*x,540+540*x},{960,540},{1920,1080}));
    CHECK(p>=previous); previous=p;
  }
}
namespace {
GazeInput input() {
  GazeInput in;
  in.active=true; in.dt=1.f/60; in.viewport={1920,1080};
  in.panelCentrePx={1450,540}; in.mousePx={1400,500}; in.mouseOverPanel=true;
  in.defaultDistance=20; in.seed=42;
  in.project=[](Vec3 p)->std::optional<std::pair<Vec2f,float>> {
    if(p.z<0) return {};
    return std::pair{Vec2f{p.x,p.y},p.z};
  };
  return in;
}
}
TEST_CASE("living gaze accommodates with dwell and returns to reading quickly", "[unit][view][living]") {
  auto in=input(); GazeState s;
  s.uiFocus=0;
  for(int i=0;i<60;++i) s=stepGaze(s,in);
  CHECK(s.uiFocus>.98f);
  const GazeTarget targets[]={{"lamp",{400,500,40},1}};
  in.targets=targets; in.mousePx={400,500}; in.mouseOverPanel=false; in.mouseMoved=true;
  for(int i=0;i<30;++i) s=stepGaze(s,in);
  CHECK(s.uiFocus>.98f); // brief: dwell outside for 0.6 s before losing focus
  for(int i=0;i<90;++i) s=stepGaze(s,in);
  CHECK(s.uiFocus<.02f); CHECK(s.focusDistance==Approx(40).epsilon(.02));
  in.active=false;
  for(int i=0;i<120;++i) s=stepGaze(s,in);
  CHECK(s.uiFocus>.99f);
  CHECK(s.pointPx==Vec2f{960,540}); CHECK(s.idle==0); CHECK(s.saccadeT==1);
}
namespace {
const GazeTarget kTargets[]={{"menu",{},.25f},{"lamp",{400,100,30},.2f},
  {"hologram",{600,600,20},.4f},{"rack",{800,700,50},.15f},
  {"behind",{100,100,-1},100},{"outside",{4000,100,10},100}};
}
TEST_CASE("daydream stays off while the player keeps acting", "[unit][view][living]") {
  auto in=input(); in.targets=kTargets;
  GazeState s;
  for(int i=0;i<3600;++i) {
    in.time=static_cast<double>(i)/60;
    in.mouseMoved=i%300==0;  // input every 5 s
    const auto count=s.fixationCount;
    s=stepGaze(s,in);
    REQUIRE(s.fixationCount==count); REQUIRE(s.daydream==0);
  }
  CHECK(kIdleBeforeWander==10.f);
}
TEST_CASE("daydream defocuses within 1.6 s of ten seconds idle and input clears it", "[unit][view][living]") {
  auto in=input(); in.targets=kTargets; in.mouseOverPanel=false;
  GazeState s;
  int frames=0;
  for(;frames<60*30 && s.daydream<1;++frames) { in.time=frames/60.0; s=stepGaze(s,in); }
  CHECK(s.daydream==1);
  CHECK(static_cast<float>(frames)/60>=kIdleBeforeWander);
  CHECK(static_cast<float>(frames)/60<=kIdleBeforeWander+1.6f);
  for(int i=0;i<60;++i) s=stepGaze(s,in);
  CHECK(s.daydream==1);
  in.mouseMoved=true; in.mousePx={300,300};
  for(int i=0;i<18;++i) s=stepGaze(s,in);  // 0.3 s
  CHECK(s.daydream<.05f);
  CHECK(s.pointPx==Vec2f{300,300});  // the spot jumps to the mouse
  CHECK(s.idle==0);
  in.mouseMoved=false;  // a key or click clears it too
  GazeState t;
  for(int i=0;i<60*12;++i) t=stepGaze(t,in);
  REQUIRE(t.daydream>0);
  for(int i=0;i<18;++i) { auto k=in; k.input=true; t=stepGaze(t,k); }
  CHECK(t.daydream<.05f);
}
TEST_CASE("daydream fixations last two to five seconds on several places and are deterministic", "[unit][view][living]") {
  auto in=input(); in.targets=kTargets; in.mouseOverPanel=false;
  GazeState a,b; std::set<std::pair<int,int>> places;
  int lastTarget=-1; float fixationStart=-1; int fixations=0;
  for(int i=0;i<60*70;++i) {
    in.time=static_cast<double>(i)/60;
    const auto count=a.fixationCount;
    a=stepGaze(a,in); b=stepGaze(b,in);
    REQUIRE(a.pointPx==b.pointPx); REQUIRE(a.daydream==b.daydream);
    REQUIRE(a.focusDistance==b.focusDistance); REQUIRE(a.toPx==b.toPx);
    if(a.fixationCount!=count) {
      const float now=static_cast<float>(in.time);
      if(fixationStart>=0) { CHECK(now-fixationStart>=kFixationMin-1.f/60); CHECK(now-fixationStart<=kFixationMax+2.f/60); }
      fixationStart=now; ++fixations;
      if(a.lastTarget>=0) { CHECK(a.lastTarget!=lastTarget); CHECK(a.lastTarget<4); lastTarget=a.lastTarget; }
      else {
        CHECK(a.toPx[0]>=.1f*1920); CHECK(a.toPx[0]<=.9f*1920);
        CHECK(a.toPx[1]>=.1f*1080); CHECK(a.toPx[1]<=.9f*1080);
      }
      places.insert({int(a.toPx[0]),int(a.toPx[1])});
    }
  }
  CHECK(kFixationMin==2.f); CHECK(kFixationMax==5.f);
  CHECK(fixations>=8);
  CHECK(places.size()>=3);
  for(const auto intensity : {0.f,1.f}) {
    in.wander=intensity==1.f ? false : true; in.intensity=intensity; a={};
    for(int i=0;i<60*30;++i) a=stepGaze(a,in);
    CHECK(a.fixationCount==0); CHECK(a.daydream==0); CHECK(a.saccadeT==1);
  }
}
TEST_CASE("daydream blur is uniform far from the spot and softer inside it", "[unit][view][living]") {
  const Vec2f spot{960,540};
  CHECK(daydreamBlurPixels(0,{100,100},spot,1080,1)==0);
  const float far=daydreamBlurPixels(1,{100,100},spot,1080,1);
  CHECK(far>=6.f);
  CHECK(daydreamBlurPixels(1,spot,spot,1080,1)==Approx(.25f*far));
  CHECK(daydreamBlurPixels(1,{960+.12f*1080,540},spot,1080,1)==Approx(.25f*far));
  CHECK(daydreamBlurPixels(1,{960+.18f*1080,540},spot,1080,1)==Approx(far));
  const float mid=daydreamBlurPixels(1,{960+.15f*1080,540},spot,1080,1);
  CHECK(mid>.25f*far); CHECK(mid<far);
  CHECK(daydreamBlurPixels(1,{100,100},spot,2160,2)==Approx(2*far));
}
TEST_CASE("the gaze never shifts the camera", "[unit][view][living]") {
  auto in=input(); in.targets=kTargets; in.mouseOverPanel=false;
  OrbitCamera cam; const auto reference=cam.viewProj(16.f/9);
  GazeState s;
  for(int i=0;i<60*30;++i) { in.time=i/60.0; s=stepGaze(s,in); }
  CHECK(s.fixationCount>=3);  // it did wander through several saccades
  CHECK(cam.viewProj(16.f/9)==reference);
}
TEST_CASE("living depth uses the camera's finite Vulkan projection", "[unit][view][living]") {
  OrbitCamera c;
  c.nearZ=.1f; c.farZ=1000;
  const auto projection=perspective(c.fovY,16.f/9,c.nearZ,c.farZ);
  for(float z : {.1f,1.f,10.f,100.f,900.f}) {
    const auto ndc=transformPoint(projection,{0,0,-z});
    CHECK(linearDepth(ndc.z,c.nearZ,c.farZ)==Approx(z).epsilon(.001));
  }
  CHECK(circleOfConfusion(20,20,cocScaleFor(20))==0);
  CHECK(circleOfConfusion(20,9,cocScaleFor(20))==Approx(.7f*kWorldDefocusPixels));
  CHECK(circleOfConfusion(1,1000,cocScaleFor(20))==kWorldDefocusPixels);
  CHECK(circleOfConfusion(10,20,1)==circleOfConfusion(20,10,1));
}
