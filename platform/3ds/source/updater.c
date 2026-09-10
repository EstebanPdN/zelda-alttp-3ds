#include "updater.h"
#include "platform_3ds.h"
#include <3ds.h>
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UPDATE_DIR "sdmc:/3ds/Zelda 3DS/update"
#define UPDATE_PART UPDATE_DIR "/download.part"
#define CHANNEL_FILE UPDATE_DIR "/channel.txt"
#define MANIFEST_LIMIT (2u * 1024u * 1024u)
static LightLock lock;
static UpdateStatus status;
static UpdateRelease release;
static Thread worker;
static bool initialized, homebrew, busy, cancel, download_job;
static char launch_file[768];

static bool cancelled(void) { return __atomic_load_n(&cancel, __ATOMIC_ACQUIRE) || aptShouldClose(); }
static void publish(UpdateState state, const char *message) {
  LightLock_Lock(&lock);
  status.state = state; status.progress = 0; status.revision++;
  snprintf(status.message, sizeof(status.message), "%s", message ? message : "");
  LightLock_Unlock(&lock);
}
void Updater_GetStatus(UpdateStatus *out) {
  if (!initialized) { memset(out, 0, sizeof(*out)); return; }
  LightLock_Lock(&lock); *out = status; LightLock_Unlock(&lock);
}
static void progress(unsigned percent) {
  if (percent > 100) percent = 100;
  LightLock_Lock(&lock);
  if (status.progress != percent) { status.progress = percent; status.revision++; }
  LightLock_Unlock(&lock);
}
bool Updater_Busy(void) { return __atomic_load_n(&busy, __ATOMIC_ACQUIRE); }
bool Updater_ShouldClose(void) { UpdateStatus s; Updater_GetStatus(&s); return s.state == UPDATE_DONE; }
void Updater_Cancel(void) { __atomic_store_n(&cancel, true, __ATOMIC_RELEASE); }

