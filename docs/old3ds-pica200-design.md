# ALttP Old 3DS PICA200 renderer: feasibility and implementation boundary

Status: researched design, not an implemented backend. E11 still rasterizes the
SNES picture on the CPU. Its retained background planes improve the software
fallback; they must not be advertised as a PICA200 renderer.

## Evidence and objective

The four E10 Old 3DS WIDE captures spend 25.3–37.8 ms in the software PPU.
The two exterior captures present approximately 24.5–24.7 frames/s. Game logic
is about 1.5–1.6 ms and the main-thread presentation call about 0.3 ms. Moving
actual rasterization/composition to PICA200 targets the dominant cost. A 60 FPS
picture needs the entire critical path below 16.67 ms, with audio and UI alive.
No hardware result for an ALttP GPU renderer exists yet.

[Minish Cap PR #26](https://github.com/EstebanPdN/zelda-tmc-3ds/pull/26)
introduced a real tile/sprite GPU backend. Its later E9–E11 parity-size and
affine-coordinate repairs kept that backend active. ALttP currently has no
such backend to re-enable. The technique transfers; GBA rendering rules do not.
[Snes9x 3DS](https://github.com/bubble2k16/snes9x_3ds) is a closer architectural
reference: `source/gfxhw.cpp` renders SNES main/sub screens and applies color
math on the GPU, including ALttP prologue fixes. Its acknowledged color
limitations make it evidence of feasibility, not a byte-exact drop-in solution.

## Proposed implementation

1. **Prepared frame, before submission.** Refactor `ZeldaDrawPpuLines` so the
   existing HDMA/line-128 register updates can record per-line state without
   rasterizing. Preserve the frame-start PPU and HDMA state for software fallback.
   Consecutive lines with matching draw state form bands. Keep the existing
   fixed-camera offsets, extended spotlight windows, and 256/400-wide geometry.
2. **Retained tile atlas.** Decode only changed SNES 2bpp/4bpp BG/OBJ tiles into
   an RGBA5551 atlas, with palette-bank generations, transparent index zero,
   correct flips and VRAM wrapping. Retain map geometry separately from animated
   atlas content. Pin entries used by the queued frame; never overwrite texture
   bytes while the GPU can still sample them. Fixed capacities fail before draw.
3. **Main and sub targets.** Draw tile/sprite quads into separate 512x256 targets
   at the active native dimensions. Encode the existing PPU priority and OBJ
   ordering in depth/draw order. Scissor bands and window intervals rather than
   issuing one draw call per pixel. Preserve per-line OBJ limits and all priority
   ties; widescreen pixel count is not permission to skip off-screen state.
4. **SNES color composition.** Preserve the visible layer's math eligibility,
   color-window clipping, fixed color and sub-screen backdrop identity. Implement
   add/subtract/half and brightness with TEV/blend/stencil passes only where
   measured GPU output matches the CPU. In particular, an absent sub-screen
   pixel selects fixed color without halving; saturated addition and half-add
   have different ordering. GBA alpha-blend code is not equivalent.
5. **One presenter owner.** Render before the existing top scaling pass inside
   its Citro3D frame. Feed a GPU texture to the top presenter and retain the
   existing bottom/UI, format and lifecycle state resets. The software path still
   uploads its output. New 3DS keeps its established renderer/profile.
6. **Explicit fallback.** Initial unsupported Mode 7, mosaic, unusual HDMA,
   resource exhaustion and unproved blend states use the complete CPU path for
   that frame. Restore original state before fallback, so HDMA is not applied
   twice. Record the exact reason and its frequency. Avoid an unreported permanent
   GPU retirement; a parity failure must be visible in diagnostics.

## Validation gates

- CPU model tests for atlas addressing, priority, window intervals, clipping,
  per-line state and resource lifetime; deterministic active-width/height checks.
- On-console color probes covering 32x32 channel pairs, all 16 brightness levels,
  add/subtract/half, clipping and backdrop exceptions. PICA200 has a fixed fragment
  pipeline: arithmetic/rounding must be observed, not inferred from host tests.
- Compare only active pixels and actual scanlines. Log first differing coordinate,
  CPU/GPU RGB, layer, register state, renderer eligibility and fallback reason.
  Dump the currently displayed GPU image, not the last software upload.
- Measure preparation, changed tile upload, submission, GPU execution, waits,
  renderer-active percentage and actual visual cadence separately. Do not benchmark
  continuously synchronous readback; parity probes are a separate diagnostic mode.
- Accept on physical Old 3DS in interiors, moving exteriors/rain, map, transitions,
  item menus, HOME/resume and sustained audio. Preserve New 3DS visual tests.

The next substantial speed step is a native GPU tile/composition backend. E11's
software work and phase dumps supply a tested fallback and a measurable baseline;
they do not complete that implementation or establish sustained 60 FPS.
