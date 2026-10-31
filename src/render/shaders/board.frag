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
layout(location = 10) in vec3 fragPanel;
layout(location = 11) in vec4 fragPanelSignal;
layout(location = 12) in vec4 fragPanelPrev;

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

// Integer-only event scheduling: each logical display cell has an independent,
// reproducible phase. No camera/world coordinates or mutable random state enter it.
uint panelHash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float panelUnit(uint x) {
    return float(x >> 8) * (1.0 / 16777216.0);
}
float panelInterference(vec2 uv, uint seed, float seconds) {
    uint epoch = uint(floor(seconds / 12.0));
    uint event = panelHash(seed ^ (epoch * 0x9e3779b9u) ^ 0x68bc21ebu);
    if (event % 3u != 0u) return 0.0;
    float start = 0.5 + 10.5 * panelUnit(panelHash(event ^ 0x1234567u));
    float age = seconds - float(epoch) * 12.0 - start;
    if (age <= 0.0 || age >= 0.28) return 0.0;
    float envelope = smoothstep(0.0, 0.06, age) *
                       (1.0 - smoothstep(0.22, 0.28, age));
    float width = 0.18 + 0.17 * panelUnit(panelHash(event ^ 0x89abcdefu));
    float x = 0.15 + (0.70 - width) * panelUnit(panelHash(event ^ 0x31415926u));
    float y = float(4u + panelHash(event ^ 0x27182818u) % 15u) / 24.0;
    vec2 lo = vec2(x, y);
    vec2 hi = lo + vec2(width, 1.0 / 24.0);
    vec2 feather = vec2(1.0 / 24.0, 0.5 / 24.0);
    vec2 band = smoothstep(lo, lo + feather, uv) *
                 (vec2(1.0) - smoothstep(hi - feather, hi, uv));
    return -0.028 * envelope * band.x * band.y;
}

// ---- M23.9 C: bounded aliveness. Every effect has its own explicit bound; none is
// clamped together with the others. All are pure in `seconds` (push.visual.x, modulo 120)
// and integer-hashed from the cell seed, so a capture at --t reproduces. The glitch
// schedule is mirrored in src/render/panel_alive.cpp, which the unit tests check: keep
// the constants and the hash lanes in step.
const float kBreathBound = 0.05;     // per-cell emission breathing, +-
const float kSweepBound = 0.06;      // refresh sweep, +
const float kGlitchSlot = 1.5;       // 120 / 1.5 is whole: no event straddles the wrap
const float kRowShiftMax = 0.30;     // emitter pitches
const float kChannelShiftMax = 0.35; // emitter pitches
const float kDropoutFloor = 0.15;    // brightness during a dropout

struct Glitch {
    bool on;
    float u;      // 0..1 through the event
    uint kind;    // 0 row shift, 1 tear, 2 channel offset, 3 dropout
    uint lane;    // hash lane for row / sign
};
Glitch panelGlitch(uint seed16, float seconds, float modulus) {
    Glitch g = Glitch(false, 0.0, 0u, 0u);
    uint slot = uint(floor(seconds / kGlitchSlot));
    uint slotHash = panelHash(slot * 0x9e3779b9u ^ 0x51ed270bu);
    uint m = max(uint(modulus), 1u);
    if (seed16 % m != (slotHash >> 8) % m) return g;
    uint event = panelHash(slotHash ^ (seed16 * 0x68bc21ebu));
    float start = 0.05 + 1.15 * panelUnit(panelHash(event ^ 0x1234567u));
    float dur = 0.08 + 0.17 * panelUnit(panelHash(event ^ 0x89abcdefu));
    float age = seconds - float(slot) * kGlitchSlot - start;
    if (age <= 0.0 || age >= dur) return g;
    g.on = true;
    g.u = age / dur;
    g.kind = panelHash(event ^ 0x31415926u) % 4u;
    g.lane = panelHash(event ^ 0x27182818u);
    return g;
}