typedef struct Transfer { char *data; size_t size, capacity; FILE *file; uint32_t expected; } Transfer;
static size_t receive(void *ptr, size_t a, size_t b, void *userdata) {
  Transfer *t = userdata;
  if (a && b > SIZE_MAX / a) return 0;
  size_t n = a * b, limit = t->file ? t->expected : MANIFEST_LIMIT;
  if (cancelled() || n > limit - t->size) return 0;
  if (t->file) {
    if (fwrite(ptr, 1, n, t->file) != n) return 0;
    progress((unsigned)((t->size + n) * 100ull / t->expected));
  } else {
    if (t->size + n + 1 > t->capacity) {
      size_t cap = t->size + n + 4096;
      char *p = realloc(t->data, cap); if (!p) return 0;
      t->data = p; t->capacity = cap;
    }
    memcpy(t->data + t->size, ptr, n); t->data[t->size + n] = 0;
  }
  t->size += n; return n;
}
static int transfer_progress(void *p, curl_off_t total, curl_off_t now, curl_off_t up, curl_off_t sent) {
  return cancelled() ? 1 : 0;
}
static bool fetch(const char *url, Transfer *t) {
  CURL *c = curl_easy_init(); if (!c) return false;
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "Zelda-ALttP-3DS/" ZELDA3_3DS_VERSION);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/update-ca.pem");
  curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 12L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, t->file ? 300L : 25L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 128L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 16384L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, t);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, transfer_progress);
  CURLcode code = curl_easy_perform(c);
  long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(c);
  if (code != CURLE_OK || http != 200) {
    Platform3DS_LogRuntime("Updater transfer failed: curl=%d http=%ld", code, http);
    return false;
  }
  return true;
}
static bool enough_space(uint64_t required) {
  FS_ArchiveResource r;
  return R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r)) &&
    (uint64_t)r.freeClusters * r.clusterSize >= required;
}
static bool verify_file(void) {
  FILE *f = fopen(UPDATE_PART, "rb"); if (!f) return false;
  mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  unsigned char data[16384], digest[32]; size_t n, total = 0;
  while (ok && (n = fread(data, 1, sizeof(data), f)) != 0) {
    total += n; ok = !cancelled() && mbedtls_sha256_update_ret(&sha, data, n) == 0;
  }
  ok = ok && !ferror(f) && total == release.size && mbedtls_sha256_finish_ret(&sha, digest) == 0;
  fclose(f); mbedtls_sha256_free(&sha);
  char hex[65]; for (int i = 0; i < 32 && ok; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  return ok && !strcmp(hex, release.sha256);
}
static bool install_cia(void) {
  Result rc = amInit(); if (R_FAILED(rc)) return false;
  Handle input = 0, output = 0; bool started = false, ok = false;
  AM_TitleInfo info; u64 required = 0;
  rc = FSUSER_OpenFileDirectly(&input, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
    fsMakePath(PATH_ASCII, "/3ds/Zelda 3DS/update/download.part"), FS_OPEN_READ, 0);
  if (R_FAILED(rc)) goto done;
  rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, input);
  if (R_FAILED(rc) || info.titleID != 0x0004000005a13e00ull) goto done;
  rc = AM_GetCiaRequiredSpace(&required, MEDIATYPE_SD, input);
  if (R_FAILED(rc) || !enough_space(required + 4 * 1024 * 1024ull) || cancelled()) goto done;
  // Overwrite through AM; never delete the installed title first.
  rc = AM_StartCiaInstallOverwrite(&output, MEDIATYPE_SD);
  if (R_FAILED(rc)) goto done;
  started = true;
  unsigned char data[16384]; u64 offset = 0;
  while (offset < release.size) {
    u32 read = 0, wrote = 0, want = release.size - offset;
    if (want > sizeof(data)) want = sizeof(data);
    if (cancelled()) goto done;
    rc = FSFILE_Read(input, &read, offset, data, want);
    if (R_FAILED(rc) || read != want) goto done;
    rc = FSFILE_Write(output, &wrote, offset, data, read, 0);
    if (R_FAILED(rc) || wrote != read) goto done;
    offset += read; progress((unsigned)(offset * 100 / release.size));
  }
  rc = AM_FinishCiaInstall(output); started = false;
  ok = R_SUCCEEDED(rc);
done:
  if (started) AM_CancelCIAInstall(output);
  if (input) FSFILE_Close(input);
  Platform3DS_LogRuntime("Updater CIA installation: result=%08lx success=%d", (unsigned long)rc, ok);
  amExit(); return ok;
}
static bool install_3dsx(void) {
  if (!launch_file[0]) return false;
  unsigned char magic[4]; FILE *f = fopen(UPDATE_PART, "rb");
  if (!f) return false;
  bool ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "3DSX", 4); fclose(f);
  if (!ok || cancelled()) return false;
  char backup[800]; snprintf(backup, sizeof(backup), "%s.update-backup", launch_file);
  struct stat st;
  if (stat(backup, &st) == 0 || rename(launch_file, backup) != 0) return false;
  if (rename(UPDATE_PART, launch_file) != 0) { rename(backup, launch_file); return false; }
  remove(backup); return true;
}
static void run_job(void *arg) {
  void *soc_buffer = NULL; bool soc_ready = false, curl_ready = false, ac_ready = false;
  bool ok = false; Transfer t = {0};
  UpdateStatus s; Updater_GetStatus(&s);
  if (R_FAILED(acInit())) goto done;
  ac_ready = true; u32 wifi = 0;
  if (R_FAILED(ACU_GetWifiStatus(&wifi)) || !wifi) { publish(UPDATE_ERROR, "NO WI-FI CONNECTION"); goto done; }
  soc_buffer = memalign(4096, 1024 * 1024);
  if (!soc_buffer || R_FAILED(socInit(soc_buffer, 1024 * 1024))) goto done;
  soc_ready = true;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) goto done;
  curl_ready = true;
  if (!download_job) {
    const char *url = s.prerelease ?
      "https://api.github.com/repos/" UPDATE_REPOSITORY "/releases?per_page=100" :
      "https://api.github.com/repos/" UPDATE_REPOSITORY "/releases/latest";
    if (!fetch(url, &t)) goto done;
    UpdateRelease candidate;
    int result = Update_ParseRelease(t.data, t.size, s.prerelease, homebrew, &candidate);
    if (result < 0) { publish(UPDATE_ERROR, "INVALID RELEASE DATA"); goto done; }
    if (!result) { publish(UPDATE_EMPTY, "NO PRE-RELEASE AVAILABLE"); ok = true; goto done; }
    LightLock_Lock(&lock); release = candidate; strcpy(status.version, release.version); LightLock_Unlock(&lock);
    bool newer = Update_IsNewer(release.version, ZELDA3_3DS_VERSION);
    publish(newer ? UPDATE_AVAILABLE : UPDATE_CURRENT, newer ? "UPDATE AVAILABLE" : "YOU ARE UP TO DATE");
    ok = true;
  } else {
    if (!Update_AllowedDownloadUrl(release.url) || !enough_space(release.size * 2ull + 16 * 1024 * 1024)) {
      publish(UPDATE_ERROR, "NOT ENOUGH SD SPACE"); goto done;
    }
    if (homebrew && !launch_file[0]) { publish(UPDATE_ERROR, "3DSX LAUNCH PATH UNKNOWN"); goto done; }
    t.expected = release.size; t.file = fopen(UPDATE_PART, "wb");
    if (!t.file) { publish(UPDATE_ERROR, "CANNOT WRITE TO SD CARD"); goto done; }
    bool fetched = fetch(release.url, &t);
    int closed = fclose(t.file); t.file = NULL;
    if (!fetched || closed || t.size != release.size) goto done;
    publish(UPDATE_VERIFYING, "VERIFYING DOWNLOAD");
    if (!verify_file()) { publish(UPDATE_ERROR, "DOWNLOAD CHECK FAILED"); goto done; }
    if (cancelled() || !aptIsActive()) goto done;
    bool home_allowed = aptIsHomeAllowed(), sleep_allowed = aptIsSleepAllowed();
    aptSetHomeAllowed(false); aptSetSleepAllowed(false);
    publish(UPDATE_INSTALLING, "INSTALLING UPDATE");
    ok = homebrew ? install_3dsx() : install_cia();
    aptSetSleepAllowed(sleep_allowed); aptSetHomeAllowed(home_allowed);
    publish(ok ? UPDATE_DONE : UPDATE_ERROR, ok ? "UPDATE INSTALLED" : "INSTALLATION FAILED");
  }
