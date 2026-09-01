#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <SDL.h>
#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#endif
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"
#include "android_logging.h"
#ifdef __3DS__
#include "platform_3ds.h"
#endif

static bool g_run_without_emu = 0;

// Dual-screen UI (second_screen_sdl.c); stubbed on Android
bool SecondScreenSDL_Init(SDL_Window *main_window);
bool SecondScreenSDL_HandleEvent(const SDL_Event *e);
void SecondScreenSDL_Handle3DSTouch(void);
void SecondScreenSDL_Update(int logic_frames);
void SecondScreenSDL_SetDiagnostics(int current_fps, int average_fps);
#ifdef __3DS__
void SecondScreenSDL_BeginFrame(int logic_frames);
#endif
void SecondScreenSDL_RequestDump(void);
void SecondScreenSDL_Shutdown(void);

// Forwards
static bool LoadRom(const char *filename);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
#ifndef __3DS__
static void OpenOneGamepad(int i);
#endif
static void HandleVolumeAdjustment(int volume_adjustment);
static void LoadAssets();
#ifndef __3DS__
static void SwitchDirectory();
#endif

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
#ifdef __3DS__
static uint32 g_win_flags = SDL_WINDOW_FULLSCREEN;
#else
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
#endif
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
static uint8 g_current_window_scale;
static uint8 g_gamepad_buttons;
static int g_input1_state;
static bool g_display_perf;
static int g_curr_fps;
#ifdef __3DS__
static int g_3ds_current_fps;
static int g_3ds_average_fps;
static uint32 g_3ds_visual_fps_window_start_ms;
static uint32 g_3ds_visual_fps_window_frames;
static uint32 g_3ds_average_fps_samples[20];
static uint32 g_3ds_average_fps_sample_count;
static uint32 g_3ds_average_fps_sample_pos;
#endif
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];
#ifdef __3DS__
static uint32 g_3ds_last_ppu_draw_us;
static uint32 g_3ds_last_capture_us;
static uint32 g_3ds_last_present_us;

static uint32 TicksToMicroseconds(uint64 ticks) {
  uint64 frequency = SDL_GetPerformanceFrequency();
  return frequency != 0 ?
    (uint32)(ticks * 1000000ull / frequency) : 0;
}
#endif

void NORETURN Die(const char *error) {
#if defined(NDEBUG) && defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
#ifndef __3DS__
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}
#endif

static void DrawPpuFrameWithPerf() {
  int render_scale = PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags);
  uint8 *pixel_buffer = 0;
  int pitch = 0;
#ifdef __3DS__
  uint64 section_start;
  g_3ds_last_ppu_draw_us = 0;
  g_3ds_last_capture_us = 0;
  g_3ds_last_present_us = 0;
#endif

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
#ifdef __3DS__
  section_start = SDL_GetPerformanceCounter();
#endif
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
  }
#ifdef __3DS__
  g_3ds_last_ppu_draw_us =
    TicksToMicroseconds(SDL_GetPerformanceCounter() - section_start);
  section_start = SDL_GetPerformanceCounter();
#endif
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
  // the second screen's save-state picker grabs a thumbnail off this frame
  extern void SecondScreen_CaptureFrameHook(const uint8 *px, int pitch,
                                             int width, int height,
                                             bool rgb565);
  SecondScreen_CaptureFrameHook(pixel_buffer, pitch,
                                g_snes_width * render_scale,
                                g_snes_height * render_scale, false);
#ifdef __3DS__
  g_3ds_last_capture_us =
    TicksToMicroseconds(SDL_GetPerformanceCounter() - section_start);
  section_start = SDL_GetPerformanceCounter();
#endif
  g_renderer_funcs.EndDraw();
#ifdef __3DS__
  g_3ds_last_present_us =
    TicksToMicroseconds(SDL_GetPerformanceCounter() - section_start);
#endif
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;
#ifdef __3DS__
static uint8 *g_3ds_top_pixels;
enum {
  k3DSTopWidth = 400,
  k3DSTopHeight = 240,
  k3DSTopTextureWidth = 512,
  k3DSTopTextureHeight = 256,
};
#endif

