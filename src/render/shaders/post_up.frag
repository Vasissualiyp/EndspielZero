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
    vec3 sum=vec3(0);
    for(int y=-1;y<=1;++y)
        for(int x=-1;x<=1;++x) {
            float weight=float((x==0?2:1)*(y==0?2:1));
            sum+=texture(source,fragUv+vec2(x,y)*push.texel).rgb*weight;
        }
    outColor=vec4(sum/16.0,1);
}
