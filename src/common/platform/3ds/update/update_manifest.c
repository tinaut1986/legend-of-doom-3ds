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
    s++;
    // Our own builds: "-beta" is a published pre-release (the CI bakes the
    // channel into the binary), "-dev.N+hash" a local or CI build off a
    // release branch. Both sort below the plain X.Y.Z, like an upstream -En.
    if (!strcmp(s, "beta")) { v->pre = true; return true; }
    if (!strncmp(s, "dev", 3)) {
      for (s += 3; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '.' && *s != '+') return false;
      v->pre = true; return true;
    }
    if (*s++ != 'E' || !isdigit((unsigned char)*s)) return false;
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
static bool asset_name(const char *name, const char *version, bool homebrew) {
  char prefix[96], simple[100];
  const char *extension = homebrew ? ".3dsx" : ".cia";
  snprintf(simple, sizeof(simple), "legend-of-doom-3ds-v%s%s", version, extension);
  if (!strcmp(name, simple)) return true;
  snprintf(prefix, sizeof(prefix), "legend-of-doom-3ds-v%s-hardware-hybrid-", version);
  size_t n = strlen(prefix);
  if (strncmp(name, prefix, n) || strlen(name) != n + 12 + strlen(extension)) return false;
  for (size_t i = n; i < n + 12; ++i)
    if (!isxdigit((unsigned char)name[i])) return false;
  return !strcmp(name + n + 12, extension);
}
static bool candidate(json_t *o, bool pre, bool homebrew, UpdateRelease *out) {
  if (!json_is_object(o) || !json_is_false(json_object_get(o, "draft")) ||
      !json_is_boolean(json_object_get(o, "prerelease")) ||
      (!pre && json_is_true(json_object_get(o, "prerelease")))) return false;
  char tag[48];
  if (!text(o, "tag_name", tag, sizeof(tag)) || !Update_ValidVersion(tag)) return false;
  const char *v = tag[0] == 'v' ? tag + 1 : tag;
  char expected[512];
  json_t *assets = json_object_get(o, "assets"), *a; size_t i; bool found = false;
  if (!json_is_array(assets)) return false;
  json_array_foreach(assets, i, a) {
    char n[128], url[512], digest[80];
    if (!text(a, "name", n, sizeof(n)) || !asset_name(n, v, homebrew)) continue;
    snprintf(expected, sizeof(expected), "https://github.com/" UPDATE_REPOSITORY "/releases/download/%s/%s", tag, n);
    if (found || !text(a, "browser_download_url", url, sizeof(url)) || strcmp(url, expected) ||
        !text(a, "digest", digest, sizeof(digest)) || strlen(digest) != 71 || strncmp(digest, "sha256:", 7)) return false;
    json_t *s = json_object_get(a, "size");
    if (!json_is_integer(s) || json_integer_value(s) < 4096 || json_integer_value(s) > UPDATE_MAX_FILE) return false;
    for (int j = 7; j < 71; j++) if (!isxdigit((unsigned char)digest[j])) return false;
    memset(out, 0, sizeof(*out)); strcpy(out->version, tag); strcpy(out->url, url);
    out->prerelease = json_is_true(json_object_get(o, "prerelease"));
    for (int j = 0; j < 64; j++) out->sha256[j] = tolower((unsigned char)digest[j + 7]);
    json_t *body = json_object_get(o, "body");
    if (json_is_string(body)) snprintf(out->notes, sizeof(out->notes), "%s", json_string_value(body));
    out->size = (uint32_t)json_integer_value(s); found = true;
  }
  return found;
}
// The version a release stands for. A pre-release (beta) tag is the number of the stable
// release it leads to, so it counts as X.Y.Z-beta, below X.Y.Z.
static void release_version(json_t *o, char *dst, size_t cap) {
  const char *tag = json_string_value(json_object_get(o, "tag_name"));
  bool beta = json_is_true(json_object_get(o, "prerelease")) && !strchr(tag, '-');
  snprintf(dst, cap, "%s%s", tag, beta ? "-beta" : "");
}
static bool newer_release(json_t *a, json_t *b) {
  char va[64], vb[64]; release_version(a, va, sizeof(va)); release_version(b, vb, sizeof(vb));
  return Update_IsNewer(va, vb);
}
// The player-facing part of a release body: its <!-- lod-notes --> block, else from
// "## Changelog", else all of it.
static void notes_section(const char *md, char *dst, size_t cap) {
  const char *section = md ? strstr(md, "<!-- lod-notes -->") : NULL;
  size_t len;
  if (section) {
    const char *stop = strstr(section, "<!-- /lod-notes -->");
    len = stop ? (size_t)(stop - section) : strlen(section);
  } else {
    section = md ? strstr(md, "## Changelog") : NULL;
    if (!section) section = md ? md : "";
    len = strlen(section);
  }
  if (len >= cap) len = cap - 1;
  memcpy(dst, section, len); dst[len] = 0;
}
static void sort_newest_first(json_t **list, size_t count) {
  for (size_t a = 1; a < count; a++)
    for (size_t b = a; b > 0 && newer_release(list[b], list[b - 1]); b--) {
      json_t *t = list[b]; list[b] = list[b - 1]; list[b - 1] = t;
    }
}
// Whether the channel sees a release: no drafts, a valid tag, and betas on Beta only.
static bool visible(json_t *o, bool pre) {
  if (json_is_true(json_object_get(o, "draft")) ||
      !json_is_boolean(json_object_get(o, "prerelease"))) return false;
  if (!pre && json_is_true(json_object_get(o, "prerelease"))) return false;
  return Update_ValidVersion(json_string_value(json_object_get(o, "tag_name")));
}
// The releases' notes as one block for Update_FormatNotes, each under its tag ("## vX.Y.Z",
// "## vX.Y.Z BETA"). One block, so a section that fell back to a "## Changelog" list
// does not make it skip the ones before.
static void write_notes(json_t **list, size_t count, char *dst, size_t cap) {
  static char section[UPDATE_HISTORY_MAX];
  size_t n = (size_t)snprintf(dst, cap, "<!-- lod-notes -->\n");
  for (size_t a = 0; a < count && n + 1 < cap; a++) {
    json_t *body = json_object_get(list[a], "body");
    notes_section(json_is_string(body) ? json_string_value(body) : "", section, sizeof(section));
    // The block's own "## What's new" heading becomes the release's tag.
    char *text = section;
    if (!strncmp(text, "<!-- lod-notes -->", 18)) text += 18;
    while (*text == '\n' || *text == '\r') text++;
    if (!strncmp(text, "## What's new", 13)) text += 13;
    while (*text == '\n' || *text == '\r') text++;
    const char *tag = json_string_value(json_object_get(list[a], "tag_name"));
    bool beta = json_is_true(json_object_get(list[a], "prerelease"));
    int written = snprintf(dst + n, cap - n, "%s## %s%s\n%s\n",
                           a ? "\n" : "", tag, beta ? " BETA" : "", text);
    if (written < 0) break;
    n += (size_t)written;
    if (n >= cap) n = cap - 1;
  }
}
// Everything published between the installed version and the offered one, newest first: every
// stable release and every beta on the way, on either channel (a stable player sees the betas'
// notes too, so a stable's own notes list only what is new since the previous tag). One release
// keeps its own body.
static void gather_notes(json_t *root, bool pre, const char *installed, json_t *latest,
                         UpdateRelease *out) {
  (void)pre;
  json_t *picked[128]; size_t count = 0, i; json_t *o;
  json_array_foreach(root, i, o) {
    if (count == 128) break;
    if (!visible(o, true)) continue;
    if (o != latest && newer_release(o, latest)) continue;
    char version[64]; release_version(o, version, sizeof(version));
    if (!Update_IsNewer(version, installed)) continue;
    picked[count++] = o;
  }
  sort_newest_first(picked, count);
  if (count >= 2) write_notes(picked, count, out->notes, sizeof(out->notes));
}
int Update_ReleaseHistory(const char *data, size_t size, bool pre, char *out, size_t capacity) {
  if (!capacity) return -1;
  out[0] = 0;
  json_error_t err; json_t *root = json_loadb(data, size, JSON_REJECT_DUPLICATES, &err);
  if (!root) return -1;
  int count = -1;
  if (json_is_array(root)) {
    json_t *list[128]; size_t n = 0, i; json_t *o;
    json_array_foreach(root, i, o) if (n < 128 && visible(o, pre)) list[n++] = o;
    sort_newest_first(list, n);
    if (n) write_notes(list, n, out, capacity);
    count = (int)n;
  }
  json_decref(root); return count;
}
int Update_ParseRelease(const char *data, size_t size, bool pre, bool homebrew,
                        const char *installed, UpdateRelease *out) {
  memset(out, 0, sizeof(*out));
  json_error_t err; json_t *root = json_loadb(data, size, JSON_REJECT_DUPLICATES, &err);
  if (!root) return -1;
  int result = 0;
  if (!json_is_array(root)) result = -1;
  else {
    size_t i; json_t *o, *latest = NULL;
    json_array_foreach(root, i, o) {
      if (json_is_true(json_object_get(o, "draft"))) continue;
      // The stable channel sees releases only; the beta channel sees both, so a stable
      // release newer than the last beta is offered there too.
      if (!json_is_boolean(json_object_get(o, "prerelease")) ||
          (!pre && json_is_true(json_object_get(o, "prerelease")))) continue;
      const char *tag = json_string_value(json_object_get(o, "tag_name"));
      if (!Update_ValidVersion(tag)) continue;
      if (!latest || newer_release(o, latest)) latest = o;
    }
    if (latest) result = candidate(latest, pre, homebrew, out) ? 1 : -1;
    if (result == 1 && installed) gather_notes(root, pre, installed, latest, out);
  }
  json_decref(root); return result;
}

