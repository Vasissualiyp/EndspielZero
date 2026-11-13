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
  in.active=false; s.eyeShift={.01f,.01f};
  for(int i=0;i<120;++i) s=stepGaze(s,in);
  CHECK(s.uiFocus>.99f); CHECK(std::hypot(s.eyeShift[0],s.eyeShift[1])<.0001f);
  CHECK(s.pointPx==Vec2f{960,540}); CHECK(s.idle==0); CHECK(s.saccadeT==1);
}
TEST_CASE("living wander is deterministic weighted visible and bounded", "[unit][view][living]") {
  auto in=input();
  const GazeTarget targets[]={{"menu",{},.25f},{"lamp",{400,100,30},.2f},
    {"hologram",{600,600,20},.4f},{"rack",{800,700,50},.15f},
    {"behind",{100,100,-1},100},{"outside",{4000,100,10},100}};
  in.targets=targets;
  GazeState a,b; std::set<int> visited;
  int last=-1; float fixationStart=0;
  for(int i=0;i<3600;++i) {
    in.time=static_cast<double>(i)/60;
    const auto count=a.fixationCount;
    a=stepGaze(a,in); b=stepGaze(b,in);
    REQUIRE(a.pointPx==b.pointPx); REQUIRE(a.eyeShift==b.eyeShift);
    REQUIRE(a.focusDistance==b.focusDistance);
    REQUIRE(std::hypot(a.eyeShift[0],a.eyeShift[1])<=kEyeShiftMaxRadians+kDriftRadians+1e-5f);
    if(a.fixationCount!=count) {
      CHECK(a.lastTarget!=last); CHECK(a.lastTarget<4);
      if(last>=0) {CHECK(float(in.time)-fixationStart>=kFixationMin-1.f/60); CHECK(float(in.time)-fixationStart<=kFixationMax+2.f/60);}
      fixationStart=float(in.time); last=a.lastTarget; visited.insert(last);
    }
    if(i==151) CHECK(a.fixationCount>0);
  }
  CHECK(visited.size()>=3);
  for(const auto intensity : {0.f,1.f}) {
    in.wander=false; in.intensity=intensity; a={};
    for(int i=0;i<600;++i) a=stepGaze(a,in);
    CHECK(a.fixationCount==0); CHECK(a.eyeShift==Vec2f{}); CHECK(a.saccadeT==1);
  }
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
TEST_CASE("living eye shift looks towards a pixel and is capped", "[unit][view][living]") {
  const Vec2f size{1920,1080};
  CHECK(eyeShiftToward({960,540},size,.9f)==Vec2f{0,0});
  const auto right=eyeShiftToward({1900,540},size,.9f);
  CHECK(right[0]<0); CHECK(right[1]==0);  // yaw towards +x pans the view right
  const auto down=eyeShiftToward({960,1080},size,.9f);
  CHECK(down[1]>0);
  CHECK(std::hypot(eyeShiftToward({1e6f,1e6f},size,.9f)[0],eyeShiftToward({1e6f,1e6f},size,.9f)[1])<=kEyeShiftMaxRadians+1e-6f);
}
