#pragma once
#include <SDL.h>
#include <stdbool.h>
#include <math.h>

// SDL renderer event watches rewrite FINGER coordinates using their viewport.
// Preserve the native 320x240 point before those watches run, in a private
// queued event they cannot reinterpret. No polling thread or allocation.
static Uint32 native_touch_event;
static SDL_EventFilter native_touch_previous_filter;
static void *native_touch_previous_data;
static int SDLCALL NativeTouch_Filter(void *unused, SDL_Event *event) {
  (void)unused;
  SDL_Event original = *event;
  if (native_touch_previous_filter &&
      !native_touch_previous_filter(native_touch_previous_data, event)) return 0;
  if (original.type != SDL_FINGERDOWN || original.tfinger.touchId != 0) return 1;
  float nx = original.tfinger.x, ny = original.tfinger.y;
  if (!isfinite(nx) || !isfinite(ny) || nx < 0 || nx > 1 || ny < 0 || ny > 1) return 0;
  int x = (int)lroundf(nx * 320), y = (int)lroundf(ny * 240);
  if (x > 319) x = 319;
  if (y > 239) y = 239;
  SDL_zero(*event);
  event->user.type = native_touch_event;
  event->user.timestamp = original.tfinger.timestamp;
  event->user.code = (y << 16) | x;
  return 1;
}
static bool NativeTouch_Init(void) {
  if (native_touch_event) return true;
  Uint32 type = SDL_RegisterEvents(1);
  if (type == (Uint32)-1) return false;
  native_touch_event = type;
  SDL_GetEventFilter(&native_touch_previous_filter, &native_touch_previous_data);
  SDL_SetEventFilter(NativeTouch_Filter, NULL);
  return true;
}
static bool NativeTouch_Decode(const SDL_Event *event, float *x, float *y) {
  if (!native_touch_event || event->type != native_touch_event) return false;
  *x = (Uint32)event->user.code & 0xffff;
  *y = (Uint32)event->user.code >> 16;
  return *x < 320 && *y < 240;
}
static void NativeTouch_Shutdown(void) {
  if (!native_touch_event) return;
  SDL_EventFilter current; void *data;
  SDL_GetEventFilter(&current, &data);
  if (current == NativeTouch_Filter)
    SDL_SetEventFilter(native_touch_previous_filter, native_touch_previous_data);
  SDL_FlushEvent(native_touch_event);
  native_touch_event = 0;
  native_touch_previous_filter = NULL;
  native_touch_previous_data = NULL;
}