// Plain-text, word-wrapped release body for a 42-column handheld display.
// Image/link destinations and Markdown markers are presentation, never actions.
unsigned Update_FormatNotes(const char *md, char lines[][43], unsigned capacity) {
  static char clean[UPDATE_HISTORY_MAX]; size_t n = 0;
  // Release bodies start with a beta notice and a QR code. The handheld shows
  // the player-facing notes (docs/release-notes/<tag>.md, which the release
  // workflow puts between <!-- lod-notes --> markers under "## What's new"),
  // or else the commit list under "## Changelog".
  const char *section = md ? strstr(md, "<!-- lod-notes -->") : NULL;
  if (section) {
    md = section;
    const char *stop = strstr(md, "<!-- /lod-notes -->");
    size_t len = stop ? (size_t)(stop - md) : strlen(md);
    static char notes_only[UPDATE_HISTORY_MAX];
    if (len >= sizeof(notes_only)) len = sizeof(notes_only) - 1;
    memcpy(notes_only, md, len); notes_only[len] = 0;
    md = notes_only;
  } else {
    section = md ? strstr(md, "## Changelog") : NULL;
    if (section) md = section;
  }

  for (size_t i = 0; md && md[i] && n < sizeof(clean)-1;) {
    if (md[i] == '<') {
      const char *end = strchr(md+i, '>');
      if (end) { i = end-md+1; continue; }
    }
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
