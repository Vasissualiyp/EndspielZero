// SPDX-License-Identifier: GPL-3.0-or-later
//
// The room behind the board (M23.9 B and E). Drawn first, inside the board's pass, so the
// board's own geometry and the ImGui ambient floor land over it.
//
//  * The light the board spills: a glow centred on the board's projected screen position
//    and sized by its screen extent, tinted by the board's emission colour, plus a lit
//    patch on the floor under it.
//  * Far stars: points on a sphere at infinity, reconstructed from the camera's view ray.
//    They rotate with the orbit but cannot translate, so they read as very far away.
#version 450

layout(location = 0) in vec2 uv;

layout(push_constant) uniform Push {
    vec4 inner;   // board emission colour: the light at the board's centre
    vec4 outer;   // the room's own colour
    vec4 params;  // x,y = board centre in uv, z = stars on (1) or off (0), w = vignette
    vec4 board;   // x,y = board extent-size in uv, z = floor spill strength, w = intensity
    vec4 right;   // xyz = camera right, w = tan(fovY / 2)
    vec4 up;      // xyz = camera up, w = aspect of the board viewport
    vec4 fwd;     // xyz = camera forward, w = seconds (modulo 120)
    vec4 map;     // uv -> board ndc: ndc = uv * map.xz + map.yw
} push;

layout(location = 0) out vec4 outColor;

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float unit(uint x) { return float(x >> 8) * (1.0 / 16777216.0); }

// Sparse dim points on the sphere of directions. A cell of the 3-D lattice that the ray
// enters owns at most one star, jittered inside it; its direction is the cell point
// normalised, so the layout depends on direction only.
float stars(vec3 dir, float pixelAngle, float seconds, float intensity, out float tint) {
    const float kCells = 34.0;
    vec3 p = dir * kCells;
    ivec3 cell = ivec3(floor(p));
    uint h = hash(uint(cell.x + 4096) * 0x1f3d5b79u ^ uint(cell.y + 4096) * 0x9e3779b1u ^
                  uint(cell.z + 4096) * 0x85ebca6bu ^ 0x2545f491u);
    tint = unit(hash(h ^ 0x68bc21ebu));
    if (unit(h) > 0.075) return 0.0;  // density
    vec3 jitter = vec3(unit(hash(h ^ 0x1234567u)), unit(hash(h ^ 0x89abcdefu)),
                       unit(hash(h ^ 0x31415926u)));
    vec3 star = normalize(vec3(cell) + 0.2 + 0.6 * jitter);
    float d = length(dir - star);
    float radius = pixelAngle * (0.7 + 0.9 * unit(hash(h ^ 0x27182818u)));
    float core = 1.0 - smoothstep(radius * 0.4, radius * 1.4, d);
    // A slow twinkle, reading the shared presentation intensity: still when calm.
    float k = 8.0 + float(hash(h) % 8u);  // whole cycles per 120 s: continuous at the wrap
    float tw = 1.0 - 0.35 * intensity *
                   (0.5 + 0.5 * sin(6.2831853 * (seconds * k / 120.0 +
                                                unit(hash(h ^ 0x55555555u)))));
    return core * tw * (0.35 + 0.65 * unit(hash(h ^ 0xaaaaaaaau)));
}

void main() {
    vec2 centre = push.params.xy;
    vec2 extent = max(push.board.xy, vec2(0.02));
    vec2 d = (uv - centre) / extent;
    float r2 = dot(d, d);
    float glow = exp(-r2 * 0.85);
    vec3 rgb = mix(push.outer.rgb, push.inner.rgb, glow);

    // The floor under the board: a wider, flatter patch below the horizon, as if the board
    // lit the table it stands on.
    vec2 fd = vec2((uv.x - centre.x) / (extent.x * 1.9),
                   (uv.y - (centre.y + extent.y * 0.95)) / (extent.y * 0.85));
    float spill = exp(-dot(fd, fd) * 1.3) * smoothstep(0.40, 0.52, uv.y);
    rgb += push.inner.rgb * (push.board.z * spill);

    // Far stars, only where the camera is a perspective one.
    if (push.params.z > 0.5) {
        vec2 ndc = uv * push.map.xz + push.map.yw;
        float tanY = push.right.w;
        vec3 dir = normalize(push.fwd.xyz + push.right.xyz * (ndc.x * tanY * push.up.w) -
                             push.up.xyz * (ndc.y * tanY));
        float pixelAngle = length(fwidth(dir));  // one pixel, whatever the resolution
        float tint;
        float s = stars(dir, pixelAngle, push.fwd.w, push.board.w, tint);
        // Fade inside and near the board's silhouette so the board stays the subject.
        float silhouette = smoothstep(1.0, 1.9, sqrt(r2));
        vec3 cool = mix(vec3(0.62, 0.70, 0.88), vec3(0.85, 0.88, 0.95), tint);
        rgb += cool * s * silhouette * 0.38;
    }

    // Corner falloff, so the panels at the edges sit on the darkest part of the frame.
    vec2 c = abs(uv - 0.5) * 2.0;
    float vignette = 1.0 - push.params.w * pow(max(c.x, c.y), 2.6);
    rgb *= clamp(vignette, 0.0, 1.0);

    outColor = vec4(rgb, 1.0);
}
