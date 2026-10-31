// SPDX-License-Identifier: GPL-3.0-or-later
//
// One instanced draw per shape covers the whole board: every cell is an instance of the
// slab mesh, and every piece an instance of its archetype. The draw-call count is the
// number of shapes in play - seven or eight - and is independent of board size, so a
// million-cell lattice is a buffer upload rather than a scene graph.
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inHeight;   // 0 at the piece's foot, 1 at its crown
layout(location = 11) in vec4 inVertColor;  // white on authored shapes; the geometry
                                            // view's board is one mesh of many colours

layout(location = 3) in vec3 instCenter;
layout(location = 4) in vec3 instScale;
layout(location = 5) in vec4 instColor;
layout(location = 6) in vec4 instEdge;    // seam colour; alpha 0 means no seam here
layout(location = 7) in float instEdgeMask;

layout(location = 8) in float instRoll;     // rotation about the mesh's own Z, radians
layout(location = 9) in float instMaterial;
layout(location = 10) in vec4 instQuat;     // full orientation (x,y,z,w); identity flat

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
    vec4 material;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragLocal;
layout(location = 3) out vec4 fragEdge;
layout(location = 4) out float fragEdgeMask;
layout(location = 5) out float fragHeight;
layout(location = 6) out vec2 fragEdgeInset;
layout(location = 7) flat out float fragMaterial;
layout(location = 8) out vec3 fragWorld;
layout(location = 9) out vec3 fragObject;

// Rotate a vector by a unit quaternion.
vec3 rotateByQuat(vec3 v, vec4 q) {
    const vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main() {
    // Spin the mesh in its own plane before scaling, so one quarter-round corner can be
    // turned into any of the four quadrants it is needed in.
    const float cr = cos(instRoll);
    const float sr = sin(instRoll);
    const vec2 spun = vec2(inPosition.x * cr - inPosition.y * sr,
                           inPosition.x * sr + inPosition.y * cr);
    // Size it in its OWN frame, before it is turned. Scaling after the rotation scales
    // the world axes instead of the mesh's, so on the geometry view - where every tile is
    // turned onto the surface - a cell drawn to the size of its own square came out
    // stretched along whichever way the world happened to point. The two orders agree
    // exactly wherever the orientation is identity, which is the whole of the flat board.
    vec3 local = vec3(spun, inPosition.z) * instScale;
    // Then orient the whole mesh - the geometry view turns each tile onto the surface's
    // own frame, so the board is built from real geometry the ordinary camera and depth
    // buffer can see. Identity on a flat board.
    vec3 world = instCenter + rotateByQuat(local, instQuat);
    gl_Position = push.viewProj * vec4(world, 1.0);
    fragColor = instColor * inVertColor;
    const vec2 nspun = vec2(inNormal.x * cr - inNormal.y * sr,
                            inNormal.x * sr + inNormal.y * cr);
    // The normal takes the inverse scale, in the same frame the scale was applied in.
    fragNormal = normalize(rotateByQuat(
        vec3(nspun, inNormal.z) / max(abs(instScale), vec3(1e-6)), instQuat));
    fragWorld = world;
    fragObject = local;
    fragLocal = inPosition.xy;
    fragEdge = instEdge;
    fragEdgeMask = instEdgeMask;
    fragHeight = inHeight;
    fragMaterial = instMaterial;

    // Where the seam band begins, in the box's own coordinates. Derived from the
    // instance's scale so the band is a constant *world* width: without this a large
    // instance - the plinth under the board - gets a border a third of its width, which
    // stops reading as a rim and starts reading as a paint job.
    const float kBandWorld = 0.085;
    fragEdgeInset = vec2(0.5) - kBandWorld / max(abs(instScale.xy), vec2(0.001));
}
