#pragma once
// Installs a CIA of this title from the SD card over the installed one, for the debug FTP
// server's SITE INSTALL (ftp_server_3ds.c). The updater has its own copy of these steps
// (update/updater.c: it downloads first).
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

// `sdPath` is an SD path without "sdmc:" ("/3ds/..."). Refuses a CIA whose title ID is not the
// running title's. On failure `error` says why.
bool CiaInstall_File(const char *sdPath, char *error, unsigned errorCap);

// Jumps into the installed build of this title (APT application jump). Call from the main
// thread, then let the game close as on HOME -> close. False when the jump was refused.
bool CiaInstall_JumpToSelf(void);

#ifdef __cplusplus
}
#endif