// ---- CRT filter (scanlines + a subtle rgb mask), sdl renderer ----
// Two multiply overlays instead of one full screen one: a 1px wide column with
// the scanline profile, stretched sideways, and a 1px tall row with the
// phosphor triads, stretched down. Each maps 1:1 along the axis it varies on,
// so nothing shimmers when the game is scaled, and both are a few kb.
static SDL_Texture *g_crt_scan_tex, *g_crt_mask_tex;
static int g_crt_w, g_crt_h;
static float g_crt_line_h;

static const float kCrtScanlineDepth = 0.28f;  // how dark the gap between lines is
static const float kCrtMaskDepth = 0.12f;      // how much the mask dims the two other channels

static uint32 CrtColor(float r, float g, float b) {
  return 0xff000000 | (uint32)(r * 255.0f + 0.5f) << 16 |
         (uint32)(g * 255.0f + 0.5f) << 8 | (uint32)(b * 255.0f + 0.5f);
}

static SDL_Texture *CrtMakeTexture(int w, int h, const uint32 *pixels) {
  SDL_Texture *tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC, w, h);
  if (tex == NULL)
    return NULL;
  SDL_UpdateTexture(tex, NULL, pixels, w * 4);
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_MOD);
  SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
  return tex;
}

static void CrtFilter_Free() {
  if (g_crt_scan_tex) SDL_DestroyTexture(g_crt_scan_tex), g_crt_scan_tex = NULL;
  if (g_crt_mask_tex) SDL_DestroyTexture(g_crt_mask_tex), g_crt_mask_tex = NULL;
  g_crt_w = g_crt_h = 0;
}

// w/h are the size of the drawn area in real pixels, line_h how many of those
// rows one snes scanline covers.
static void CrtFilter_Build(int w, int h, float line_h) {
  CrtFilter_Free();
  if (w <= 0 || h <= 0 || line_h <= 0.0f)
    return;
  g_crt_w = w, g_crt_h = h, g_crt_line_h = line_h;

  // below two rows per scanline there's nothing to draw lines into, so fade out
  float depth = kCrtScanlineDepth * (line_h >= 2.0f ? 1.0f : line_h - 1.0f);
  if (depth < 0.0f)
    depth = 0.0f;
  uint32 *col = malloc(h * sizeof(uint32));
  for (int y = 0; y < h; y++) {
    float f = y / line_h;
    // brightest across the middle of a scanline, darkest at the seam
    float v = 1.0f - depth * (0.5f + 0.5f * cosf((f - floorf(f)) * 6.2831853f));
    col[y] = CrtColor(v, v, v);
  }
  g_crt_scan_tex = CrtMakeTexture(1, h, col);
  free(col);

  uint32 *row = malloc(w * sizeof(uint32));
  for (int x = 0; x < w; x++) {
    // aperture grille: every pixel keeps one channel and dims the other two
    float v[3] = { 1.0f - kCrtMaskDepth, 1.0f - kCrtMaskDepth, 1.0f - kCrtMaskDepth };
    v[x % 3] = 1.0f;
    row[x] = CrtColor(v[0], v[1], v[2]);
  }
  g_crt_mask_tex = CrtMakeTexture(w, 1, row);
  free(row);
}

