#pragma once
#include <citro3d.h>
#include "compat/citro3d-1.7.1/internal.h"

// Citro3D 1.7.1 cannot unbind unit 1 through TexBind(1, NULL): it first reads
// tex->param. Leaving the unit enabled instead feeds undefined C2D UV1 into
// sampling. Use the pinned library's exact unbind operation, with an ABI
// canary before any GPU setup. No library functions or New 3DS calls change.
static inline bool PicaC3DCompatible(void) {
  return &C3Di_GetContext()->texEnv[0] == C3D_GetTexEnv(0);
}
static inline void PicaC3DUnbindSecondary(void) {
  C3D_Context *ctx=C3Di_GetContext();
  ctx->tex[1]=NULL;
  ctx->flags|=C3DiF_Tex(1);
}

// FrameSync waits for VBlank counters, NOT for the command queue. Wait for
// actual DMA/draw completion before CPU buffer reuse/readback; no VBlank tax.
// Only called outside an application frame and after the layout canary.
static inline bool PicaC3DWaitIdle(void) {
  return gxCmdQueueWait(&C3Di_GetContext()->gxQueue, -1);
}

static inline bool PicaC3DWaitIdleFor(s64 timeout) {
  return gxCmdQueueWait(&C3Di_GetContext()->gxQueue, timeout);
}