done:
  if (t.file) fclose(t.file);
  free(t.data);
  if (download_job) remove(UPDATE_PART);
  if (curl_ready) curl_global_cleanup();
  if (soc_ready) socExit();
  free(soc_buffer);
  if (ac_ready) acExit();
  Updater_GetStatus(&s);
  if (!ok && s.state != UPDATE_ERROR)
    publish(UPDATE_ERROR, cancelled() ? "UPDATE CANCELLED" : "CONNECTION FAILED - RETRY");
  __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
}
static void start(bool downloading) {
  if (!initialized || Updater_Busy()) return;
  if (worker) { threadJoin(worker, UINT64_MAX); threadFree(worker); worker = NULL; }
  if (downloading) { UpdateStatus s; Updater_GetStatus(&s); if (s.state != UPDATE_AVAILABLE) return; }
  if (!downloading) { LightLock_Lock(&lock); status.version[0] = 0; LightLock_Unlock(&lock); }
  download_job = downloading;
  __atomic_store_n(&cancel, false, __ATOMIC_RELEASE);
  __atomic_store_n(&busy, true, __ATOMIC_RELEASE);
  publish(downloading ? UPDATE_DOWNLOADING : UPDATE_CHECKING,
          downloading ? "DOWNLOADING UPDATE" : "CHECKING FOR UPDATES");
  // Background priority on Core 0; the main thread never waits for HTTP.
  worker = threadCreate(run_job, NULL, 96 * 1024, 0x31, 0, false);
  if (!worker) { __atomic_store_n(&busy, false, __ATOMIC_RELEASE); publish(UPDATE_ERROR, "CANNOT START UPDATE"); }
}
void Updater_Check(void) { start(false); }
void Updater_Download(void) { start(true); }
void Updater_SetChannel(bool pre) {
  if (!initialized || Updater_Busy()) return;
  const char *temp = CHANNEL_FILE ".tmp", *backup = CHANNEL_FILE ".bak";
  FILE *f = fopen(temp, "wb"); bool saved = false;
  if (f) { saved = fprintf(f, "%d\n", pre ? 1 : 0) > 0; if (fclose(f)) saved = false; }
  struct stat st; bool had_old = stat(CHANNEL_FILE, &st) == 0;
  if (saved && had_old) { remove(backup); saved = rename(CHANNEL_FILE, backup) == 0; }
  if (saved && rename(temp, CHANNEL_FILE) != 0) {
    if (had_old) rename(backup, CHANNEL_FILE);
    saved = false;
  }
  if (!saved) { remove(temp); publish(UPDATE_ERROR, "CANNOT SAVE UPDATE CHANNEL"); return; }
  remove(backup);
  LightLock_Lock(&lock); status.prerelease = pre; status.version[0] = 0; LightLock_Unlock(&lock);
  Updater_Check();
}
void Updater_Init(const char *path) {
  if (initialized) return;
  LightLock_Init(&lock); initialized = true; homebrew = envIsHomebrew();
  mkdir(UPDATE_DIR, 0777);
  if (homebrew && path && !strncmp(path, "sdmc:/", 6) && strlen(path) < sizeof(launch_file) &&
      strlen(path) > 5 && !strcmp(path + strlen(path) - 5, ".3dsx")) strcpy(launch_file, path);
  struct stat st;
  if (stat(CHANNEL_FILE, &st) != 0) rename(CHANNEL_FILE ".bak", CHANNEL_FILE);
  FILE *f = fopen(CHANNEL_FILE, "rb"); if (f) { status.prerelease = fgetc(f) == '1'; fclose(f); }
  Updater_Check();
}
void Updater_Shutdown(void) {
  if (!initialized) return;
  Updater_Cancel();
  if (worker) { threadJoin(worker, UINT64_MAX); threadFree(worker); worker = NULL; }
  initialized = false;
}

unsigned Updater_GetNotes(char *out, unsigned capacity) {
  if (!initialized || !capacity) return 0;
  LightLock_Lock(&lock);
  snprintf(out, capacity, "%s", release.notes);
  unsigned revision = status.revision;
  LightLock_Unlock(&lock);
  return revision;
}
