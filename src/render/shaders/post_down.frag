// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
layout(location=0) in vec2 fragUv;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D source;
layout(push_constant) uniform Push {
    vec2 texel; float strength; float grain;
    uint seed; uint first; float vignette; float pad;
} push;

void main() {
    const vec2 offsets[13] = vec2[](vec2(0), vec2(-2,0), vec2(2,0), vec2(0,-2), vec2(0,2),
        vec2(-2,-2), vec2(2,-2), vec2(-2,2), vec2(2,2),
        vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
    const float weights[13] = float[](.125, .0625,.0625,.0625,.0625,
        .03125,.03125,.03125,.03125, .125,.125,.125,.125);
    vec3 sum=vec3(0);
    float total=0;
    for(int i=0;i<13;++i) {
        vec3 c=max(texture(source,fragUv+offsets[i]*push.texel).rgb,vec3(0));
        float w=weights[i];
        if(push.first!=0u) w/=1.0+dot(c,vec3(.2126,.7152,.0722));
        sum+=c*w;
        total+=w;
    }
    sum/=total;
    if(push.first!=0u) {
        const float T=.85, knee=.25;
        float lum=dot(sum,vec3(.2126,.7152,.0722));
        float q=clamp(lum-T+knee,0,2*knee);
        float soft=q*q/(4*knee);
        sum*=max(lum-T,soft)/max(lum,1e-5);
    }
    outColor=vec4(sum,1);
}
