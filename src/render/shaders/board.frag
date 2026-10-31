// SPDX-License-Identifier: GPL-3.0-or-later
//
// Material-dispatched shading: matte and metal retain their original lighting;
// wood adds object-space grain and a varnish highlight.
#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragLocal;
layout(location = 3) in vec4 fragEdge;
layout(location = 4) in float fragEdgeMask;
layout(location = 5) in float fragHeight;
layout(location = 6) in vec2 fragEdgeInset;
layout(location = 7) flat in float fragMaterial;
layout(location = 8) in vec3 fragWorld;
layout(location = 9) in vec3 fragObject;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
    vec4 material;
    vec4 visual;
} push;

layout(location = 0) out vec4 outColor;

bool maskHas(float mask, int bit) {
    return mod(floor(mask / pow(2.0, float(bit))), 2.0) >= 0.5;
}

const float kWoodScale = 3.0;
const float kRingsBoard = 7.0;
const float kRingsPiece = 9.0;
const float kRingAmp = 0.19;
const float kRingMean = 0.80;
const float kFibreAmp = 0.055;
const float kVarnishPower = 48.0;
const float kVarnishBoard = 0.10;
const float kVarnishPiece = 0.18;

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float woodFactor(vec3 obj, float variation, bool turned) {
    vec3 g;
    float s;
    if (turned) {
        g = vec3(obj.z, obj.x, obj.y);
        s = variation;
    } else {
        g = variation < 0.5 ? obj : vec3(obj.y, obj.x, obj.z);
        s = fract(variation * 2.0);
    }
    vec2 pith = vec2(1.7 + 1.3 * s, -0.9 - 1.1 * fract(s * 7.13));
    vec3 q = g * kWoodScale + vec3(s * 41.0, s * 13.0, 0.0);
    // One octave: a second one cost several ms at 1080p on the software renderer.
    float warp = valueNoise(vec3(q.x * 0.35, q.y * 2.0, q.z * 2.0)) - 0.5;
    float r = length(g.yz - pith) * (turned ? kRingsPiece : kRingsBoard) + warp * 1.6;
    float t = fract(r);
    float ring = smoothstep(0.0, 0.12, t) * (1.0 - smoothstep(0.72, 1.0, t));
    float fibre = valueNoise(vec3(q.x * 1.2, q.y * 38.0, q.z * 38.0));
    return 1.0 + kRingAmp * (ring - kRingMean) + kFibreAmp * (fibre - 0.5);
}

