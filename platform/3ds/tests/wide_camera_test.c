#include <assert.h>
#include <stdio.h>

#include "wide_camera.h"

static void TestWrappedCoordinates(void) {
  assert(WideCamera_Unwrap16(0xff00, 0x0000) == -256);
  assert(WideCamera_Unwrap16(0x0000, 0xff00) == 65536);
  assert(WideCamera_ClampToBounds(0, -256, 0, 72) == -72);
}

static void TestStableCamera(void) {
  assert(WideCamera_ClampToBounds(0, 0, 256, 72) == 72);
  assert(WideCamera_ClampToBounds(128, 0, 256, 72) == 128);
  assert(WideCamera_ClampToBounds(256, 0, 256, 72) == 184);
  assert(WideCamera_ClampToBounds(0, 0, 128, 72) == 0);
}

static void TestDungeonTransitionTargets(void) {
  assert(WideCamera_FindDungeonTransitionEnd(472, -1, 0) == 2);
  assert(WideCamera_FindDungeonTransitionEnd(780, -1, 0) == 514);
  assert(WideCamera_FindDungeonTransitionEnd(768, 1, 0) == 1024);
  assert(WideCamera_FindDungeonTransitionEnd(1024, 1, 256) == 1280);
}

static void TestRightTransition(void) {
  assert(WideCamera_InterpolateTransition(
    256, 256, 184, -72, 1, 256) == 184);
  assert(WideCamera_InterpolateTransition(
    384, 256, 184, -72, 1, 256) == 384);
  assert(WideCamera_InterpolateTransition(
    512, 256, 184, -72, 1, 256) == 584);
}

static void TestLeftTransition(void) {
  assert(WideCamera_InterpolateTransition(
    512, 512, 584, 72, -1, 256) == 584);
  assert(WideCamera_InterpolateTransition(
    384, 512, 584, 72, -1, 256) == 384);
  assert(WideCamera_InterpolateTransition(
    256, 512, 584, 72, -1, 256) == 184);
}

int main(void) {
  TestWrappedCoordinates();
  TestStableCamera();
  TestDungeonTransitionTargets();
  TestRightTransition();
  TestLeftTransition();
  puts("wide camera tests passed");
  return 0;
}
