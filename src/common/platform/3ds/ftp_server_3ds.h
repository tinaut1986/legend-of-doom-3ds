#pragma once
// A small FTP server inside the game, for debug builds (LOD3DS_DEBUG_TOOLS; OPTIONS -> DEVELOPER ->
// FTP SERVER): fetch logs and dumps, or drop files on the SD card, while the game keeps running.
// No FBI or ftpd round trip.
//
// One client at a time, passive mode only, the whole SD card as "/", login in
// ftp_credentials_3ds.h. Commands: USER PASS SYST FEAT TYPE MODE STRU PWD CWD CDUP PASV EPSV LIST
// NLST MLSD RETR STOR APPE DELE MKD RMD RNFR RNTO SIZE MDTM NOOP QUIT OPTS. Enough for FileZilla,
// WinSCP, curl and lftp.
//
// SITE INSTALL <path>: installs that CIA (it must be this game's title ID) over the running
// game and relaunches into the new build. With curl:
//   curl -u USER:PASS -T game.cia "ftp://IP:5000/3ds/Legend%20of%20Doom%203DS/update/dev.cia"
//        -Q "-SITE INSTALL /3ds/Legend of Doom 3DS/update/dev.cia"   (the "-": after the upload)
// (platform/3ds/tools/build_3ds.py --install does both). The top screen says so meanwhile
// (FtpServer_Notice): an install never happens without a warning.
//
// Other SITE commands are the remote control, debug_link_3ds.h.
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>

enum { FTP_SERVER_PORT = 5000 };

bool FtpServer_Start(void);
/* Stable builds: the install-only server, started when dev-ftp.txt is in the data folder. Only
 * <data folder>/update is reachable and the remote control (SITE) is off. Not started again
 * after a cancel: the player has to leave the game and open it again. */
bool FtpServer_StartInstallOnly(void);
bool FtpServer_InstallOnly(void); /* serving in that mode right now */
void FtpServer_Cancel(void);
void FtpServer_Stop(void);
bool FtpServer_Running(void);
// "192.168.1.50:5000", or a short reason when it is not serving.
void FtpServer_Address(char *out, unsigned cap);
// Files sent and received since start, and whether a client is connected.
void FtpServer_Stats(unsigned *sent, unsigned *received, bool *connected);
// While a .cia is being received or SITE INSTALL runs, the line to show the player; NULL
// otherwise.
const char *FtpServer_Notice(void);
// Main thread, once per frame: hands what the server thread has to say to `log`.
void FtpServer_Tick(void (*log)(const char *line));
// True once, after a SITE INSTALL succeeded: the main loop jumps into the new build.
bool FtpServer_TakeRelaunch(void);

#ifdef __cplusplus
}
#endif
