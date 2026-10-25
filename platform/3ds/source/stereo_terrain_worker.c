#include "stereo_terrain_worker.h"
#include "platform_3ds.h"
#include <3ds.h>
#include <string.h>

enum { kInlineEvery = 8 };   // without core 1 time, solve on the render thread this often

// Handoff: the render thread owns job, front and back. It writes the job only
// while the worker is idle, and swaps front/back only after the worker signals
// done, so the worker never writes a buffer the renderer is reading.
static struct {
  bool started, threaded, busy, hasFront, resetPending;
  bool running, done;           // shared with the worker, atomic access only
  int front, back;
  unsigned gen, postedGen;      // a reset discards a solve that was already running
  Thread thread;
  LightEvent start;
  StereoTerrainJob job;
  StereoTerrainOut out[2];
  unsigned frames, solves;
  uint32_t lastUs, maxUs;
} w;

static uint32_t TicksToUs(uint64_t ticks) { return (uint32_t)(ticks * 1000000ull / SYSCLOCK_ARM11); }

static void WorkerMain(void *arg) {
  (void)arg;
  for (;;) {
    LightEvent_Wait(&w.start);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&w.running, __ATOMIC_ACQUIRE)) break;
    uint64_t t0 = svcGetSystemTick();
    StereoTerrain_Solve(&w.job, &w.out[w.back]);
    w.out[w.back].us = TicksToUs(svcGetSystemTick() - t0);
    __atomic_store_n(&w.done, true, __ATOMIC_RELEASE);
  }
}

static void Start(void) {
  w.started = true;
  if (Platform3DS_CanUseCore1PpuWorker()) {
    LightEvent_Init(&w.start, RESET_ONESHOT);
    __atomic_store_n(&w.running, true, __ATOMIC_RELEASE);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Below the render thread, on the system core.
    w.thread = threadCreate(WorkerMain, NULL, 16 * 1024, priority + 1, 1, false);
    w.threaded = w.thread != NULL;
    if (!w.threaded) __atomic_store_n(&w.running, false, __ATOMIC_RELEASE);
  }
  Platform3DS_LogRuntime("Stereo terrain: %s", w.threaded ? "solving on core 1" : "solving inline");
}

static void Finished(int buffer) {
  w.solves++;
  w.lastUs = w.out[buffer].us;
  if (w.lastUs > w.maxUs) w.maxUs = w.lastUs;
}

bool TerrainWorker_Frame(PicaTerrain *t, const Ppu *p, const StereoTerrainSource *src) {
  if (!w.started) Start();
  w.frames++;
  if (w.threaded) {
    if (w.busy && __atomic_load_n(&w.done, __ATOMIC_ACQUIRE)) {
      __atomic_store_n(&w.done, false, __ATOMIC_RELAXED);
      w.busy = false;
      if (w.postedGen == w.gen && w.out[w.back].valid) {
        w.front = w.back; w.hasFront = true;
        Finished(w.front);
      }
    }
    if (!w.busy) {
      w.back = w.hasFront ? 1 - w.front : 0;
      StereoTerrain_MakeJob(&w.job, p, src, w.resetPending);
      w.resetPending = false;
      w.postedGen = w.gen;
      w.busy = true;
      __atomic_thread_fence(__ATOMIC_RELEASE);
      LightEvent_Signal(&w.start);
    }
  } else if (!w.hasFront || w.frames % kInlineEvery == 0) {
    StereoTerrain_MakeJob(&w.job, p, src, w.resetPending);
    w.resetPending = false;
    uint64_t t0 = svcGetSystemTick();
    StereoTerrain_Solve(&w.job, &w.out[0]);
    w.out[0].us = TicksToUs(svcGetSystemTick() - t0);
    w.front = 0; w.hasFront = w.out[0].valid;
    if (w.hasFront) Finished(0);
  }
  if (!w.hasFront) { t->valid = false; return false; }
  StereoTerrain_Sprites(&w.out[w.front], p, t);
  return t->valid;
}

void TerrainWorker_RequestReset(void) {
  w.resetPending = true;
  w.hasFront = false;
  w.gen++;
}

void TerrainWorker_Shutdown(void) {
  if (!w.threaded) return;
  __atomic_store_n(&w.running, false, __ATOMIC_RELEASE);
  LightEvent_Signal(&w.start);
  threadJoin(w.thread, U64_MAX);
  threadFree(w.thread);
  w.thread = NULL; w.threaded = false; w.started = false; w.busy = false; w.hasFront = false;
}

void TerrainWorker_Diagnostics(FILE *f) {
  fprintf(f, "PICA terrain_worker=%s frames=%u solves=%u last_solve_us=%lu max_solve_us=%lu changed_tiles=%u sweeps=%u\n",
          !w.started ? "not-started" : w.threaded ? "core1" : "inline", w.frames, w.solves,
          (unsigned long)w.lastUs, (unsigned long)w.maxUs,
          w.hasFront ? w.out[w.front].changed : 0, w.hasFront ? w.out[w.front].sweeps : 0);
}
