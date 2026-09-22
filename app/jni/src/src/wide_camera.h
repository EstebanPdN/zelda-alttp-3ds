#ifndef ZELDA3_WIDE_CAMERA_H_
#define ZELDA3_WIDE_CAMERA_H_

int WideCamera_Unwrap16(int value, int reference);
int WideCamera_ClampToBounds(int logical_x, int left_bound,
                             int right_bound, int margin);
int WideCamera_FindDungeonTransitionEnd(int start_x, int direction,
                                        int target);
int WideCamera_InterpolateTransition(int logical_x, int start_logical_x,
                                     int start_visual_x, int end_offset,
                                     int direction, int distance);

#endif  // ZELDA3_WIDE_CAMERA_H_
