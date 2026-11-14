// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D ui;
layout(set=0,binding=2) uniform sampler2D uiSoft;
layout(set=0,binding=3) uniform sampler2D uiHalo;
struct Panel { vec4 rect; vec4 state; vec4 originAge; };
layout(std140,set=0,binding=1) uniform Params {
  Panel panels[16]; vec4 tools[4]; vec4 timing; vec4 screen; vec4 counts;
  vec4 warm; vec4 body; vec4 living; vec4 gazeTremor; vec4 periphery; vec4 optics; vec4 deep;
  vec4 hand;  // M24.7: handshake age (-1 none), menu fade; M24.4d: daydream amount, 0
} p;
float hash12(vec2 v) {
  vec3 q=fract(vec3(v.xyx)*.1031); q+=dot(q,q.yzx+33.33); return fract((q.x+q.y)*q.z);
}
float noise(vec2 v) {
  vec2 i=floor(v),f=fract(v); f=f*f*(3-2*f);
  return mix(mix(hash12(i),hash12(i+vec2(1,0)),f.x),mix(hash12(i+vec2(0,1)),hash12(i+vec2(1)),f.x),f.y);
}
vec2 curl(vec2 v,float t) {
  const float e=.01; v+=vec2(0,t);
  vec2 c=vec2(noise(v+vec2(0,e))-noise(v-vec2(0,e)),-noise(v+vec2(e,0))+noise(v-vec2(e,0)))/(2*e);
  return c/max(1,length(c));
}
// How far the handshake front has to travel to reach px: the brief's ln(1+6d) law over the
// screen diagonal, 0 at the centre and 1 in the corners.
float handUnit(vec2 px) {
  vec2 size=p.screen.xy;
  float d=clamp(length(px-size*.5)/(.5*length(size)),0,1);
  return log(1+6*d)/log(7.0);
}
bool inside(vec2 px,vec4 r) { return all(greaterThanEqual(px,r.xy)) && all(lessThan(px,r.zw)); }
vec4 safeSample(sampler2D tex,vec2 uv) {
  if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1)))) return vec4(0);
  // A warped panel must never duplicate a tool into the rest of the image.
  for(int i=0;i<int(p.counts.y);++i) if(inside(uv*p.screen.xy,p.tools[i])) return vec4(0);
  return texture(tex,uv);
}
float roundedDistance(vec2 px,vec4 r,float radius) {
  vec2 q=abs(px-(r.xy+r.zw)*.5)-(r.zw-r.xy)*.5+radius;
  return length(max(q,0))+min(max(q.x,q.y),0)-radius;
}
vec4 shade() {
  vec2 size=p.screen.xy,px=fragUv*size;
  for(int i=0;i<int(p.counts.y);++i) if(inside(px,p.tools[i])) return texture(ui,fragUv);
  bool living=p.living.x>.5,calm=p.timing.y<.5;
  float scale=max(.001,p.screen.w);
  vec2 centre=size*.5,delta=px-centre;
  float r2=dot(delta,delta)/(.25*dot(size,size));
  vec2 src=living ? centre+delta*(1+p.living.y*r2) : px;
  vec2 uv=src/size;
  vec4 sharp=safeSample(ui,uv);
  // Select only records containing this pixel. Unrecorded pixels can never emit
  // transition light: the old sparks defaulted to local=1 yet still ran globally.
  int chosen=-1; float score=-1;
  for(int i=0;i<int(p.counts.x);++i) {
    Panel panel=p.panels[i];
    float priority=max(1-panel.state.x,max(panel.state.y,panel.state.z));
    if(inside(src,panel.rect+vec4(vec2(-24*scale),vec2(24*scale))) && priority>score) {chosen=i;score=priority;}
  }
  bool handshake=p.hand.x>=0;
  if(!living && chosen<0 && !handshake && p.hand.y>=1) return sharp;
  vec4 soft=safeSample(uiSoft,uv),halo=safeSample(uiHalo,uv);
  vec4 c=sharp;
  float local=1,veilAlpha=0;
  if(chosen>=0) {
    Panel panel=p.panels[chosen]; vec4 st=panel.state;
    float age=panel.originAge.z;
    if(age<0) age=st.y>0 ? st.y*.22 : st.x*.45;
    vec2 origin=clamp(panel.originAge.xy,panel.rect.xy,panel.rect.zw);
    if(all(equal(panel.originAge.xy,vec2(0)))) origin=clamp(p.gazeTremor.xy,panel.rect.xy,panel.rect.zw);
    float d=clamp(length(src-origin)/max(1,length(panel.rect.zw-panel.rect.xy))+(noise(src/(320*scale)+st.w)-.5)*.1,0,1);
    float reached=.26*log(1+6*d)/log(7.0);
    float s=smoothstep(0,1,(age-reached)/.19);
    bool leaving=st.y>0 || st.z>0;
    if(calm) {
      local=min(st.x,1-st.y); c=sharp*local;
    } else if(leaving) {
      float fade=smoothstep(0,.22-.10*log(1+6*d)/log(7.0),age);
      local=1-fade;
      vec2 flow=curl(src/(size.y/3),p.timing.x*.3);
      vec2 smear=flow*5*scale*fade/size;
      c=mix(safeSample(ui,uv+smear),safeSample(uiSoft,uv+smear),smoothstep(0,.1,age))*local;
      if(age<.06) c.rgb*=1+.3*(1-smoothstep(0,.12,d));
      if(st.z>0) {
        float persistence=st.z/max(p.counts.z,1e-4);
        c=vec4(p.deep.rgb*halo.a,halo.a)*(.06*persistence);
      }
    } else if(panel.originAge.w<.5 && st.x<1) {
      float bands=.5*smoothstep(0,.12,age);
      c=mix(halo,soft,smoothstep(0,.45,s));
      c=mix(c,sharp,smoothstep(.45,1,s));
      local=mix(bands,1,s);
      c*=local;
      vec2 flow=curl(src/(size.y/3),p.timing.x*.3)*2.5*scale*(1-s)/size;
      vec4 fringe=mix(safeSample(uiSoft,uv+flow),safeSample(ui,uv+flow),smoothstep(.45,1,s));
      vec4 blue=mix(safeSample(uiSoft,uv-flow),safeSample(ui,uv-flow),smoothstep(.45,1,s));
      c.r=mix(c.r,fringe.r*local,s*(1-s)); c.b=mix(c.b,blue.b*local,s*(1-s));
      c.rgb+=p.warm.rgb*max(0,c.r-c.b)*.4*(1-s);
    }
    float confinement=1-smoothstep(20*scale,24*scale,max(0,roundedDistance(src,panel.rect,0)));
    c*=confinement;
  }
  if(living) {
    // The attentional veil (BCI brief 2): room dimming, never painted, 90 px smooth falloff
    // from the rect, 40 px corners, no noise. It is not light, so the 24 px confinement of
    // record effects does not apply; it follows each record's own progress.
    float uiFocus=clamp(p.living.z,0,1);
    for(int i=0;i<int(p.counts.x);++i) {
      Panel panel=p.panels[i];
      float progress=panel.originAge.w>.5 ? 1 : min(smoothstep(0,.9,panel.state.x),1-panel.state.y);
      float edge=1-smoothstep(0,90*scale,max(0,roundedDistance(src,panel.rect,40*scale)));
      veilAlpha=max(veilAlpha,p.living.w*edge*progress*mix(.35,1,uiFocus)*p.timing.z);
    }
  }
  if(living) {
    float focus=clamp(p.living.z,0,1);
    float ecc=min(length(px-p.gazeTremor.xy),1.3*length(delta))/size.y;
    float peripheral=smoothstep(p.periphery.x,p.periphery.y,ecc);
    // Daydream (M24.4d): the whole panel softens, less inside the gaze's focus spot.
    // Mirrors view::kSpotRadius, kSpotFeather, kSpotBlurFraction and kDaydreamUiSoft.
    float spotInside=1-smoothstep(0,1,(length(px-p.gazeTremor.xy)/size.y-.12)/.06);
    float daydream=p.hand.z*.8*(1-.75*spotInside);
    c=mix(c,soft*local,max(max(.4*(1-focus),.5*peripheral),daydream));
    // Resampling compensation is fixed in time, never applied to the breathing glow.
    c.rgb+=.25*max(vec3(0),sharp.rgb-soft.rgb)*local;
    c.rgb=mix(c.rgb,vec3(dot(c.rgb,vec3(.299,.587,.114))),peripheral*p.periphery.z);
    c*=mix(p.optics.x,1,focus);
    vec2 radial=delta/max(length(delta),1);
    vec2 fringe=radial*peripheral*p.periphery.w*scale/size;
    c.r=mix(c.r,safeSample(ui,uv+fringe).r*local,.25*peripheral);
    c.b=mix(c.b,safeSample(ui,uv-fringe).b*local,.25*peripheral);
    vec2 glowUv=uv+(calm ? vec2(0) : p.gazeTremor.zw)/size;
    vec4 tight=safeSample(uiSoft,glowUv), broad=safeSample(uiHalo,glowUv);
    float glowFocus=mix(.35,1,focus);
    // Stable opaque glyph cores: tremor and heartbeat only breathe around them.
    float outsideCore=1-smoothstep(.8,.9,sharp.a);
    vec3 glow=tight.rgb*.35+broad.a*p.deep.rgb*.12*(calm ? 1 : 1+3*(p.timing.w-1));
    c.rgb+=glow*local*glowFocus*outsideCore;
    c.a=max(c.a,(tight.a*.35+broad.a*.12)*local*glowFocus*outsideCore);
  }
  if(handshake) {
    // The launch handshake (brief 3.6): the main menu locks in behind the front, slowly,
    // every pixel resolving over half a second. A pixel's unsync is max(panel's, 1-sync).
    float gs=smoothstep(0,1,(p.hand.x-(.6+.7*handUnit(px)))/.5);
    float bands=.5*smoothstep(0,.12,p.hand.x-.6);
    vec4 locked=mix(halo,soft,smoothstep(0,.45,gs));
    locked=mix(locked,c,smoothstep(.45,1,gs));
    float local2=mix(bands,1,gs);
    locked*=local2;
    vec2 flow=curl(src/(size.y/3),p.timing.x*.3)*4*scale*(1-gs)/size;
    vec4 red=mix(safeSample(uiSoft,uv+flow),safeSample(ui,uv+flow),smoothstep(.45,1,gs));
    vec4 blue=mix(safeSample(uiSoft,uv-flow),safeSample(ui,uv-flow),smoothstep(.45,1,gs));
    locked.r=mix(locked.r,red.r*local2,gs*(1-gs));
    locked.b=mix(locked.b,blue.b*local2,gs*(1-gs));
    locked.rgb+=p.warm.rgb*max(0,locked.r-locked.b)*.4*(1-gs);
    if(chosen>=0)
      locked*=1-smoothstep(20*scale,24*scale,max(0,roundedDistance(src,p.panels[chosen].rect,0)));
    c=locked;
  }
  c*=p.hand.y; veilAlpha*=p.hand.y;
  c.rgb*=1-p.screen.z; c.a*=1-p.screen.z;
  c.a=c.a+veilAlpha*(1-c.a);
  return clamp(c,0,1);
}
// Phosphene mapping (brief 3.6): 24 soft blobs on a log-polar lattice, lit in spiral order.
vec4 phosphenes(vec2 px) {
  const float ecc[3]=float[3](.05,.16,.42), radius[3]=float[3](5,14,36), peak[3]=float[3](.32,.22,.13);
  vec2 size=p.screen.xy; float scale=max(.001,p.screen.w);
  vec3 rgb=vec3(0); float alpha=0;
  for(int ring=0;ring<3;++ring) for(int k=0;k<8;++k) {
    int n=k*3+ring;
    float jitter=hash12(vec2(float(n),7.3))*2-1, jitter2=hash12(vec2(float(n),1.9))*2-1;
    float a=(float(k)*45.+float(ring)*22.5+jitter2*4.5)*.01745329;
    float e=ecc[ring]*(1+.1*jitter)*size.y;
    vec2 at=size*.5+vec2(cos(a),sin(a))*e;
    float lit=exp(-.5*pow((p.hand.x-(.06+float(n)*.028))/.085,2.0));
    float r=radius[ring]*scale,split=1.5*scale;
    vec2 d=px-at;
    float aR=exp(-1.5*dot(d-vec2(split,0),d-vec2(split,0))/(r*r));
    float aG=exp(-1.5*dot(d,d)/(r*r));
    float aB=exp(-1.5*dot(d+vec2(split,0),d+vec2(split,0))/(r*r));
    vec3 k3=vec3(aR,aG,aB)*peak[ring]*lit;
    rgb+=p.body.rgb*k3; alpha+=k3.g;
  }
  return vec4(rgb,min(alpha,1));
}
void main() {
  vec4 c=shade();
  vec2 size=p.screen.xy,px=fragUv*size;
  bool tool=false;
  for(int i=0;i<int(p.counts.y);++i) tool=tool || inside(px,p.tools[i]);
  if(p.hand.x>=0 && !tool) {
    vec4 ph=phosphenes(px);
    c.rgb=ph.rgb+c.rgb*(1-ph.a); c.a=ph.a+c.a*(1-ph.a);
    // One broad front out of the centre: a faint lift of the room in implantDeep.
    float front=exp(-.5*pow((handUnit(px)-(p.hand.x-.5)/.7)/.05,2.0))*smoothstep(0,.05,p.hand.x-.5);
    c.rgb+=p.deep.rgb/max(p.deep.b,1e-4)*.04*front*(1-smoothstep(1.2,1.4,p.hand.x));
  }
  outColor=clamp(c,0,1);
}
