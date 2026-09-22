#include <SDL.h>
#include <stdlib.h>

#include "zelda_rtl.h"

int ZeldaProgramMain(int argc, char **argv);

static int InjectReplay(void *unused) {
  (void)unused;
  static const SDL_Keycode kReferenceKeys[] = {
    SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7,
    SDLK_8, SDLK_9, SDLK_0, SDLK_MINUS, SDLK_EQUALS, SDLK_BACKSPACE,
  };
  while (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
    SDL_Delay(10);

  SDL_Delay(250);
  const char *mode_text = getenv("ZELDA_CAMERA_MODE");
  ZeldaSetWidescreenEdgeMode(
    mode_text && atoi(mode_text) == 0 ? 0 : 1);

  if (getenv("ZELDA_CAMERA_RENDER_ALL")) {
    SDL_Event turbo_event = {0};
    turbo_event.type = SDL_KEYDOWN;
    turbo_event.key.state = SDL_PRESSED;
    turbo_event.key.keysym.sym = SDLK_t;
    SDL_PushEvent(&turbo_event);
    turbo_event.type = SDL_KEYUP;
    turbo_event.key.state = SDL_RELEASED;
    SDL_PushEvent(&turbo_event);
  }

  int chapter = 1;
  const char *chapter_text = getenv("ZELDA_CAMERA_CHAPTER");
  if (chapter_text)
    chapter = atoi(chapter_text);
  if (chapter < 1 || chapter > (int)(sizeof(kReferenceKeys) /
                                      sizeof(kReferenceKeys[0])))
    chapter = 1;

  SDL_Event event = {0};
  event.type = SDL_KEYDOWN;
  event.key.state = SDL_PRESSED;
  event.key.keysym.sym = kReferenceKeys[chapter - 1];
  event.key.keysym.mod = KMOD_CTRL;
  SDL_PushEvent(&event);

  event.type = SDL_KEYUP;
  event.key.state = SDL_RELEASED;
  SDL_PushEvent(&event);

  for (int i = 0; i < 600; i++) {
    SDL_Delay(100);
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
      return 0;
  }
  event.type = SDL_QUIT;
  SDL_PushEvent(&event);
  return 0;
}

int main(int argc, char **argv) {
  SDL_Thread *thread = SDL_CreateThread(InjectReplay, "camera-replay", NULL);
  int result = ZeldaProgramMain(argc, argv);
  if (thread)
    SDL_WaitThread(thread, NULL);
  return result;
}