static void CrtFilter_Draw() {
  SDL_Rect vp;
  float sx, sy;
  int lw, lh;
  SDL_RenderGetViewport(g_renderer, &vp);
  SDL_RenderGetScale(g_renderer, &sx, &sy);
  SDL_RenderGetLogicalSize(g_renderer, &lw, &lh);
  // the viewport is in logical units, which are snes pixels once the logical
  // size is set; without it they're real pixels and the scale is 1
  int w = (int)(vp.w * sx + 0.5f), h = (int)(vp.h * sy + 0.5f);
  float line_h = (float)h / g_snes_height;
  if (w != g_crt_w || h != g_crt_h || line_h != g_crt_line_h)
    CrtFilter_Build(w, h, line_h);
  if (!g_crt_scan_tex && !g_crt_mask_tex)
    return;
  // Both overlays hold one texel per real screen row/column, so they have to be
  // blitted 1:1. Under a logical size they'd first be squeezed into snes rows
  // and stretched back out, which turns the scanline profile into wide dark
  // bands, so this pass runs with logical scaling off.
  int outw, outh;
  SDL_GetRendererOutputSize(g_renderer, &outw, &outh);
  if (lw > 0 && lh > 0)
    SDL_RenderSetLogicalSize(g_renderer, 0, 0);
  SDL_Rect dst = { (outw - w) / 2, (outh - h) / 2, w, h };
  if (g_crt_scan_tex)
    SDL_RenderCopy(g_renderer, g_crt_scan_tex, NULL, &dst);
  if (g_crt_mask_tex)
    SDL_RenderCopy(g_renderer, g_crt_mask_tex, NULL, &dst);
  if (lw > 0 && lh > 0)
    SDL_RenderSetLogicalSize(g_renderer, lw, lh);
}

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

#ifdef __3DS__
  (void)window;
  size_t top_buffer_size =
    k3DSTopTextureWidth * k3DSTopTextureHeight * sizeof(uint32);
  g_3ds_top_pixels =
    linearMemAlign(top_buffer_size, 64);
  if (!g_3ds_top_pixels) {
    SDL_SetError("Unable to allocate native 3DS top framebuffer");
    return false;
  }
  memset(g_3ds_top_pixels, 0, top_buffer_size);
  if (!Platform3DS_InitTopPresenter()) {
    linearFree(g_3ds_top_pixels);
    g_3ds_top_pixels = NULL;
    SDL_SetError("Unable to initialize native 3DS top presenter");
    return false;
  }
  return true;
#else
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (Uint32 i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetTextureScaleMode(g_texture, SDL_ScaleModeNearest);
  return true;
#endif
}

static void SdlRenderer_Destroy() {
#ifdef __3DS__
  Platform3DS_ShutdownTopPresenter();
  linearFree(g_3ds_top_pixels);
  g_3ds_top_pixels = NULL;
#else
  CrtFilter_Free();
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
#endif
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
#ifdef __3DS__
  if (width > k3DSTopWidth || height > k3DSTopHeight) {
    *pixels = NULL;
    *pitch = 0;
    return;
  }
  *pixels = g_3ds_top_pixels;
  *pitch = k3DSTopTextureWidth * sizeof(uint32);
#else
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
#endif
}

static void SdlRenderer_EndDraw() {

#ifdef __3DS__
  int focus_x = -1;
  int focus_y = -1;
  if ((main_module_index == 7 || main_module_index == 9) &&
      submodule_index == 0) {
    focus_x = (int)(uint16)(link_x_coord - BG2HOFS_copy2) + 8;
    focus_y = (int)(uint16)(link_y_coord - BG2VOFS_copy2) + 12;
    if (g_sdl_renderer_rect.w > 256)
      focus_x += (g_sdl_renderer_rect.w - 256) / 2;
  }
  Platform3DS_PresentTopFrame(g_3ds_top_pixels,
                              k3DSTopTextureWidth * sizeof(uint32),
                              g_sdl_renderer_rect.w,
                              g_sdl_renderer_rect.h,
                              focus_x,
                              focus_y);
#else
//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  if (g_config.crt_filter)
    CrtFilter_Draw();
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
#endif
}

// The snes height of the frame being drawn; the ppu buffer can be a multiple
// of it with enhanced mode 7, and the crt filter wants the real scanlines.
int ZeldaGetSnesHeight(void) {
  return g_snes_height;
}

static void ZeldaApplyRendererSize(void) {
  if (g_renderer) {
    if (g_config.ignore_aspect_ratio)
      SDL_RenderSetLogicalSize(g_renderer, 0, 0);
    else
      SDL_RenderSetLogicalSize(g_renderer, g_snes_width, g_snes_height);
  }
  if (g_texture) {
    SDL_DestroyTexture(g_texture);
    int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  g_snes_width * tex_mult, g_snes_height * tex_mult);
    if (g_texture)
      SDL_SetTextureScaleMode(g_texture, SDL_ScaleModeNearest);
  }
}

