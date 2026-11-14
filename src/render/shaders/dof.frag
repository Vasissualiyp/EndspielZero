// SPDX-License-Identifier: GPL-3.0-or-later
//
// Depth of field for the lab room (M24.4c 4.4; M24.4d adds the daydream floor and spot): one half of a separable gather blur whose
// radius is the circle of confusion at each pixel's own depth. Run across, then down. The
// UI layer is composited afterwards and never sees this pass.
#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D source;
layout(set = 0, binding = 1) uniform sampler2DMS depthTex;

layout(push_constant) uniform Push {
    vec2 direction;  // one texel on the axis being blurred, in uv
    float focus;     // world units
    float cocScale;  // view::cocScaleFor
    float maxPx;     // kWorldDefocusPixels * pixelScale
    float nearZ;
    float farZ;
    float floorPx;      // M24.4d daydream: uniform circle-of-confusion floor, pixels
    vec2 spot;          // focus spot centre, pixels
    float spotRadius;   // pixels
    float spotFeather;  // pixels
    float spotFraction; // the floor's share left inside the spot
    float pad0;
    float pad1;
    float pad2;
} push;

// Mirrors view::linearDepth: the camera's finite Vulkan projection, depth in [0, 1].
float linearDepth(float d) { return push.nearZ * push.farZ / (push.farZ - d * (push.farZ - push.nearZ)); }

// Mirrors view::circleOfConfusion.
float coc(float z) {
    return clamp(abs(1.0 / max(z, 0.001) - 1.0 / max(push.focus, 0.001)) * push.cocScale, 0.0, 1.0) *
           push.maxPx;
}

// Mirrors view::daydreamBlurPixels.
float floorAt(ivec2 p) {
    float inside = 1.0 - smoothstep(0.0, 1.0, (distance(vec2(p) + 0.5, push.spot) - push.spotRadius) / push.spotFeather);
    return push.floorPx * (1.0 - (1.0 - push.spotFraction) * inside);
}

float cocAt(ivec2 p) {
    p = clamp(p, ivec2(0), textureSize(depthTex) - 1);
    return max(coc(linearDepth(texelFetch(depthTex, p, 0).r)), floorAt(p));
}

void main() {
    ivec2 ip = ivec2(gl_FragCoord.xy);
    vec4 centre = texture(source, fragUv);
    float centreCoc = cocAt(ip);
    if (centreCoc < 0.25) {
        outColor = centre;
        return;
    }
    const float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
    float spacing = centreCoc / 4.0;
    vec2 axis = push.direction.x != 0.0 ? vec2(1, 0) : vec2(0, 1);
    vec4 sum = centre * w[0];
    float weight = w[0];
    for (int i = 1; i < 5; ++i) {
        for (int side = -1; side <= 1; side += 2) {
            float offset = float(side * i) * spacing;
            float tapCoc = cocAt(ip + ivec2(round(axis * offset)));
            // A sharper foreground never takes colour from a blurrier background behind it.
            float k = w[i] * step(tapCoc, centreCoc + 0.5);
            sum += texture(source, fragUv + push.direction * offset) * k;
            weight += k;
        }
    }
    outColor = sum / weight;
}
