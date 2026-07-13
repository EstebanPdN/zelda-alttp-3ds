// Android JNI bridge: thin wrappers exposing the SS_* core (second_screen.c)
// to com.dishii.zelda3.GameState. Compiled only in the Android build.
#include "../../types.h"
#include "../../second_screen_tables.h"

// SS_* core (implemented in second_screen.c)
int SS_GetLinkX(void); int SS_GetLinkY(void); int SS_GetArea(void); int SS_GetModule(void);
bool SS_IsIndoors(void); void SS_ReadSram(uint8 *out, int n); int SS_GetEquippedSlot(void);
int SS_GetDungeon(void); void SS_ReadDungFlags(uint8 *out, int n);
bool SS_RenderIconSheet(uint32 *px); bool SS_RenderGlyphSheet(uint32 *px);
bool SS_RenderLetterSheet(uint32 *px); bool SS_RenderWorldMap(uint32 *px, bool dark);
bool SS_RenderLinkFace(uint32 *px, int chunk); int SS_GetDungeonLayout(int palace, uint8 *out, int cap);
bool SS_RenderDungeonFloor(int palace, int floorIdx, uint32 *px); bool SS_RenderMapIcons(int palace, uint32 *px);
void SS_EquipSlot(int slot); void SS_SetWidescreen(bool on); bool SS_IsWidescreen(void);
void SS_SetHudHidden(bool hide); bool SS_IsHudHidden(void); void SS_ArmButtonCapture(bool arm);
int SS_GetCapturedButton(void); void SS_GetGamepadControls(int *out); void SS_SetGamepadControls(const int *in);
int SS_GetEquippedSlotX(void); void SS_AssignSlotX(int slot);
uint32 SS_GetFeatures(void); void SS_SetFeature(unsigned mask, bool on);

#include <jni.h>

static uint32 g_jni_px[512 * 512];  // scratch for the render wrappers

JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getLinkX(JNIEnv *env, jclass clazz) { return SS_GetLinkX(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getLinkY(JNIEnv *env, jclass clazz) { return SS_GetLinkY(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getArea(JNIEnv *env, jclass clazz) { return SS_GetArea(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getModule(JNIEnv *env, jclass clazz) { return SS_GetModule(); }
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_isIndoors(JNIEnv *env, jclass clazz) { return SS_IsIndoors(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getEquippedSlot(JNIEnv *env, jclass clazz) { return SS_GetEquippedSlot(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getEquippedSlotX(JNIEnv *env, jclass clazz) { return SS_GetEquippedSlotX(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getDungeon(JNIEnv *env, jclass clazz) { return SS_GetDungeon(); }

JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_readSram(JNIEnv *env, jclass clazz, jbyteArray out) {
  uint8 tmp[0x100];
  jsize n = (*env)->GetArrayLength(env, out);
  if (n > 0x100) n = 0x100;
  SS_ReadSram(tmp, n);
  (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)tmp);
}

JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_readDungFlags(JNIEnv *env, jclass clazz, jbyteArray out) {
  uint8 tmp[0x500];
  jsize n = (*env)->GetArrayLength(env, out);
  if (n > 0x500) n = 0x500;
  SS_ReadDungFlags(tmp, n);
  (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)tmp);
}

static jboolean JniCopyPixels(JNIEnv *env, jintArray out, int count, bool ok) {
  if (!ok || (*env)->GetArrayLength(env, out) < count) return false;
  (*env)->SetIntArrayRegion(env, out, 0, count, (const jint *)g_jni_px);
  return true;
}

JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderIconSheet(JNIEnv *env, jclass clazz, jintArray out) {
  int w = kIconCols * 16, h = ((kIconCount + kIconCols - 1) / kIconCols) * 16;
  if ((*env)->GetArrayLength(env, out) < w * h) return false;
  return JniCopyPixels(env, out, w * h, SS_RenderIconSheet(g_jni_px));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderGlyphSheet(JNIEnv *env, jclass clazz, jintArray out) {
  int w = kGlyphCols * 8, h = ((kGlyphCount + kGlyphCols - 1) / kGlyphCols) * 8;
  if ((*env)->GetArrayLength(env, out) < w * h) return false;
  return JniCopyPixels(env, out, w * h, SS_RenderGlyphSheet(g_jni_px));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderLetterSheet(JNIEnv *env, jclass clazz, jintArray out) {
  if ((*env)->GetArrayLength(env, out) < 128 * 16) return false;
  return JniCopyPixels(env, out, 128 * 16, SS_RenderLetterSheet(g_jni_px));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderWorldMap(JNIEnv *env, jclass clazz, jintArray out, jboolean dark) {
  if ((*env)->GetArrayLength(env, out) < 512 * 512) return false;
  return JniCopyPixels(env, out, 512 * 512, SS_RenderWorldMap(g_jni_px, dark));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderLinkFace(JNIEnv *env, jclass clazz, jintArray out, jint chunk) {
  if ((*env)->GetArrayLength(env, out) < 16 * 16) return false;
  return JniCopyPixels(env, out, 16 * 16, SS_RenderLinkFace(g_jni_px, chunk));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderDungeonFloor(JNIEnv *env, jclass clazz, jint palace, jint floorIdx, jintArray out) {
  if ((*env)->GetArrayLength(env, out) < 80 * 80) return false;
  return JniCopyPixels(env, out, 80 * 80, SS_RenderDungeonFloor(palace, floorIdx, g_jni_px));
}
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderMapIcons(JNIEnv *env, jclass clazz, jint palace, jintArray out) {
  if ((*env)->GetArrayLength(env, out) < 32 * 8) return false;
  return JniCopyPixels(env, out, 32 * 8, SS_RenderMapIcons(palace, g_jni_px));
}

JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getDungeonLayout(JNIEnv *env, jclass clazz, jint palace, jbyteArray out) {
  uint8 tmp[16 * 25];
  jsize n = (*env)->GetArrayLength(env, out);
  if (n > (jsize)sizeof(tmp)) n = sizeof(tmp);
  int r = SS_GetDungeonLayout(palace, tmp, n);
  if (r >= 0)
    (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)tmp);
  return r;
}

JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_equipSlot(JNIEnv *env, jclass clazz, jint slot) { SS_EquipSlot(slot); }
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_assignSlotX(JNIEnv *env, jclass clazz, jint slot) { SS_AssignSlotX(slot); }
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_setWidescreen(JNIEnv *env, jclass clazz, jboolean on) { SS_SetWidescreen(on); }
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_isWidescreen(JNIEnv *env, jclass clazz) { return SS_IsWidescreen(); }
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_setHudHidden(JNIEnv *env, jclass clazz, jboolean hide) { SS_SetHudHidden(hide); }
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_isHudHidden(JNIEnv *env, jclass clazz) { return SS_IsHudHidden(); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getFeatures(JNIEnv *env, jclass clazz) { return (jint)SS_GetFeatures(); }
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_setFeature(JNIEnv *env, jclass clazz, jint mask, jboolean on) { SS_SetFeature((unsigned)mask, on); }
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_armButtonCapture(JNIEnv *env, jclass clazz, jboolean arm) { SS_ArmButtonCapture(arm); }
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getCapturedButton(JNIEnv *env, jclass clazz) { return SS_GetCapturedButton(); }

JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_getGamepadControls(JNIEnv *env, jclass clazz, jintArray out) {
  if ((*env)->GetArrayLength(env, out) < 12) return;
  int tmp[12];
  SS_GetGamepadControls(tmp);
  (*env)->SetIntArrayRegion(env, out, 0, 12, (const jint *)tmp);
}
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_setGamepadControls(JNIEnv *env, jclass clazz, jintArray in) {
  if ((*env)->GetArrayLength(env, in) < 12) return;
  jint buf[12];
  (*env)->GetIntArrayRegion(env, in, 0, 12, buf);
  int tmp[12];
  for (int i = 0; i < 12; i++) tmp[i] = buf[i];
  SS_SetGamepadControls(tmp);
}
