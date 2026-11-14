// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/living_vision.hpp"

#include <numbers>

namespace cb::view {
namespace {
float smooth(float x) { x = std::clamp(x, 0.f, 1.f); return x*x*(3.f-2.f*x); }
float distance(Vec2f a, Vec2f b) { return std::hypot(a[0]-b[0], a[1]-b[1]); }
Vec2f mix(Vec2f a, Vec2f b, float t) {
  return {a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t};
}
// The integer avalanche used by board_boot.cpp and game_end_fx.cpp.
float unit(std::uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
  x *= 0x846ca68bu; x ^= x >> 16;
  return static_cast<float>(x >> 8)*(1.f/16777216.f);
}
}
Vec2f lensWarpSource(Vec2f px, Vec2f size, float k) noexcept {
  const float x = px[0]-.5f*size[0], y=px[1]-.5f*size[1];
  const float diagonal2=.25f*(size[0]*size[0]+size[1]*size[1]);
  if (diagonal2 <= 0 || k == 0) return px;
  const float factor=1+k*(x*x+y*y)/diagonal2;
  return {.5f*size[0]+x*factor,.5f*size[1]+y*factor};
}
Vec2f tremorOffset(double seconds, float intensity, std::uint32_t seed) noexcept {
  if (intensity < .5f) return {};
  constexpr double hz[2][3]={{.71,1.33,2.87},{.53,1.71,3.11}};
  constexpr double weights[]={.5,.3,.2};
  Vec2f result{};
  for (std::size_t axis=0;axis<2;++axis)
    for (std::size_t i=0;i<3;++i) {
      const double phase=static_cast<double>(unit(seed+static_cast<std::uint32_t>(3*axis+i)));
      result[axis]+=static_cast<float>(weights[i]*std::sin(2*std::numbers::pi*(hz[axis][i]*seconds+phase)))*kTremorPixels*.70710678f;
    }
  return result;
}
float eccentricity(Vec2f px, Vec2f gaze, Vec2f size) noexcept {
  if (size[1] <= 0) return 0;
  return std::min(distance(px,gaze),1.3f*distance(px,{size[0]*.5f,size[1]*.5f}))/size[1];
}
float peripheryAmount(float e) noexcept { return smooth((e-kPeripheryStart)/(kPeripheryFull-kPeripheryStart)); }
GazeState stepGaze(const GazeState& previous, const GazeInput& in) noexcept {
  auto s=previous;
  const float dt=std::clamp(in.dt,0.f,.1f);
  const float a=1-std::exp(-dt/kAccommodationSeconds);
  const auto easeFocus=[&] {
    const float inv=1/std::max(s.focusDistance,.001f);
    s.focusDistance=1/(inv+(1/std::max(s.focusDistanceTarget,.001f)-inv)*a);
  };
  if (!in.active) {
    s.uiFocus+=(1-s.uiFocus)*a;
    s.uiFocusTarget=1;
    s.daydreamT=s.daydream=0;
    s.pointPx={in.viewport[0]*.5f,in.viewport[1]*.5f};
    s.fromPx=s.toPx=s.pointPx;
    s.idle=s.fixationLeft=s.outsidePanel=0; s.saccadeT=1;
    s.focusDistanceTarget=in.defaultDistance; easeFocus(); return s;
  }
  const bool acted=in.mouseMoved || in.dragging || in.input;
  s.idle=acted ? 0 : s.idle+dt;
  const bool wandering=s.idle>=kIdleBeforeWander && in.wander && in.intensity>=.5f;
  const auto project=[&](const GazeTarget& target)->std::optional<std::pair<Vec2f,float>> {
    if (target.key=="menu") return std::pair{in.panelCentrePx,s.focusDistanceTarget};
    return in.project ? in.project(target.world) : std::nullopt;
  };
  const auto start=[&](Vec2f to) {
    s.fromPx=s.pointPx; s.toPx=to; s.saccadeT=0;
  };
  if (!wandering) {
    if (s.daydreamT>0) {  // waking: the spot jumps to the mouse
      s.pointPx=s.fromPx=s.toPx=in.mousePx; s.saccadeT=1;
    }
    if (distance(in.mousePx,s.toPx)>.04f*in.viewport[1]) start(in.mousePx);
    else s.toPx=in.mousePx;
    s.outsidePanel=in.mouseOverPanel ? 0 : s.outsidePanel+dt;
    s.uiFocusTarget=s.outsidePanel<.6f ? 1.f : 0.f;
    if (!in.mouseOverPanel) {
      s.focusDistanceTarget=in.defaultDistance;
      float closest=.08f*in.viewport[1];
      for (const auto& target:in.targets) {
        if (target.key=="menu") continue;
        const auto p=project(target);
        if (p && distance(p->first,in.mousePx)<closest) {
          closest=distance(p->first,in.mousePx); s.focusDistanceTarget=p->second;
        }
      }
    }
    s.fixationLeft=0;
  } else if (s.fixationLeft<=0) {
    const auto valid=[&](std::size_t i) {
      if (static_cast<int>(i)==s.lastTarget || in.targets[i].weight<=0) return false;
      const auto p=project(in.targets[i]);
      return p && p->first[0]>=0 && p->first[0]<in.viewport[0] && p->first[1]>=0 && p->first[1]<in.viewport[1];
    };
    float sum=0;
    for (std::size_t i=0;i<in.targets.size();++i) if(valid(i)) sum+=in.targets[i].weight;
    const std::uint32_t n=s.fixationCount;
    const auto fixationLength=[&] { return kFixationMin+(kFixationMax-kFixationMin)*unit(in.seed+n*0x846ca68bu+71); };
    bool chosen=false;
    if (sum>0 && unit(in.seed+n*0x85ebca6bu+13)>=kRandomFixationChance) {
      float choice=unit(in.seed ^ (n*0x9e3779b9u))*sum;
      for (std::size_t i=0;i<in.targets.size();++i) {
        if (!valid(i)) continue;
        choice-=in.targets[i].weight;
        if (choice>0) continue;
        const auto& target=in.targets[i]; const auto p=*project(target);
        start(p.first); s.lastTarget=static_cast<int>(i);
        s.uiFocusTarget=target.key=="menu" ? 1.f : 0.f;
        if (target.key!="menu") s.focusDistanceTarget=p.second;
        chosen=true; break;
      }
    }
    if (!chosen) {  // a made-up glance inside the middle 80 %; focus distance is kept
      start({in.viewport[0]*(.1f+.8f*unit(in.seed+n*0x27d4eb2fu+5)),
             in.viewport[1]*(.1f+.8f*unit(in.seed+n*0x165667b1u+9))});
      s.lastTarget=-1;
    }
    s.fixationLeft=fixationLength(); ++s.fixationCount;
  } else s.fixationLeft-=dt;
  s.saccadeT=std::min(1.f,s.saccadeT+dt/kSaccadeSeconds);
  s.pointPx=mix(s.fromPx,s.toPx,smooth(s.saccadeT));
  s.uiFocus+=(s.uiFocusTarget-s.uiFocus)*(1-std::exp(-dt/(s.uiFocusTarget==1 ? .15f : kAccommodationSeconds)));
  easeFocus();
  s.daydreamT=std::clamp(s.daydreamT+(wandering ? dt/kDaydreamRiseSeconds : -dt/kDaydreamWakeSeconds),0.f,1.f);
  s.daydream=smooth(s.daydreamT);
  return s;
}
float daydreamBlurPixels(float daydream,Vec2f px,Vec2f spot,float height,float pixelScale) noexcept {
  const float d=distance(px,spot)/std::max(height,1.f);
  const float inside=1-smooth((d-kSpotRadius)/kSpotFeather);
  return daydream*kDaydreamBlurPixels*pixelScale*(1-(1-kSpotBlurFraction)*inside);
}
float circleOfConfusion(float z,float focus,float scale) noexcept {
  return std::clamp(std::abs(1/std::max(z,.001f)-1/std::max(focus,.001f))*scale,0.f,1.f)*kWorldDefocusPixels;
}
float cocScaleFor(float cellsPerMetre) noexcept { return .7f*cellsPerMetre/(1/kMenuFocusDistanceMetres-1); }
float linearDepth(float depth,float nearZ,float farZ) noexcept { return nearZ*farZ/(farZ-depth*(farZ-nearZ)); }
}  // namespace cb::view
