<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR-0025: Frame-local HDR scene composition before the interface

- **Status:** Accepted
- **Date:** 2026-10-31
- **Supersedes:** ADR-0018's direct scene-to-swapchain attachment decision when post-processing is enabled; its frame ring and submission ownership remain in force.

## Context

The board's LED and hologram materials can produce light above the authored display
range, but the UNORM scene attachment clips it before a bloom pass can use it.
Applying post-processing to the final frame would also change interface text and
controls. The existing renderer has two frames in flight, dynamic rendering,
runtime output formats, and an independently recorded UI background field inside
the multisampled scene pass. Captures share that recording path and read RGBA8.

## Decision

Keep the public output target and its readback in the existing display format.
When enabled and supported, render the scene into renderer-owned float colour
attachments for the current frame slot, with a matching multisampled image and
single-sample resolve. Reuse the target depth buffer. Filter bloom in float images
and composite directly into the acquired swapchain or capture target, then run the
existing pause blur and draw the ordinary interface. There is no final image blit.

Keep the existing LDR scene path as the explicit disabled and unsupported-device
fallback. Scene pipeline variants, including the interface's background-field
pipeline, match the effective scene attachment format. The final interface and blur
pipelines continue to match the output target. HDR-only emission gain and the moved
scene vignette are selected by the effective path; disabling post preserves the
legacy shader behavior.

A renderer-owned RAII helper owns HDR images, bloom images and descriptors per
frame slot. A slot is reused or resized only after its existing fence has completed.
Images are allocated on first use or resize, with bloom allocated only when needed.
Descriptors referencing those images obey the same lifetime. Recording does not
submit, wait the queue idle, or overwrite resources still in use by another slot.

All noise derives from explicit presentation time and pixel coordinates. No frame
history or hidden clock participates in post-processing. The operation uses the
existing authored numeric colour space; it does not silently convert the palette
to a different transfer function. Mathematical identity applies only below the
shoulder with other effects neutralized, not to arbitrary finished LDR scenes.

## Consequences

Post-processing adds float colour memory, format-specific pipelines and fullscreen
passes. Unsupported float attachment/filter/sample configurations retain the LDR
path. Allocation or device failures remain visible errors. Both output formats,
frame slots, background-field submission, resize and disabled-path compatibility
need real Vulkan coverage. Software-renderer timing cannot establish an integrated
GPU performance budget.

The final composite still writes directly into the swapchain. Window acquisition,
the two-slot fence ring, caller-owned command buffers and presentation retain
ADR-0018's ownership. Headless captures exercise the same scene and composite
passes. No engine state, replay data, vertex attributes or material identity changes.

Implementation and acceptance: `docs/plan/M23.10-presentation-polish.md` §2.
