#include "debug_link_3ds.h"
#include "diagnostics_3ds.h"
#include "app_paths_3ds.h"
#include "ftp_server_3ds.h"
#include "lod3ds_version.h"

#if LOD3DS_DEBUG_TOOLS
#include <3ds.h>
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "c_cvars.h"
#include "c_dispatch.h"
#include "common/menu/menu.h"
#include "doomstat.h"
#include "g_levellocals.h"
#include "gamestate.h"
#include "menustate.h"
#include "playsim/actor.h"
#include "playsim/d_player.h"

extern uint64_t LastCount;

namespace
{
constexpr const char *LinkDebugDir = LOD3DS_APP_DIR "/debug";

// A request the game thread never takes (game hung, or stuck in a long load).
constexpr int64_t LinkWaitNanoseconds = 20000000000LL;
constexpr int64_t LinkWaitSliceNanoseconds = 50000000LL;

constexpr unsigned LinkPeekMax = 256;
constexpr unsigned LinkResultMax = 600;
constexpr unsigned LinkCommandMax = 400;

enum class ELinkOp
{
	None, Touch, Shot, Dump, StateLine, Console, CVar, Poke,
};

// One request at a time: the FTP server serves one client and one command.
LightLock RequestLock = 1; // LightLock_Init's value, so no init call is needed
LightEvent Done;
bool DoneReady;
std::atomic<ELinkOp> PendingOp{ ELinkOp::None };
ELinkOp ActiveOp = ELinkOp::None; // a request that takes more than one poll
int ArgA, ArgB;
uintptr_t PokeAddress;
uint8_t PokeBytes[LinkPeekMax];
unsigned PokeLength;
char TextArg[LinkCommandMax];
char TextValue[LinkCommandMax];
char Result[LinkResultMax];
uint64_t PollCount;

// Injected buttons: written by the FTP thread, read by the game thread.
std::atomic<uint32_t> KeysMask{ 0 };
std::atomic<uint32_t> KeysPolls{ 0 };
std::atomic<bool> KeysNew{ false };
uint64_t KeysUntil;
uint32_t KeysNow; // what is injected as held right now

// Injected sticks: axes 0/1 the Circle Pad, 2/3 the C-Stick, as SDL values.
std::atomic<int> StickValue[4];
std::atomic<uint32_t> StickPolls[2];
std::atomic<bool> StickNew[2];
uint64_t StickUntil[2];

// Bit numbers are libctru's HID bits, which are also SDL's N3DS joystick button indices
// (i_input_3ds.cpp, I_3DSMapJoystickButton).
const struct
{
	const char *Name;
	int Bit;
} Keys[] = {
	{ "A", 0 }, { "B", 1 }, { "SELECT", 2 }, { "START", 3 }, { "RIGHT", 4 }, { "LEFT", 5 },
	{ "UP", 6 }, { "DOWN", 7 }, { "R", 8 }, { "L", 9 }, { "X", 10 }, { "Y", 11 },
	{ "ZL", 14 }, { "ZR", 15 },
};

bool ParseKeys(const char *text, uint32_t &mask)
{
	mask = 0;
	char word[16];
	while (*text != '\0' && *text != ' ')
	{
		unsigned n = 0;
		while (*text != '\0' && *text != '+' && *text != ' ' && n + 1 < sizeof(word))
			word[n++] = static_cast<char>(std::toupper(static_cast<unsigned char>(*text++)));
		word[n] = '\0';
		if (*text == '+') ++text;
		if (std::strcmp(word, "NONE") == 0) continue;
		bool found = false;
		for (const auto &key : Keys)
		{
			if (std::strcmp(word, key.Name) == 0)
			{
				mask |= 1u << key.Bit;
				found = true;
			}
		}
		if (!found) return false;
	}
	return true;
}

// Readable (or writable) for `length` bytes, page by page: a bad address from the PC must not
// crash the game.
bool MemoryOk(uintptr_t address, unsigned length, bool write)
{
	uintptr_t at = address;
	const uintptr_t end = address + length;
	while (at < end)
	{
		MemInfo info;
		PageInfo page;
		if (R_FAILED(svcQueryMemory(&info, &page, static_cast<u32>(at)))) return false;
		if (info.state == MEMSTATE_FREE) return false;
		if (!(info.perm & (write ? MEMPERM_WRITE : MEMPERM_READ))) return false;
		at = info.base_addr + info.size;
	}
	return true;
}

int ParseHex(const char *text, uint8_t *out, unsigned cap)
{
	unsigned n = 0;
	while (text[0] != '\0' && text[1] != '\0' && n < cap)
	{
		const char pair[3] = { text[0], text[1], '\0' };
		char *end = nullptr;
		const long value = std::strtol(pair, &end, 16);
		if (*end != '\0') return -1;
		out[n++] = static_cast<uint8_t>(value);
		text += 2;
	}
	return text[0] != '\0' ? -1 : static_cast<int>(n);
}

// Hands the request to the game thread and waits for its result line.
void RunOnGameThread(ELinkOp op, char *reply, unsigned cap)
{
	LightLock_Lock(&RequestLock);
	if (!DoneReady)
	{
		LightEvent_Init(&Done, RESET_ONESHOT);
		DoneReady = true;
	}
	LightEvent_Clear(&Done);
	Result[0] = '\0';
	PendingOp.store(op, std::memory_order_release);
	// In slices, giving up as soon as the server stops: FtpServer_Stop runs on the game thread,
	// so a request waiting for it could only time out, long after the stop's 3 s join.
	bool done = false;
	for (int64_t waited = 0; waited < LinkWaitNanoseconds && FtpServer_Running();
		waited += LinkWaitSliceNanoseconds)
	{
		if (LightEvent_WaitTimeout(&Done, LinkWaitSliceNanoseconds) == 0)
		{
			done = true;
			break;
		}
	}
	if (!done)
	{
		PendingOp.store(ELinkOp::None, std::memory_order_release);
		std::snprintf(reply, cap, "451 The game did not take the request (loading or stopped?).");
	}
	else
	{
		std::snprintf(reply, cap, "%s", Result);
	}
	LightLock_Unlock(&RequestLock);
}

const char *GameStateName()
{
	switch (gamestate)
	{
	case GS_LEVEL: return "LEVEL";
	case GS_INTERMISSION: return "INTERMISSION";
	case GS_FINALE: return "FINALE";
	case GS_DEMOSCREEN: return "TITLE";
	case GS_FULLCONSOLE: return "CONSOLE";
	case GS_HIDECONSOLE: return "HIDECONSOLE";
	case GS_STARTUP: return "STARTUP";
	case GS_TITLELEVEL: return "TITLELEVEL";
	case GS_INTRO: return "INTRO";
	default: return "WIPE";
	}
}

// The open menu: its descriptor name when it has one (MENUDEF), else its class; and the
// selected row.
void MenuName(char *out, size_t cap, int &selected)
{
	selected = -1;
	if (menuactive == MENU_Off || CurrentMenu == nullptr)
	{
		std::snprintf(out, cap, "none");
		return;
	}
	if (CurrentMenu->IsKindOf("ListMenu"))
	{
		auto *descriptor = CurrentMenu->PointerVar<DListMenuDescriptor>(FName("mDesc"));
		if (descriptor != nullptr)
		{
			selected = descriptor->mSelectedItem;
			std::snprintf(out, cap, "%s", descriptor->mMenuName.GetChars());
			return;
		}
	}
	if (CurrentMenu->IsKindOf("OptionMenu"))
	{
		auto *descriptor = CurrentMenu->PointerVar<DOptionMenuDescriptor>(FName("mDesc"));
		if (descriptor != nullptr)
		{
			selected = descriptor->mSelectedItem;
			std::snprintf(out, cap, "%s", descriptor->mMenuName.GetChars());
			return;
		}
	}
	std::snprintf(out, cap, "%s", CurrentMenu->GetClass()->TypeName.GetChars());
}

void StateLine()
{
	char menu[64];
	int selected = -1;
	MenuName(menu, sizeof(menu), selected);
	int n = std::snprintf(Result, sizeof(Result),
		"200 poll=%llu tic=%d state=%s menu=%s item=%d fps=%llu",
		static_cast<unsigned long long>(PollCount), gametic, GameStateName(), menu, selected,
		static_cast<unsigned long long>(LastCount));
	if (gamestate == GS_LEVEL && n > 0 && static_cast<unsigned>(n) < sizeof(Result))
	{
		n += std::snprintf(Result + n, sizeof(Result) - n, " map=%s", primaryLevel->MapName.GetChars());
		const AActor *mo = players[consoleplayer].mo;
		if (mo != nullptr && n > 0 && static_cast<unsigned>(n) < sizeof(Result))
		{
			std::snprintf(Result + n, sizeof(Result) - n,
				" x=%.1f y=%.1f z=%.1f angle=%.1f health=%d",
				mo->X(), mo->Y(), mo->Z(), mo->Angles.Yaw.Degrees, mo->health);
		}
	}
}

void PushFinger(Uint32 type)
{
	SDL_Event event = {};
	event.type = type;
	event.tfinger.touchId = 0;
	event.tfinger.fingerId = 0;
	event.tfinger.x = (ArgA + 0.5f) / 320.0f;
	event.tfinger.y = (ArgB + 0.5f) / 240.0f;
	event.tfinger.pressure = 1.0f;
	SDL_PushEvent(&event);
}

void PushButton(int bit, bool down)
{
	SDL_Event event = {};
	event.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
	event.jbutton.which = 0;
	event.jbutton.button = static_cast<Uint8>(bit);
	event.jbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
	SDL_PushEvent(&event);
}

// Starts a request. True when it is complete (Result set); false when it needs another poll
// (Continue finishes it).
bool Start(ELinkOp op)
{
	switch (op)
	{
	case ELinkOp::Touch:
		PushFinger(SDL_FINGERDOWN);
		return false; // lifted on the next poll
	case ELinkOp::Shot:
	{
		mkdir(LinkDebugDir, 0777);
		char top[128], bottom[128];
		std::snprintf(top, sizeof(top), "%s/shot-top.bmp", LinkDebugDir);
		std::snprintf(bottom, sizeof(bottom), "%s/shot-bottom.bmp", LinkDebugDir);
		const bool ok = I_3DSSaveScreenShots(top, bottom);
		std::snprintf(Result, sizeof(Result), ok ? "200 debug/shot-top.bmp debug/shot-bottom.bmp"
			: "550 Could not write the screenshots.");
		return true;
	}
	case ELinkOp::Dump:
		I_3DSRequestDiagnosticDump();
		std::snprintf(Result, sizeof(Result), "200 Quick capture requested (dumps/).");
		return true;
	case ELinkOp::StateLine:
		StateLine();
		return true;
	case ELinkOp::Console:
		AddCommandString(TextArg);
		std::snprintf(Result, sizeof(Result), "200 Ran: %s", TextArg);
		return true;
	case ELinkOp::CVar:
	{
		FBaseCVar *cvar = FindCVar(TextArg, nullptr);
		if (cvar == nullptr)
		{
			std::snprintf(Result, sizeof(Result), "550 No console variable %s.", TextArg);
			return true;
		}
		if (TextValue[0] != '\0')
		{
			UCVarValue value;
			value.String = TextValue;
			cvar->SetGenericRep(value, CVAR_String);
		}
		std::snprintf(Result, sizeof(Result), "200 %s = %s", TextArg, cvar->GetHumanString());
		return true;
	}
	case ELinkOp::Poke:
		std::memcpy(reinterpret_cast<void *>(PokeAddress), PokeBytes, PokeLength);
		std::snprintf(Result, sizeof(Result), "200 Wrote %u bytes.", PokeLength);
		return true;
	default:
		return true;
	}
}

bool Continue(ELinkOp op)
{
	if (op == ELinkOp::Touch)
	{
		PushFinger(SDL_FINGERUP);
		std::snprintf(Result, sizeof(Result), "200 Tapped %d,%d.", ArgA, ArgB);
	}
	return true;
}

void SplitWord(const char *args, char *word, size_t wordCap, const char *&rest)
{
	size_t n = 0;
	while (*args == ' ') ++args;
	while (*args != '\0' && *args != ' ' && n + 1 < wordCap) word[n++] = *args++;
	word[n] = '\0';
	while (*args == ' ') ++args;
	rest = args;
}
}

