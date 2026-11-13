// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D source;
struct Panel { vec4 rect; vec4 state; vec4 originAge; };
layout(std140,set=0,binding=1) uniform Params {
  Panel panels[16]; vec4 tools[4]; vec4 timing; vec4 screen; vec4 counts;
  vec4 warm; vec4 body; vec4 living; vec4 gazeTremor; vec4 periphery; vec4 optics; vec4 deep;
} p;
layout(push_constant) uniform Push { vec4 direction; } push;
vec4 sampleUi(vec2 uv) {
  if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return vec4(0);
  vec2 px=uv*p.screen.xy;
  for(int i=0;i<int(p.counts.y);++i)
    if(all(greaterThanEqual(px,p.tools[i].xy)) && all(lessThan(px,p.tools[i].zw))) return vec4(0);
  return texture(source,uv);
}
void main() {
  vec4 sum=vec4(0); float weights=0;
  for(int i=-4;i<=4;++i) {
    float x=float(i)*.5, w=exp(-.5*x*x);
    sum+=sampleUi(fragUv+push.direction.xy*x)*w; weights+=w;
  }
  outColor=sum/weights;
}
