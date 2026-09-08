# Citro3D compatibility declaration

`internal.h` and `LICENSE` are unmodified from devkitPro/citro3d tag v1.7.1.
The installed build dependency is citro3d 1.7.1-2. No replacement library is
built. `ppu_gpu_c3d.h` uses this layout only to unbind Old 3DS's second sampler,
a case the public `C3D_TexBind(1, NULL)` cannot handle. A runtime pointer-layout
canary rejects the GPU backend before setup if the layout is incompatible.
New 3DS never calls the adapter. Replace this adapter with a public unbind API
when the library provides one; do not remove the canary.

Source: https://github.com/devkitPro/citro3d/tree/v1.7.1