// Toggle the extended aspect ratio while running (game thread).
void ZeldaSetWidescreen(bool enable) {
#ifdef __3DS__
  int extra = enable ? (g_snes_height * 5 / 3 - 256) / 2 : 0;
#else
  int extra = enable ? (g_snes_height * 16 / 9 - 256) / 2 : 0;
#endif
  g_config.extended_aspect_ratio = extra;
  g_zenv.ppu->extraLeftRight = UintMin(extra, kPpuExtraLeftRight);
  if (!enable)
    PpuSetExtraSideSpace(g_zenv.ppu, 0, 0, 0);
  g_snes_width = extra * 2 + 256;
  ZeldaApplyRendererSize();
}

#ifdef __3DS__
void ZeldaSet3DSDisplayMode(int mode) {
  enum Platform3DSDisplayMode display_mode = (enum Platform3DSDisplayMode)mode;
  bool wide = display_mode == kPlatform3DSDisplayUltraWideMod;
  int extra = wide ? 72 : 0;
  uint32 ws_features = kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;

  g_config.ignore_aspect_ratio = display_mode == kPlatform3DSDisplayStretch;
  g_config.extended_aspect_ratio = extra;
  g_config.features0 = wide ? (g_config.features0 | ws_features) :
                               (g_config.features0 & ~ws_features);
  g_wanted_zelda_features = g_config.features0;
  g_zenv.ppu->extraLeftRight = UintMin(extra, kPpuExtraLeftRight);
  if (!wide)
    PpuSetExtraSideSpace(g_zenv.ppu, 0, 0, 0);
  g_snes_width = extra * 2 + 256;
// Check if we're in wide mode, and if so enable extended-vertical rendering on the PPU emulator, and set window to 240 pixels high
  g_config.extend_y = wide;
  g_snes_height = wide ? 240 : 224;
  if (wide)
    g_ppu_render_flags |= kPpuRenderFlags_Height240;
  else
    g_ppu_render_flags &= ~kPpuRenderFlags_Height240;
  ZeldaApplyRendererSize();
  Platform3DS_SetDisplayMode(display_mode);
}
#endif

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

//#undef main
int main(int argc, char** argv) {
  argc--, argv++;
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  }
#ifdef __3DS__
restart_3ds_runtime:
  if (!config_file) {
    if (!Platform3DS_PrepareStorage())
      return 0;
  }
#else
  if (!config_file) {
    SwitchDirectory();
  }
#endif

  ParseConfigFile(config_file);
#ifdef __3DS__
  Platform3DS_ApplyConfig(&g_config);
  Platform3DS_LogRuntime("Configuration loaded: %dx%d, software renderer",
                         g_config.window_width, g_config.window_height);
#endif
  LoadAssets();
#ifdef __3DS__
  Platform3DS_LogRuntime("Assets loaded and validated");
#endif
  LoadLinkGraphics();

  ZeldaInitialize();
#ifdef __3DS__
  ZeldaSetWidescreenEdgeMode(Platform3DS_GetWideEdgeMode());
  Platform3DS_LogRuntime("Game engine initialized");
#endif
  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;

  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
                       g_config.extend_y * kPpuRenderFlags_Height240 |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaSetLanguage(g_config.language);

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR SDL_Init: %s", SDL_GetError());
#endif
    return 1;
  }
#ifdef __3DS__
  Platform3DS_LogRuntime("SDL initialized");
#endif

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

#ifndef __3DS__
  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }
#else
  g_renderer_funcs = kSdlRendererFuncs;
#endif

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR top window: %s", SDL_GetError());
#endif
    return 1;
  }
#ifdef __3DS__
  Platform3DS_LogRuntime("Top window created");
#endif
  g_window = window;
#ifndef __3DS__
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);
#endif

  if (!g_renderer_funcs.Initialize(window)) {
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR top renderer: %s", SDL_GetError());
#endif
    return 1;
  }
