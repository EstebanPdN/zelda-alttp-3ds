#pragma once
#include <stdbool.h>

// Explicit, immutable device policies. Old experiments never opt New 3DS in.
typedef struct Platform3DSHardwareProfile {
  const char *name;
  bool old_ppu;
  bool live_palette_upload;
  bool rgb565_ui_textures;
  bool integer_ui_rounding;
} Platform3DSHardwareProfile;

static inline const Platform3DSHardwareProfile *Platform3DS_ProfileForModel(bool is_new) {
  static const Platform3DSHardwareProfile old_profile = {
    "Old 3DS E8", true, true, true, true
  };
  static const Platform3DSHardwareProfile new_profile = {
    "New 3DS E6 renderer", false, false, false, false
  };
  return is_new ? &new_profile : &old_profile;
}