bool DebugLink_Command(const char *verb, const char *args, char *reply, unsigned cap)
{
	if (args == nullptr) args = "";
	if (std::strcmp(verb, "KEYS") == 0)
	{
		uint32_t mask = 0;
		if (!ParseKeys(args, mask))
		{
			std::snprintf(reply, cap, "501 KEYS <A+B+LEFT...|NONE> [polls]");
			return true;
		}
		const char *polls = std::strchr(args, ' ');
		long n = polls != nullptr ? std::strtol(polls + 1, nullptr, 10) : 1;
		if (n < 1) n = 1;
		KeysMask.store(mask, std::memory_order_relaxed);
		KeysPolls.store(static_cast<uint32_t>(n), std::memory_order_relaxed);
		KeysNew.store(true, std::memory_order_release);
		std::snprintf(reply, cap, "200 Holding %s for %ld polls.", args, n);
		return true;
	}
	if (std::strcmp(verb, "STICK") == 0)
	{
		char which[16] = {};
		float x = 0.0f, y = 0.0f;
		long n = 1;
		const int fields = std::sscanf(args, "%15s %f %f %ld", which, &x, &y, &n);
		for (char *c = which; *c != '\0'; ++c) *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
		const int stick = std::strcmp(which, "CPAD") == 0 ? 0 : (std::strcmp(which, "CSTICK") == 0 ? 1 : -1);
		if (fields < 3 || stick < 0)
		{
			std::snprintf(reply, cap, "501 STICK <CPAD|CSTICK> <x -1..1> <y -1..1, up> [polls]");
			return true;
		}
		if (n < 1) n = 1;
		// SDL's sticks are positive right and down.
		StickValue[stick * 2 + 0].store(static_cast<int>(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
		StickValue[stick * 2 + 1].store(static_cast<int>(std::clamp(-y, -1.0f, 1.0f) * 32767.0f));
		StickPolls[stick].store(static_cast<uint32_t>(n), std::memory_order_relaxed);
		StickNew[stick].store(true, std::memory_order_release);
		std::snprintf(reply, cap, "200 Holding %s at %.2f,%.2f for %ld polls.", which, x, y, n);
		return true;
	}
	if (std::strcmp(verb, "TOUCH") == 0)
	{
		if (std::sscanf(args, "%d %d", &ArgA, &ArgB) != 2 || ArgA < 0 || ArgA >= 320 ||
			ArgB < 0 || ArgB >= 240)
		{
			std::snprintf(reply, cap, "501 TOUCH <x 0-319> <y 0-239>");
			return true;
		}
		RunOnGameThread(ELinkOp::Touch, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "SHOT") == 0)
	{
		RunOnGameThread(ELinkOp::Shot, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "DUMP") == 0)
	{
		RunOnGameThread(ELinkOp::Dump, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "STATE") == 0)
	{
		RunOnGameThread(ELinkOp::StateLine, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "CONSOLE") == 0)
	{
		if (*args == '\0')
		{
			std::snprintf(reply, cap, "501 CONSOLE <command>");
			return true;
		}
		std::snprintf(TextArg, sizeof(TextArg), "%s", args);
		RunOnGameThread(ELinkOp::Console, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "CVAR") == 0)
	{
		const char *value = "";
		SplitWord(args, TextArg, sizeof(TextArg), value);
		if (TextArg[0] == '\0')
		{
			std::snprintf(reply, cap, "501 CVAR <name> [value]");
			return true;
		}
		std::snprintf(TextValue, sizeof(TextValue), "%s", value);
		RunOnGameThread(ELinkOp::CVar, reply, cap);
		return true;
	}
	if (std::strcmp(verb, "PEEK") == 0)
	{
		unsigned long address = 0, length = 0;
		if (std::sscanf(args, "%lx %lu", &address, &length) != 2 || length == 0 || length > LinkPeekMax)
		{
			std::snprintf(reply, cap, "501 PEEK <hex address> <length 1-%u>", LinkPeekMax);
			return true;
		}
		if (!MemoryOk(static_cast<uintptr_t>(address), static_cast<unsigned>(length), false))
		{
			std::snprintf(reply, cap, "550 Not readable.");
			return true;
		}
		int n = std::snprintf(reply, cap, "200 ");
		const auto *bytes = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(address));
		for (unsigned long i = 0; i < length && static_cast<unsigned>(n) + 3 < cap; ++i)
			n += std::snprintf(reply + n, cap - static_cast<unsigned>(n), "%02x", bytes[i]);
		return true;
	}
	if (std::strcmp(verb, "POKE") == 0)
	{
		unsigned long address = 0;
		char hex[2 * LinkPeekMax + 1];
		int length = -1;
		if (std::sscanf(args, "%lx %512s", &address, hex) != 2 ||
			(length = ParseHex(hex, PokeBytes, LinkPeekMax)) <= 0)
		{
			std::snprintf(reply, cap, "501 POKE <hex address> <hex bytes>");
			return true;
		}
		if (!MemoryOk(static_cast<uintptr_t>(address), static_cast<unsigned>(length), true))
		{
			std::snprintf(reply, cap, "550 Not writable.");
			return true;
		}
		PokeAddress = static_cast<uintptr_t>(address);
		PokeLength = static_cast<unsigned>(length);
		RunOnGameThread(ELinkOp::Poke, reply, cap);
		return true;
	}
	return false;
}

void DebugLink_Poll()
{
	++PollCount;
	// Buttons. A new KEYS first releases what is still held (one poll), so that the same button
	// pressed again is a new press for the game.
	uint32_t want = KeysNow;
	if (KeysNew.load(std::memory_order_acquire) && KeysNow != 0)
	{
		want = 0;
	}
	else
	{
		if (KeysNew.exchange(false, std::memory_order_acq_rel))
			KeysUntil = PollCount + KeysPolls.load(std::memory_order_relaxed);
		want = PollCount < KeysUntil ? KeysMask.load(std::memory_order_relaxed) : 0;
	}
	for (const auto &key : Keys)
	{
		const uint32_t bit = 1u << key.Bit;
		if ((want ^ KeysNow) & bit) PushButton(key.Bit, (want & bit) != 0);
	}
	KeysNow = want;

	for (int stick = 0; stick < 2; ++stick)
	{
		if (StickNew[stick].exchange(false, std::memory_order_acq_rel))
			StickUntil[stick] = PollCount + StickPolls[stick].load(std::memory_order_relaxed);
	}

	if (ActiveOp != ELinkOp::None)
	{
		if (Continue(ActiveOp))
		{
			ActiveOp = ELinkOp::None;
			LightEvent_Signal(&Done);
		}
		return;
	}
	const ELinkOp op = PendingOp.exchange(ELinkOp::None, std::memory_order_acq_rel);
	if (op == ELinkOp::None) return;
	if (Start(op)) LightEvent_Signal(&Done);
	else ActiveOp = op;
}

bool DebugLink_Axis(int axis, int *value)
{
	if (axis < 0 || axis >= 4 || PollCount >= StickUntil[axis / 2]) return false;
	*value = StickValue[axis].load(std::memory_order_relaxed);
	return true;
}

#else

bool DebugLink_Command(const char *, const char *, char *, unsigned) { return false; }
void DebugLink_Poll() {}
bool DebugLink_Axis(int, int *) { return false; }

#endif
