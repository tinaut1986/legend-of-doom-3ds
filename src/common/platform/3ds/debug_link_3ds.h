#pragma once
// Remote control of the running game for debug builds (LOD3DS_DEBUG_TOOLS), carried as SITE
// commands over the in-game FTP server (ftp_server_3ds.h) so there is one port and one switch:
//
//   SITE KEYS <A+B+LEFT|NONE> [tics]      hold 3DS buttons for N input polls (default 1)
//   SITE STICK <CPAD|CSTICK> <x> <y> [tics]  hold a stick at x,y in -1..1 (y up), default 1
//   SITE TOUCH <x> <y>                    tap the bottom screen (320x240)
//   SITE SHOT                             write debug/shot-top.bmp and debug/shot-bottom.bmp
//   SITE DUMP                             take a quick diagnostic capture (as L+R+A)
//   SITE STATE                            one line: tic, game state, menu, map, player, fps
//   SITE CONSOLE <command>                run a GZDoom console command (map, give, save...)
//   SITE CVAR <name> [value]              read a console variable, or set it
//   SITE PEEK <address> <length>          hex of up to 256 bytes of this process's memory
//   SITE POKE <address> <hex bytes>       write them (game thread, between tics)
//
// tools/lod-link/ is the PC client. Anything that touches the game runs on the game thread
// (DebugLink_Poll, once per input poll); the FTP thread waits for it.
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/* FTP thread. Handles `verb` (upper case) with `args`; writes the whole reply line, code
 * included, into `reply`. False when the verb is not ours. */
bool DebugLink_Command(const char *verb, const char *args, char *reply, unsigned cap);

#ifdef __cplusplus
}

// Game thread, at the start of every input poll (I_GetEvent): posts the injected buttons and
// touches as SDL events and runs the queued request, if any.
void DebugLink_Poll();

// Game thread, from the joystick poll: the injected value of SDL axis `axis` (0/1 Circle Pad,
// 2/3 C-Stick, SDL's -32768..32767), or false when that axis is not being driven.
bool DebugLink_Axis(int axis, int *value);
#endif
