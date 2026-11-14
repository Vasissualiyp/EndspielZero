// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=1) uniform sampler2D bloom;
layout(push_constant) uniform Push {
    vec2 texel; float strength; float grain;
    uint seed; uint first; float vignette; uint wakeActive;
    // M24.7 waking up: the eyelids and the defocus, exposure and desaturation behind them.
    float lidTop; float lidBottom; float curve; float feather;
    float blurPx; float exposureEv; float saturation; float lidGlow;
} push;
uint hash(uint x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu;
    return x^(x>>16);
}
float unit(uint x) { return float(x>>8)/16777216.0; }
float toneMap(float x) {
    const float K=.8;
    if(x<=K) return x;
    return K+(1-K)*(1-exp(-(x-K)/(1-K)));
}
// A 24-tap golden-angle spiral (Vogel disc) of radius blurPx, equal weights, plus the centre.
vec3 defocused(vec2 uv) {
    vec3 sum=texture(scene,uv).rgb;
    for(int i=0;i<24;++i) {
        float r=sqrt((float(i)+.5)/24.0)*push.blurPx;
        float a=float(i)*2.39996323;
        sum+=texture(scene,uv+vec2(cos(a),sin(a))*r*push.texel).rgb;
    }
    return sum/25.0;
}
// Where an eyelid edge sits at height y: soft, and exactly absent when it covers nothing.
float lidOpenness(vec2 uv) {
    float x=(uv.x-.5)*2, y=uv.y;
    float bow=push.curve*.10*x*x;
    float top=push.lidTop+bow, bottom=1-push.lidBottom-bow;
    float f=push.feather;
    // A lid that covers nothing must not darken the frame's own edge.
    float topShift=max(0.,f-top), bottomShift=max(0.,f-(1-bottom));
    return smoothstep(top-topShift-f,top-topShift+f,y)
         *(1-smoothstep(bottom+bottomShift-f,bottom+bottomShift+f,y));
}
void main() {
    bool wake=push.wakeActive!=0u;
    vec3 rgb=max((wake && push.blurPx>.5 ? defocused(fragUv) : texture(scene,fragUv).rgb),vec3(0));
    if(push.strength>0) rgb+=push.strength*texture(bloom,fragUv).rgb;
    if(wake) {
        rgb*=exp2(push.exposureEv);
        rgb=mix(vec3(dot(rgb,vec3(.2126,.7152,.0722))),rgb,push.saturation);
    }
    float m=max(rgb.r,max(rgb.g,rgb.b));
    if(m>0) rgb*=toneMap(m)/m;
    vec2 edge=abs(fragUv-.5)*2;
    // Smooth rounded square; see backdrop.frag and postVignette in post_params.cpp.
    vec2 e2=edge*edge;
    vec2 e4=e2*e2;
    vec2 e8=e4*e4;
    rgb*=clamp(1-push.vignette*pow(e8.x+e8.y-e8.x*e8.y,2.6/8.0),0,1);
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    uint h=hash(pixel.x*0x1f3d5b79u ^ pixel.y*0x9e3779b1u ^ push.seed*0x85ebca6bu);
    float y=clamp(dot(rgb,vec3(.2126,.7152,.0722)),0,1);
    float grain=(2*unit(hash(h^0xa54ff53au))-1)*push.grain*4*y*(1-y);
    float dither=(unit(hash(h^0x68bc21ebu))-unit(hash(h^0x31415926u)))/255;
    vec3 shown=clamp(rgb+vec3(grain+dither),0,1);
    if(wake) {
        // The lids cover the world and exactly nothing else; with no glow they are pure black.
        // The hue of #7A2A18 at full strength: the output is written as-is, and 3 % of the
        // darker hex itself would be one 8-bit step.
        const vec3 lidGlowColor=vec3(1.0,.345,.196);
        float open=lidOpenness(fragUv);
        shown=shown*open+lidGlowColor*push.lidGlow*(1-open);
    }
    outColor=vec4(shown,1);
}