// Living colour change: seeded per-emitter delay/duration (mirror of emitterTransition in
// src/render/panel_alive.cpp). ~80 % of emitters finish within 120 ms, a lagging tail takes
// 2-4x longer and a very few lazy ones drift in over 0.4-0.6 s.
vec2 emitterTiming(uint seed16, vec2 id) {
    uint h = panelHash(seed16 ^ (uint(id.x) * 0x1f3d5b79u) ^ (uint(id.y) * 0x9e3779b1u) ^
                       0x7a3c5e91u);
    float cls = panelUnit(h);
    float a = panelUnit(panelHash(h ^ 0x85ebca6bu));
    float b = panelUnit(panelHash(h ^ 0xc2b2ae35u));
    if (cls < 0.80) return vec2(0.040 * a, 0.040 + 0.040 * b);
    if (cls < 0.98) return vec2(0.060 + 0.100 * a, 0.120 + 0.200 * b);
    return vec2(0.100 + 0.100 * a, 0.300 + 0.100 * b);
}

// One emitter mask for a given pitch coordinate, filtered like the original.
float emitterMask(vec2 pitch, vec2 footprint, float undersampled) {
    vec2 d = abs(fract(pitch) - vec2(0.5));
    vec2 e = vec2(1.0) - smoothstep(vec2(0.36) - footprint * 0.5,
                                    vec2(0.36) + footprint * 0.5, d);
    return mix(e.x * e.y, 0.72 * 0.72, undersampled);
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
    // Evaluate footprints before material/face branches so derivatives remain defined.
    vec2 panelPitch = fragPanel.xy * 24.0;
    vec2 panelFootprint = max(fwidth(panelPitch), vec2(0.0001));

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
        // A projector-handoff copy (M23.9 D): `fragPanelSignal.w` is its scan dominance.
        // A partial projection is mostly the moving scan line and faint raster, not a
        // solid figure; only the destination resolves (w -> 0).
        float ghost = clamp(fragPanelSignal.w, 0.0, 1.0);
        vec3 scanOnly = emission * (0.10 + 0.50 * lines * lines + 0.90 * scan) +
                        base * 0.10 * (0.5 + 0.5 * face);
        rgb = mix(rgb, scanOnly, ghost);
    }

    if (matId == 7) {
        float seconds = push.visual.x;
        float intensity = clamp(push.visual.y, 0.0, 1.0);
        vec2 puv = fragPanel.xy;
        float undersampled = smoothstep(0.35, 0.65,
                                         max(panelFootprint.x, panelFootprint.y));
        vec2 border = min(puv, vec2(1.0) - puv);
        float face = smoothstep(0.0, 0.018, min(border.x, border.y));
        bool lit = fragPanel.z >= 0.5;
        bool alive = lit && fragPanelSignal.x > 0.0;
        uint seed16 = alive ? uint(floor(fragPanelSignal.x * 65536.0 + 0.5)) : 0u;
        float weightRaw = fragPanelSignal.z;
        float weight = clamp(abs(weightRaw), 0.0, 1.0);
        bool cue = weightRaw < 0.0;  // a gameplay colour is shown: no glitches here

        vec2 pitch = panelPitch;
        Glitch glitch = Glitch(false, 0.0, 0u, 0u);
        if (alive && intensity >= 0.5 && !cue && undersampled < 0.5)
            glitch = panelGlitch(seed16, seconds, push.visual.z);
        float rowIndex = floor(panelPitch.y);
        float glitchRow = float(glitch.lane % 22u) + 1.0;
        float glitchWave = sin(3.14159265 * glitch.u);
        float shift = 0.0;
        if (glitch.on && glitch.kind == 0u &&
            rowIndex >= glitchRow && rowIndex < glitchRow + 3.0)
            shift = ((glitch.lane >> 8) % 2u == 0u ? 1.0 : -1.0) * kRowShiftMax * glitchWave;
        if (glitch.on && glitch.kind == 1u && rowIndex == glitchRow)
            shift = ((glitch.lane >> 8) % 2u == 0u ? 2.0 : -2.0) * glitchWave;
        pitch.x += shift;

        // Emission (A): only emitter centres carry it; the gaps between them stay dark.
        // `emitAmt` follows the cell colour's luminance, so light squares and bright
        // gameplay colours emit and a dark square stays a dark square.
        vec3 cellCol = fragColor.rgb;
        float landing = 0.0;
        if (alive && fragPanelPrev.w >= 0.0) {
            float age = fragPanelPrev.w;
            float x;
            if (undersampled > 0.5) {
                x = clamp((age - 0.02) / 0.06, 0.0, 1.0);
            } else {
                vec2 t = emitterTiming(seed16, floor(pitch));
                x = clamp((age - t.x) / t.y, 0.0, 1.0);
            }
            float p = x * x * (3.0 - 2.0 * x);
            cellCol = mix(fragPanelPrev.rgb, fragColor.rgb, p);
            // A slight brightness overshoot as an emitter lands.
            landing = 0.10 * sin(3.14159265 * clamp((x - 0.55) / 0.45, 0.0, 1.0)) * intensity;
        }
        float lum = dot(cellCol, vec3(0.2126, 0.7152, 0.0722));
        float emitAmt = smoothstep(0.115, 0.155, lum);
        vec3 mask = vec3(emitterMask(pitch, panelFootprint, undersampled));
        if (glitch.on && glitch.kind == 2u) {
            float d = kChannelShiftMax * glitchWave;
            mask.r = emitterMask(pitch + vec2(d, 0.0), panelFootprint, undersampled);
            mask.b = emitterMask(pitch - vec2(d, 0.0), panelFootprint, undersampled);
        }
        // Sub-pixel bleed: brightest at the emitter centre, falling off inside it. The
        // radial profile collapses to its mean once the emitters are undersampled.
        float radial = 1.0 - smoothstep(0.0, 0.42, length(fract(pitch) - vec2(0.5)));
        radial = mix(radial, 0.45, undersampled);
        float gapLevel = mix(0.82, 0.16, emitAmt);
        float emitterLevel = mix(1.04, 1.00 + 4.60 * radial, emitAmt);
        vec3 level = mix(vec3(gapLevel), vec3(emitterLevel), mask);
        // The restrained glow: emitters lift their own face a little, falling off to the
        // border. An in-shader approximation; no extra pass and no screen-wide bloom.
        level += vec3(0.06 * emitAmt * face);
        rgb = cellCol * level * (1.0 + landing) * mix(0.58, 1.0, face);
        if (!lit) rgb = fragColor.rgb * 0.55;

        if (alive) {
            // Breathing: a slow per-cell variation. Periods are 120 / k seconds, so the
            // clock wrap is continuous.
            float k = 8.0 + float(panelHash(seed16 ^ 0x3c6ef372u) % 8u);
            float phase = panelUnit(panelHash(seed16 ^ 0xa54ff53au));
            float breath = sin(6.2831853 * (seconds * k / 120.0 + phase));
            float lumMul = 1.0 + kBreathBound * breath * intensity;
            // Refresh sweep: one faint front, every 24 s, over the panel field in world
            // space with a row raster inside it.
            float sweepT = clamp(fract(seconds / 24.0) / 0.125, 0.0, 1.0);
            float front = -16.0 + 32.0 * sweepT;
            float dSweep = dot(fragWorld.xy, vec2(0.6, 0.8)) - front;
            float sweepBand = exp(-dSweep * dSweep / 1.4) * step(0.0001, sweepT) *
                              step(sweepT, 0.9999);
            float raster = 0.6 + 0.4 * (0.5 + 0.5 * cos(panelPitch.y * 6.2831853));
            raster = mix(raster, 0.8, undersampled);
            lumMul += kSweepBound * sweepBand * raster * intensity;
            // Wandering emitter faults (mirror of emitterFault in panel_alive.cpp): about
            // one cell in six has up to two, each living 8-60 s, fading in and out and
            // then reappearing elsewhere; blinkers blink ~1-3 times a second.
            if (intensity >= 0.5 && panelHash(seed16 ^ 0x1b873593u) % 6u == 0u &&
                undersampled < 0.5) {
                const uint kPeriods[9] = uint[9](8u, 10u, 12u, 15u, 20u, 24u, 30u, 40u, 60u);
                for (uint slot = 0u; slot < 2u; ++slot) {
                    uint fh = panelHash(seed16 * 0x2c1b3c6du ^ slot * 0x297a2d39u ^ 0x6a09e667u);
                    uint period = kPeriods[fh % 9u];
                    float pf = float(period);
                    float fx = seconds + pf * panelUnit(panelHash(fh ^ 0x1111u));
                    float raw = floor(fx / pf);
                    uint epoch = uint(raw) % (120u / period);
                    float phase = fx / pf - raw;
                    uint e = panelHash(fh ^ (epoch * 0x9e3779b9u) ^ 0x5bd1e995u);
                    if (e % 3u != 0u) continue;
                    vec2 which = vec2(float(2u + (e >> 4) % 20u), float(2u + (e >> 12) % 20u));
                    if (!all(equal(floor(panelPitch), which))) continue;
                    uint kind = (e >> 20) % 3u;
                    float w = phase / 0.7;
                    float env = w >= 1.0 ? 0.0 : smoothstep(0.0, 0.15, w) * (1.0 - smoothstep(0.85, 1.0, w));
                    float level = kind == 0u ? 0.12 : 1.7;
                    if (kind == 2u) {
                        uint r = panelHash(uint(floor(seconds * 6.0)) ^ e);
                        level = r % 4u == 0u ? 0.12 : (r % 9u == 0u ? 1.6 : 1.0);
                        if (intensity < 0.5) level = 1.0;  // calm: no motion
                    }
                    lumMul *= mix(1.0, level, env);
                }
            }
            rgb *= mix(1.0, lumMul, face);
            // The existing sparse interference strip and the LCD settling response.
            float strip = panelInterference(puv, seed16, seconds);
            float settle = clamp(fragPanelSignal.y, -0.04, 0.04);
            rgb *= 1.0 + (strip + settle) * clamp(weight, 0.0, 1.0) * face *
                         (1.0 - undersampled);
            if (glitch.on) {
                if (glitch.kind == 1u) {
                    // The tear's own row runs brighter across the whole cell.
                    float bright = rowIndex == glitchRow ? 0.40 * glitchWave : 0.0;
                    rgb *= 1.0 + bright * face;
                } else if (glitch.kind == 3u) {
                    // Dropout: dark for the first half, then the panel settles back.
                    float dip = glitch.u < 0.5 ? kDropoutFloor
                                   : mix(kDropoutFloor, 1.0, smoothstep(0.5, 1.0, glitch.u));
                    rgb *= mix(1.0, dip, face);
                }
            }
        }
        // Projector base (D): a faint standing footprint, warm and neutral, and the
        // heat-up ahead of a handoff. Its own channel, never the colour response.
        float heat = clamp(fragPanelSignal.w, 0.0, 1.0);
        if (lit && heat > 0.0) {
            // A neutral warm-white glow concentrated under the piece's base - never a cell
            // fill, so it cannot be read as a move cue.
            float dc = length(puv - vec2(0.5));
            float radius = 0.17 + 0.11 * heat;
            float disc = 1.0 - smoothstep(radius * 0.30, radius, dc);
            float lattice = mix(0.45 + 0.55 * mask.g, 0.75, undersampled);
            vec3 warmWhite = vec3(1.0, 0.90, 0.72);
            rgb += warmWhite * (0.45 * heat * disc * disc * lattice) * face;
        }
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
            // The board's lit bezel (M23.9 B): the plinth's rim on an emitting board glows.
            if (matId == 3 && fragEdgeMask >= 15.0) rgb += fragEdge.rgb * 0.55;
        }
    }

    outColor = vec4(rgb, fragColor.a);
}
