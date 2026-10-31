<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR-0022: Explicit presentation time and opaque holographic pieces

- **Status:** Accepted
- **Date:** 2026-10-31

## Context

ADR-0021 separates cosmetic materials from game identity. Manifold now needs moving
holographic light on playing pieces, reproducible when capturing or seeking, including
pieces oriented onto curved boards. Capture warmup advances UI transitions and must
not advance a material's phase. Normalized overture and move progress are not seconds.

## Decision

The GUI supplies explicit nonnegative double presentation seconds. Interactive frames
advance one elapsed clock; capture `--visual-time` supplies its origin. Ordinary clips
add output frame index / fps, saved playback adds its global seconds, and trailer
output adds index times fixed step. Warmup and pointer redraws reuse the same sample.
This does not replace the existing background or simulation clocks.

The renderer sanitizes negative/nonfinite samples to zero and uploads seconds modulo
120 in a trailing vec4. The common push block is 128 bytes, with visual data at offset
112, within Vulkan's guaranteed minimum. Time is sampled at recording, independently
of instance construction. UI control animation will consume the same explicit input.

Internal material 4 is renamed Hologram, retaining its shader id. It uses smooth normals,
filtered static height lines, a subdued rim and a descending light band sampled using
mesh-authored normalized foot-to-crown height. Orientation and travel cannot move the
pattern off the piece. The approximation remains opaque, preserves incoming alpha and
silhouette, and leaves flat piece decals matte. No new pass, sorting, displacement or
engine/replay state is introduced. Glass cells and the plinth remain material 3 here.

## Consequences

Reverse/seek and repeated stills are stateless. Both shaders and C++ must retain the
same push layout. The 4-second scan divides the wrap period without a visible reset.
Visible animated themes use the existing short event timeout even on an idle board.
Hardware performance and appearance still require artifact review; software timing
cannot establish a hardware frame budget. LED surface coordinates are a later decision.
