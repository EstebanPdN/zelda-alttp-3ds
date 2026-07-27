// No-op SecondScreenSDL_* hooks for the Android build (the second screen is the
// Java Presentation UI). Keeps main.c link-clean without compiling the SDL UI.
#include <SDL.h>
#include <stdbool.h>

bool SecondScreenSDL_Init(SDL_Window *main_window) { (void)main_window; return false; }
bool SecondScreenSDL_HandleEvent(const SDL_Event *e) { (void)e; return false; }
void SecondScreenSDL_Update(int logic_frames) { (void)logic_frames; }
void SecondScreenSDL_RequestDump(void) {}
void SecondScreenSDL_Shutdown(void) {}
