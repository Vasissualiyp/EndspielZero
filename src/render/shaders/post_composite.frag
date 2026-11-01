// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D scene;
layout(set=0,binding=1) uniform sampler2D bloom;
layout(push_constant) uniform Push {
    vec2 texel; float strength; float grain;
    uint seed; uint first; float vignette; float pad;
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
void main() {
    vec3 rgb=max(texture(scene,fragUv).rgb,vec3(0));
    if(push.strength>0) rgb+=push.strength*texture(bloom,fragUv).rgb;
    float m=max(rgb.r,max(rgb.g,rgb.b));
    if(m>0) rgb*=toneMap(m)/m;
    vec2 edge=abs(fragUv-.5)*2;
    rgb*=clamp(1-push.vignette*pow(max(edge.x,edge.y),2.6),0,1);
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    uint h=hash(pixel.x*0x1f3d5b79u ^ pixel.y*0x9e3779b1u ^ push.seed*0x85ebca6bu);
    float y=clamp(dot(rgb,vec3(.2126,.7152,.0722)),0,1);
    float grain=(2*unit(hash(h^0xa54ff53au))-1)*push.grain*4*y*(1-y);
    float dither=(unit(hash(h^0x68bc21ebu))-unit(hash(h^0x31415926u)))/255;
    outColor=vec4(clamp(rgb+vec3(grain+dither),0,1),1);
}
