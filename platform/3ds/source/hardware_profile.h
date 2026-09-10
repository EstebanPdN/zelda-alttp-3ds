#pragma once
#include <stdbool.h>

// Old performance experiments never opt New 3DS in. Both models share
// integer UI rounding and palette invalidation on NMI uploads.
typedef struct Platform3DSHardwareProfile {
  const char *name;
  bool old_ppu;
  bool live_palette_upload;
  bool rgb565_ui_textures;
  bool integer_ui_rounding;
} Platform3DSHardwareProfile;

static inline const Platform3DSHardwareProfile *Platform3DS_ProfileForModel(bool is_new) {
  static const Platform3DSHardwareProfile old_profile = {
    "Old 3DS E16", true, true, true, true
  };
  static const Platform3DSHardwareProfile new_profile = {
    "New 3DS E6 renderer", false, true, false, true
  };
  return is_new ? &new_profile : &old_profile;
}