#ifdef __3DS__
  Platform3DS_LogRuntime("Top renderer initialized");
#endif

  if (!SecondScreenSDL_Init(window)) {
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR second screen initialization: %s", SDL_GetError());
#endif
  }

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec want = { 0 }, have;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  if (g_config.enable_audio) {
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = g_config.audio_channels;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
#ifdef __3DS__
      Platform3DS_LogRuntime("ERROR audio device: %s", SDL_GetError());
#endif
      return 1;
    }
    g_audio_channels = have.channels;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = malloc(g_frames_per_block * have.channels * sizeof(int16));
#ifdef __3DS__
    Platform3DS_LogRuntime("Audio initialized: %d Hz, %d channels, %d samples",
                           have.freq, have.channels, have.samples);
#endif
  }

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  ZeldaReadSram();
#ifdef __3DS__
  Platform3DS_LogRuntime("Entering main loop");
#endif

#ifdef __3DS__
  Platform3DS_LogRuntime("Native HID input enabled");
#else
  int joystick_count = SDL_NumJoysticks();
  for (int i = 0; i < joystick_count; i++)
    OpenOneGamepad(i);
#endif

  bool running = true;
  SDL_Event event;
#ifndef __3DS__
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
#else
  const uint64 logic_frequency = SDL_GetPerformanceFrequency();
  uint64 logic_last_counter = SDL_GetPerformanceCounter();
  uint64 logic_accumulator = logic_frequency;
  uint64 last_render_counter = 0;
#endif
  uint32 frameCtr = 0;
  bool audiopaused = true;
#ifdef __3DS__
  bool system_exit_requested = false;
