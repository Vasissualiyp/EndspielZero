// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0, binding=0) uniform sampler2D ui;
struct Panel { vec4 rect; vec4 state; };
layout(std140, set=0, binding=1) uniform Params {
  Panel panels[16];
  vec4 tools[4];
  vec4 timing;  // time, intensity, globalSync, heartbeat
  vec4 screen;  // width, height, dip, pixelScale
  vec4 counts;  // panels, tools, afterimageMax, reserved
  vec4 warm;
  vec4 body;
} p;

float hash12(vec2 v) {
  vec3 q = fract(vec3(v.xyx) * .1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}
float noise(vec2 v) {
  vec2 i = floor(v), f = fract(v);
  f = f*f*(3.0-2.0*f);
  return mix(mix(hash12(i), hash12(i+vec2(1,0)), f.x),
             mix(hash12(i+vec2(0,1)), hash12(i+vec2(1,1)), f.x), f.y);
}
float fbm(vec2 v) {
  float n = 0.0, a = .5333333;
  for (int i=0; i<4; ++i) { n += a*noise(v); v = v*2.03 + vec2(13.1,7.7); a *= .5; }
  return n;
}
vec2 curl(vec2 v, float t) {
  const float e = .01;
  v += vec2(0,t);
  vec2 c = vec2(noise(v+vec2(0,e))-noise(v-vec2(0,e)),
                -noise(v+vec2(e,0))+noise(v-vec2(e,0))) / (2.0*e);
  return c/max(1.0,length(c));
}
bool inside(vec2 px, vec4 r) {
  return all(greaterThanEqual(px,r.xy)) && all(lessThan(px,r.zw));
}
vec4 blurSample(vec2 uv, float radiusPx) {
  if (radiusPx < .5) return texture(ui,uv);
  const vec2 disc[8] = vec2[8](vec2(-.326,-.406),vec2(-.840,-.074),
    vec2(-.696,.457),vec2(-.203,.621),vec2(.962,-.195),vec2(.473,-.480),
    vec2(.519,.767),vec2(.185,-.893));
  vec4 c = texture(ui,uv);
  for (int i=0;i<8;++i) c += texture(ui,uv + disc[i]*radiusPx/p.screen.xy);
  return c/9.0;
}
void main() {
  vec2 px = fragUv*p.screen.xy;
  for (int i=0;i<int(p.counts.y);++i)
    if (inside(px,p.tools[i])) { outColor=texture(ui,fragUv); return; }
  float scale = max(p.screen.w,.001);
  vec4 state = vec4(1,0,0,0);
  float unsettled = -1.0;
  for (int i=0;i<int(p.counts.x);++i) {
    vec4 r = p.panels[i].rect + vec4(vec2(-24.0*scale),vec2(24.0*scale));
    vec4 s = p.panels[i].state;
    float score = max(1.0-s.x,max(s.y,s.z));
    if (inside(px,r) && score>unsettled) { state=s; unsettled=score; }
  }
  state.x = min(state.x,p.timing.z);
  bool calm = p.timing.y < .5;
  float local = min(state.x,1.0-state.y);
  if (!calm && (state.x<1.0 || state.y>0.0)) {
    float n = fbm(px/(180.0*scale)+state.w);
    local = min(smoothstep(n*.6,n*.6+.4,state.x),
                1.0-smoothstep(n*.6,n*.6+.4,state.y));
  }
  float unsync = 1.0-local;
  vec4 c;
  if (calm || unsync==0.0) {
    c = texture(ui,fragUv)*local;
  } else {
    vec2 flow = curl(px/(p.screen.y/6.0),p.timing.x*.3);
    vec2 amp = unsync*6.0*scale/p.screen.xy;
    float radius = unsync*4.0*scale;
    float r = blurSample(fragUv+flow*amp,radius).r;
    float g = blurSample(fragUv+flow.yx*amp*.3,radius).g;
    float b = blurSample(fragUv-flow*amp,radius).b;
    float a = blurSample(fragUv,radius).a;
    float fringe = max(r-b,0.0)*unsync;
    c = vec4(vec3(r,g,b)+p.warm.rgb*fringe*.5,a)*local;
    vec2 cell = floor(px/(18.0*scale));
    float h = hash12(cell+state.w);
    if (h>.965 && unsync>.05) {
      vec2 centre = (cell+.5+.3*(vec2(hash12(cell+1.7),hash12(cell+3.1))-.5))*18.0*scale;
      float d = length(px-centre)/(5.0*scale);
      float s = exp(-d*d)*unsync*(.6+.4*sin(p.timing.x*7.0+h*50.0));
      c.rgb += p.body.rgb*s*.8;
      c.a = max(c.a,s*.8);
    }
  }
  if (!calm && state.z>0.0) {
    vec4 ghost = blurSample(fragUv,3.0*scale);
    float lum = dot(ghost.rgb,vec3(.299,.587,.114));
    float level = lum*.08*state.z/max(p.counts.z,1e-4);
    c = vec4((vec3(1)-p.body.rgb)*level,level);
  }
  c.rgb *= p.timing.w*(1.0-p.screen.z);
  c.a *= 1.0-p.screen.z;
  outColor=c;
}
