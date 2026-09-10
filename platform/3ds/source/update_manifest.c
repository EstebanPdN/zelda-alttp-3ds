#include "update_manifest.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct Version { unsigned part[3], experiment; bool pre; } Version;
static bool parse_version(const char *s, Version *v) {
  memset(v, 0, sizeof(*v));
  if (!s) return false;
  if (*s == 'v') s++;
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)*s)) return false;
    unsigned n = 0; int digits = 0;
    while (isdigit((unsigned char)*s)) {
      n = n * 10 + (*s++ - '0');
      if (++digits > 5 || n > 65535) return false;
    }
    v->part[i] = n;
    if (*s != '.') break;
    if (i == 2) return false;
    s++;
  }
  if (*s == '-') {
    s++; if (*s++ != 'E' || !isdigit((unsigned char)*s)) return false;
    v->pre = true;
    while (isdigit((unsigned char)*s)) {
      v->experiment = v->experiment * 10 + (*s++ - '0');
      if (v->experiment > 65535) return false;
    }
  }
  return *s == 0;
}
bool Update_ValidVersion(const char *s) { Version v; return parse_version(s, &v); }
bool Update_IsNewer(const char *candidate, const char *installed) {
  Version a, b;
  if (!parse_version(candidate, &a) || !parse_version(installed, &b)) return false;
  for (int i = 0; i < 3; i++) if (a.part[i] != b.part[i]) return a.part[i] > b.part[i];
  if (a.pre != b.pre) return !a.pre;
  return a.pre && a.experiment > b.experiment;
}
bool Update_AllowedDownloadUrl(const char *url) {
  const char *prefix = "https://github.com/" UPDATE_REPOSITORY "/releases/download/";
  return url && !strncmp(url, prefix, strlen(prefix));
}
static bool text(json_t *o, const char *key, char *dst, size_t cap) {
  json_t *v = json_object_get(o, key);
  if (!json_is_string(v) || json_string_length(v) >= cap ||
      strlen(json_string_value(v)) != json_string_length(v)) return false;
  strcpy(dst, json_string_value(v)); return true;
}
static bool candidate(json_t *o, bool pre, bool homebrew, UpdateRelease *out) {
  if (!json_is_object(o) || !json_is_false(json_object_get(o, "draft")) ||
      !json_is_boolean(json_object_get(o, "prerelease")) ||
      json_is_true(json_object_get(o, "prerelease")) != pre) return false;
  char tag[48];
  if (!text(o, "tag_name", tag, sizeof(tag)) || !Update_ValidVersion(tag)) return false;
  const char *v = tag[0] == 'v' ? tag + 1 : tag;
  char name[96], expected[512];
  snprintf(name, sizeof(name), "zelda3-3ds-v%s.%s", v, homebrew ? "3dsx" : "cia");
  snprintf(expected, sizeof(expected), "https://github.com/" UPDATE_REPOSITORY "/releases/download/%s/%s", tag, name);
  json_t *assets = json_object_get(o, "assets"), *a; size_t i; bool found = false;
  if (!json_is_array(assets)) return false;
  json_array_foreach(assets, i, a) {
    char n[96], url[512], digest[80];
    if (!text(a, "name", n, sizeof(n)) || strcmp(n, name)) continue;
    if (found || !text(a, "browser_download_url", url, sizeof(url)) || strcmp(url, expected) ||
        !text(a, "digest", digest, sizeof(digest)) || strlen(digest) != 71 || strncmp(digest, "sha256:", 7)) return false;
    json_t *s = json_object_get(a, "size");
    if (!json_is_integer(s) || json_integer_value(s) < 4096 || json_integer_value(s) > UPDATE_MAX_FILE) return false;
    for (int j = 7; j < 71; j++) if (!isxdigit((unsigned char)digest[j])) return false;
    memset(out, 0, sizeof(*out)); strcpy(out->version, tag); strcpy(out->url, url);
    for (int j = 0; j < 64; j++) out->sha256[j] = tolower((unsigned char)digest[j + 7]);
    json_t *body = json_object_get(o, "body");
    if (json_is_string(body)) snprintf(out->notes, sizeof(out->notes), "%s", json_string_value(body));
    out->size = (uint32_t)json_integer_value(s); found = true;
  }
  return found;
}
int Update_ParseRelease(const char *data, size_t size, bool pre, bool homebrew, UpdateRelease *out) {
  memset(out, 0, sizeof(*out));
  json_error_t err; json_t *root = json_loadb(data, size, JSON_REJECT_DUPLICATES, &err);
  if (!root) return -1;
  int result = 0;
  if (!pre) result = candidate(root, false, homebrew, out) ? 1 : -1;
  else if (!json_is_array(root)) result = -1;
  else {
    size_t i; json_t *o;
    json_array_foreach(root, i, o) {
      if (!json_is_true(json_object_get(o, "prerelease"))) continue;
      UpdateRelease r;
      if (!candidate(o, true, homebrew, &r)) { result = -1; break; }
      if (!result || Update_IsNewer(r.version, out->version)) *out = r;
      result = 1;
    }
  }
  json_decref(root); return result;
}

// Plain-text, word-wrapped release body for a 42-column handheld display.
// Image/link destinations and Markdown markers are presentation, never actions.
unsigned Update_FormatNotes(const char *md, char lines[][43], unsigned capacity) {
  char clean[12289]; size_t n = 0;
  for (size_t i = 0; md && md[i] && n < sizeof(clean)-1;) {
    if (md[i] == '!' && md[i+1] == '[') {
      const char *end = strstr(md+i, ")");
      if (end) { i = end-md+1; continue; }
    }
    if (md[i] == '[') { i++; continue; }
    if (md[i] == ']' && md[i+1] == '(') {
      const char *end = strchr(md+i+2, ')');
      if (end) { i = end-md+1; continue; }
    }
    unsigned char c = md[i++];
    if (c == '*' || c == '`' || c == '#' || c == '\r') continue;
    if (c >= 128) continue;
    clean[n++] = c;
  }
  clean[n] = 0;
  unsigned count = 0; const char *p = clean;
  while (*p && count < capacity) {
    while (*p == ' ' || (*p == '\n' && (!count || !lines[count-1][0]))) p++;
    if (!*p) break;
    const char *end = strchr(p, '\n'); if (!end) end = p + strlen(p);
    size_t take = end-p;
    if (take > 42) {
      take = 42;
      while (take && p[take] != ' ') take--;
      if (!take) take = 42;
    }
    memcpy(lines[count], p, take); lines[count++][take] = 0;
    p += take; if (*p == '\n') p++;
  }
  if (!count && capacity) { strcpy(lines[0], "No changelog provided."); count = 1; }
  return count;
}
