#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UPDATE_REPOSITORY "tinaut1986/legend-of-doom-3ds"
#define UPDATE_MAX_FILE (128u * 1024u * 1024u)
// The whole changelog (Update_ReleaseHistory), and what Update_FormatNotes reads.
#define UPDATE_HISTORY_MAX 32768
typedef struct UpdateRelease {
  char version[48];
  char notes[12289];
  char url[512];
  char sha256[65];
  uint32_t size;
  bool prerelease; // a beta (GitHub pre-release): its version counts as X.Y.Z-beta
} UpdateRelease;
// -1: invalid response, 0: no publication in this channel, 1: valid candidate.
// The stable channel (prerelease false) takes releases only; the beta channel takes the
// newest of releases and pre-releases. With `installed` (NULL: the offered one's body
// only), notes gathers everything published since that version (see gather_notes).
int Update_ParseRelease(const char *data, size_t size, bool prerelease,
                        bool homebrew, const char *installed, UpdateRelease *out);
// Every release the channel sees, newest first, each one's notes under its tag: the whole
// changelog. Returns how many (-1: invalid response).
int Update_ReleaseHistory(const char *data, size_t size, bool prerelease, char *out,
                          size_t capacity);
bool Update_IsNewer(const char *candidate, const char *installed);
bool Update_ValidVersion(const char *version);
bool Update_AllowedDownloadUrl(const char *url);

unsigned Update_FormatNotes(const char *markdown, char lines[][43], unsigned capacity);

#ifdef __cplusplus
}
#endif