#endif

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  while(running) {
#ifdef __3DS__
    if (Platform3DS_ShouldExit()) {
      system_exit_requested = true;
      running = false;
      break;
    }
#endif
    while(SDL_PollEvent(&event)) {
      if (SecondScreenSDL_HandleEvent(&event))
        continue;
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
#ifndef __3DS__
        OpenOneGamepad(event.cdevice.which);
#endif
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInput(b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
#ifdef __3DS__
        system_exit_requested = true;
#endif
        running = false;
        break;
      }
    }

    if (!running)
      break;

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (device)
        SDL_PauseAudioDevice(device, audiopaused);
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    int inputs;
    bool is_replay = false;
    int rendered_logic_frames = 1;
#ifdef __3DS__
    uint64 frame_work_start = SDL_GetPerformanceCounter();
    uint64 elapsed_ticks = frame_work_start - logic_last_counter;
    logic_last_counter = frame_work_start;
    if (elapsed_ticks > logic_frequency / 4) {
      logic_accumulator = logic_frequency;
      last_render_counter = 0;
    } else {
      logic_accumulator += elapsed_ticks * 60;
    }

    int scheduled_logic_frames = (int)(logic_accumulator / logic_frequency);
    if (scheduled_logic_frames == 0) {
      uint64 remaining_ticks =
        (logic_frequency - logic_accumulator + 59) / 60;
      uint64 remaining_ns =
        remaining_ticks * 1000000000ull / logic_frequency;
      if (remaining_ns > 300000)
        svcSleepThread((int64)(remaining_ns - 200000));
      else
        svcSleepThread(0);
      continue;
    }
    if (scheduled_logic_frames > 5) {
      scheduled_logic_frames = 5;
      logic_accumulator = 0;
    } else {
      logic_accumulator -= (uint64)scheduled_logic_frames * logic_frequency;
    }

    uint32 render_interval_us = 0;
    if (last_render_counter != 0) {
      render_interval_us = (uint32)(
        (frame_work_start - last_render_counter) * 1000000ull /
        logic_frequency);
      uint32 now_ms = SDL_GetTicks();
      if (g_3ds_visual_fps_window_start_ms == 0)
        g_3ds_visual_fps_window_start_ms = now_ms;
      g_3ds_visual_fps_window_frames++;
      uint32 elapsed_ms = now_ms - g_3ds_visual_fps_window_start_ms;
      if (elapsed_ms >= 500) {
        g_3ds_current_fps =
          (int)((uint64)g_3ds_visual_fps_window_frames * 1000ull /
                (elapsed_ms ? elapsed_ms : 1));
        g_3ds_average_fps_samples[g_3ds_average_fps_sample_pos] =
          (uint32)g_3ds_current_fps;
        g_3ds_average_fps_sample_pos =
          (g_3ds_average_fps_sample_pos + 1) %
          countof(g_3ds_average_fps_samples);
        if (g_3ds_average_fps_sample_count <
            countof(g_3ds_average_fps_samples))
          g_3ds_average_fps_sample_count++;
        uint32 fps_sum = 0;
        for (uint32 i = 0; i < g_3ds_average_fps_sample_count; i++)
          fps_sum += g_3ds_average_fps_samples[i];
        g_3ds_average_fps = g_3ds_average_fps_sample_count ?
          (int)(fps_sum / g_3ds_average_fps_sample_count) :
          g_3ds_current_fps;
        g_3ds_visual_fps_window_start_ms = now_ms;
        g_3ds_visual_fps_window_frames = 0;
      }
    }
    last_render_counter = frame_work_start;

    bool turbo_held;
    int turbo_multiplier;
    inputs = Platform3DS_ReadInput(&turbo_held, &turbo_multiplier);
    SecondScreenSDL_SetDiagnostics(g_3ds_current_fps, g_3ds_average_fps);
    SecondScreenSDL_Handle3DSTouch();
    if (Platform3DS_TakeQuickDumpRequest())
      SecondScreenSDL_RequestDump();
    g_turbo = turbo_held;
    int frames_to_run = scheduled_logic_frames *
                        (g_turbo ? turbo_multiplier : 1);
    if (frames_to_run < 1)
      frames_to_run = 1;
    if (frames_to_run > 20)
      frames_to_run = 20;
    rendered_logic_frames = frames_to_run;
    for (int i = 0; i < frames_to_run; i++) {
      extern void SecondScreen_RunFrameHook(void);
      SecondScreen_RunFrameHook();

      SDL_LockMutex(g_audio_mutex);
      is_replay |= ZeldaRunFrame(inputs);
      SDL_UnlockMutex(g_audio_mutex);
      frameCtr++;
    }
    uint64 logic_work_ticks = SDL_GetPerformanceCounter() - frame_work_start;
#else
    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;

    extern void SecondScreen_RunFrameHook(void);
    SecondScreen_RunFrameHook();

    SDL_LockMutex(g_audio_mutex);
    is_replay = ZeldaRunFrame(inputs);
    SDL_UnlockMutex(g_audio_mutex);

    frameCtr++;

    if ((g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0) {
      continue;
    }
#endif

#ifdef __3DS__
    SecondScreenSDL_BeginFrame(rendered_logic_frames);
    uint64 top_draw_start = SDL_GetPerformanceCounter();
#endif
    DrawPpuFrameWithPerf();
#ifdef __3DS__
    uint64 top_draw_ticks = SDL_GetPerformanceCounter() - top_draw_start;
    uint64 top_work_ticks = SDL_GetPerformanceCounter() - frame_work_start;
    uint64 bottom_work_start = SDL_GetPerformanceCounter();
#endif
    SecondScreenSDL_Update(rendered_logic_frames);

#ifdef __3DS__
    Platform3DS_EndFrame();
    uint64 bottom_work_ticks =
      SDL_GetPerformanceCounter() - bottom_work_start;
    uint64 total_work_ticks = SDL_GetPerformanceCounter() - frame_work_start;
    uint64 performance_frequency = SDL_GetPerformanceFrequency();
    uint32 logic_work_us = performance_frequency != 0 ?
      (uint32)(logic_work_ticks * 1000000ull / performance_frequency) : 0;
    uint32 top_draw_us = performance_frequency != 0 ?
      (uint32)(top_draw_ticks * 1000000ull / performance_frequency) : 0;
    uint32 top_work_us = performance_frequency != 0 ?
      (uint32)(top_work_ticks * 1000000ull / performance_frequency) : 0;
    uint32 bottom_work_us = performance_frequency != 0 ?
      (uint32)(bottom_work_ticks * 1000000ull / performance_frequency) : 0;
    uint32 total_work_us = performance_frequency != 0 ?
      (uint32)(total_work_ticks * 1000000ull / performance_frequency) : 0;
    Platform3DS_RecordFrameTiming(logic_work_us, top_draw_us,
                                  g_3ds_last_ppu_draw_us,
                                  g_3ds_last_capture_us,
                                  g_3ds_last_present_us,
                                  top_work_us, bottom_work_us, total_work_us,
                                  render_interval_us,
                                  scheduled_logic_frames,
                                  frames_to_run);
#else

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
#endif
  }
  if (g_config.autosave) {
#ifdef __3DS__
    if (system_exit_requested && device)
      SDL_PauseAudioDevice(device, 1);
#endif
    HandleCommand(kKeys_Save + 0, true);
  }

  // clean sdl
  if (g_config.enable_audio) {
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
  }

  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  ZeldaShutdownPpuWorker();
  g_renderer_funcs.Destroy();

  SecondScreenSDL_Shutdown();
  SDL_DestroyWindow(window);
  SDL_Quit();
#ifdef __3DS__
  if (Platform3DS_TakeRomSelectionRequest())
    goto restart_3ds_runtime;
#endif
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      ZeldaReset(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

#ifndef __3DS__
static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}
#endif

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  extern volatile int g_ss_capture_button;
  if (g_ss_capture_button == -2) {
    if (pressed)
      g_ss_capture_button = button;
    return;
  }
  if (!!(g_gamepad_modifiers & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers ^= 1 << button;
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0)
    HandleCommand(g_gamepad_last_cmd[button], pressed);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 x_bits, y_bits;
  memcpy(&x_bits, &x, sizeof(x_bits));
  memcpy(&y_bits, &y, sizeof(y_bits));
  uint32 ux_s = sign_mask & x_bits;
  uint32 uy_s = sign_mask & y_bits;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t atan_bits;
  memcpy(&atan_bits, &atan_1q, sizeof(atan_bits));
  uint32_t uatan_2q = (ux_s ^ uy_s) | atan_bits;
  float atan_2q;
  memcpy(&atan_2q, &uatan_2q, sizeof(atan_2q));
  return q + atan_2q;
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_x, last_y;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id != gamepad_id) {
      if (value > -16000 && value < 16000)
        return;
      last_gamepad_id = gamepad_id;
      last_x = last_y = 0;
    }
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x : &last_y) = value;
    int buttons = 0;
    if (last_x * last_x + last_y * last_y >= 10000 * 10000) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(last_y, last_x) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    g_gamepad_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