void main() {
    int matId = int(fragMaterial);
    float matVar = fragMaterial - float(matId);
    // Two-sided: turn the normal toward the viewer before lighting, so a fragment on
    // either side of the surface is shaded the same.
    vec3 n = normalize(fragNormal);
    if (dot(n, push.eyePos.xyz - fragWorld) < 0.0) n = -n;
    float lambert = max(dot(n, normalize(-push.lightDir.xyz)), 0.0);

    // A warm key and a cool fill, so vertical faces stay distinct from horizontal ones
    // without going black.
    vec3 warm = vec3(1.06, 0.96, 0.84);
    vec3 cool = vec3(0.80, 0.85, 1.00);
    vec3 shade = mix(cool * 0.52, warm, lambert);

    // Pieces darken slightly toward the foot: a tall piece should read as standing on
    // the board rather than floating above a flat blob.
    shade *= mix(0.80, 1.06, clamp(fragHeight, 0.0, 1.0));

    vec3 rgb = fragColor.rgb * shade;

    if (matId == 2 || matId == 6) {
        rgb *= woodFactor(fragObject, matVar, matId == 6);
        vec3 toEye = normalize(push.eyePos.xyz - fragWorld);
        vec3 halfV = normalize(normalize(-push.lightDir.xyz) + toEye);
        float spec = pow(max(dot(n, halfV), 0.0), kVarnishPower);
        rgb += vec3(1.00, 0.93, 0.82) * spec * (matId == 6 ? kVarnishPiece : kVarnishBoard);
    }

    if (matId == 3) {
        vec3 viewDir = normalize(push.eyePos.xyz - fragWorld);
        float fresnel = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
        float glassShade = 0.55 + 0.28 * max(dot(n, vec3(-0.25, 0.5, 0.82)), 0.0);
        rgb = fragColor.rgb * glassShade + push.material.rgb * fresnel * 0.38;
        // A narrow, luminous bevel between glass panels, never a bright solid slab.
        float edge = max(abs(fragLocal.x), abs(fragLocal.y));
        float grid = smoothstep(0.445, 0.46, edge);
        rgb += push.material.rgb * grid * 0.08;
    }

    if (matId == 4) {
        float height = clamp(fragHeight, 0.0, 1.0);
        vec3 viewDir = normalize(push.eyePos.xyz - fragWorld);
        vec3 coolKey = normalize(vec3(0.35, 0.6, 0.72));
        float face = max(dot(n, coolKey), 0.0);
        float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
        float lineCoord = height * 28.0;
        float footprint = fwidth(lineCoord);
        float lines = 0.5 + 0.5 * cos(lineCoord * 6.2831853);
        lines = mix(lines, 0.5, smoothstep(0.25, 1.0, footprint));
        float centre = 1.08 - 1.16 * fract(push.visual.x / 4.0);
        float scan = 1.0 - smoothstep(0.0, 0.055, abs(height - centre));
        vec3 emission = mix(fragColor.rgb, push.material.rgb, 0.20);
        float luminance = dot(fragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec3 base = mix(fragColor.rgb, vec3(luminance), 0.35);
        rgb = base * (0.86 + 0.16 * face) * (0.92 + 0.08 * lines) +
              emission * (0.12 * rim + 0.16 * scan);
    }

    if (matId == 5) {
        rgb = fragColor.rgb * push.material.a;
    }

    // A mirror face is polished metal rather than a painted band: a tight specular
    // highlight and a faint rim, so bright white reads as silvered glass. The view
    // direction is fixed because the board is looked at from above the frame; it is
    // enough to catch the light as the face turns.
    if (matId == 1) {
        vec3 toLight = normalize(-push.lightDir.xyz);
        vec3 toView = normalize(vec3(0.0, 0.42, 1.0));
        vec3 halfV = normalize(toLight + toView);
        float spec = pow(max(dot(n, halfV), 0.0), 64.0);
        float rim = pow(1.0 - max(dot(n, toView), 0.0), 3.0);
        rgb = rgb * 0.92 + vec3(0.9) * spec + vec3(0.35) * rim;
    }

    // A seam edge: the cell's side is glued to somewhere else on the board. Drawn only
    // on the sides the geometry actually identified, so a player can pair the two edges
    // by eye rather than being told the whole cell is special.
    if (fragEdge.a > 0.0 && n.z > 0.35) {
        vec2 inset = clamp(fragEdgeInset, vec2(0.0), vec2(0.49));
        bool onEdge =
            (maskHas(fragEdgeMask, 0) && fragLocal.x < -inset.x) ||
            (maskHas(fragEdgeMask, 1) && fragLocal.x >  inset.x) ||
            (maskHas(fragEdgeMask, 2) && fragLocal.y < -inset.y) ||
            (maskHas(fragEdgeMask, 3) && fragLocal.y >  inset.y) ||
            // The sides of the box always take the rim, which is what gives the board's
            // plinth a lit edge you can see from a low angle.
            (fragEdgeMask >= 15.0 && n.z < 0.35);
        if (onEdge) {
            float reach = max(abs(fragLocal.x) - inset.x, abs(fragLocal.y) - inset.y);
            float glow = clamp(reach / max(0.5 - min(inset.x, inset.y), 0.001), 0.0, 1.0);
            rgb = mix(rgb, fragEdge.rgb, clamp(0.30 + 0.55 * glow, 0.0, 1.0));
        }
    }

    outColor = vec4(rgb, fragColor.a);
}
