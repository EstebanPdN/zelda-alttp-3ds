#include "wide_camera.h"

#include <stdint.h>

int WideCamera_Unwrap16(int value, int reference) {
  return reference + (int16_t)((uint16_t)value - (uint16_t)reference);
}

int WideCamera_ClampToBounds(int logical_x, int left_bound,
                             int right_bound, int margin) {
  if (margin <= 0 || right_bound - left_bound < margin * 2)
    return logical_x;

  int minimum = left_bound + margin;
  int maximum = right_bound - margin;
  if (logical_x < minimum)
    return minimum;
  if (logical_x > maximum)
    return maximum;
  return logical_x;
}

int WideCamera_FindDungeonTransitionEnd(int start_x, int direction,
                                        int target) {
  if (direction != -1 && direction != 1)
    return start_x;

  uint16_t current = (uint16_t)start_x;
  target &= 0x1fc;
  for (int i = 0; i < 256; i++) {
    current = (uint16_t)((current + direction * 2) & ~1);
    if ((current & 0x1fc) == target)
      return start_x + (int16_t)(current - (uint16_t)start_x);
  }
  return start_x + direction * 256;
}

static int RoundedDivide(int numerator, int denominator) {
  if (numerator < 0)
    return -((-numerator + denominator / 2) / denominator);
  return (numerator + denominator / 2) / denominator;
}

int WideCamera_InterpolateTransition(int logical_x, int start_logical_x,
                                     int start_visual_x, int end_offset,
                                     int direction, int distance) {
  if ((direction != -1 && direction != 1) || distance <= 0)
    return logical_x;

  int progress = direction > 0 ?
    (int)(uint16_t)(logical_x - start_logical_x) :
    (int)(uint16_t)(start_logical_x - logical_x);
  if (progress > distance)
    progress = distance;

  int start_offset = start_logical_x - start_visual_x;
  int offset_delta = end_offset - start_offset;
  int offset = start_offset +
    RoundedDivide(offset_delta * progress, distance);
  return logical_x - offset;
}