static bool LoadRom(const char *filename) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if(!file) Die("Failed to read file");
  bool result = EmuInitialize(file, length);
  free(file);
  return result;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124)
    memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

// Patch (or unpatch) the dungeon floor palettes for the DimFlashes feature.
// Also called from the second screen when the toggle changes at runtime;
// a change shows the next time a dungeon loads its palette.
void ZeldaApplyDimFlashesPalette(bool enable) {
  static uint16 orig[3];
  static bool saved;
  if (!saved) {
    saved = true;
    orig[0] = kPalette_DungBgMain[0x484];
    orig[1] = kPalette_DungBgMain[0x485];
    orig[2] = kPalette_DungBgMain[0x486];
  }
  kPalette_DungBgMain[0x484] = enable ? 0x70 : orig[0];
  kPalette_DungBgMain[0x485] = enable ? 0x95 : orig[1];
  kPalette_DungBgMain[0x486] = enable ? 0x57 : orig[2];
}

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Failed to read zelda3_assets.dat. Please see the README for information about how you get this file.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  ZeldaApplyDimFlashesPalette((g_config.features0 & kFeatures0_DimFlashes) != 0);
}

// Go some steps up and find zelda3.ini
#ifndef __3DS__
static void SwitchDirectory() {
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}
#endif

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
