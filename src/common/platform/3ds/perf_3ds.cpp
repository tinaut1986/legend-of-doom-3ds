#include "perf_3ds.h"
#include "app_paths_3ds.h"
#include "lod3ds_version.h"

#include <3ds.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "c_dispatch.h"
#include "doomstat.h"
#include "gamestate.h"
#include "menustate.h"
#include "printf.h"

bool I3DSPerfRunning;

namespace
{
// 36000 rows: ten minutes at 60 fps, ~2 MB.
constexpr unsigned PerfCapacity = 36000;
constexpr const char *PerfDirectory = LOD3DS_APP_DIR "/debug";
constexpr unsigned PartCount = static_cast<unsigned>(E3DSPerfPart::Count);

struct FPerfRow
{
	uint64_t EndTick;
	uint32_t PartTicks[PartCount];
	uint16_t Tics;
	uint16_t CanvasWidth, CanvasHeight;
	uint8_t GameState;
	uint8_t Menu;
};

FPerfRow *Rows;
unsigned RowCount;
uint32_t CurrentTicks[PartCount];
uint16_t CurrentTics;
uint64_t StartTick;

// The next free debug/perf-NNNN.csv number.
int NextFileNumber()
{
	int highest = 0;
	if (DIR *dir = opendir(PerfDirectory))
	{
		while (dirent *entry = readdir(dir))
		{
			int number = 0;
			if (std::sscanf(entry->d_name, "perf-%d.csv", &number) == 1 && number > highest)
				highest = number;
		}
		closedir(dir);
	}
	return highest + 1;
}

const char *StateName(uint8_t state)
{
	switch (state)
	{
	case GS_LEVEL: return "level";
	case GS_INTERMISSION: return "intermission";
	case GS_FINALE: return "finale";
	case GS_DEMOSCREEN: return "title";
	case GS_TITLELEVEL: return "titlelevel";
	default: return "other";
	}
}
}

F3DSPerfScope::F3DSPerfScope(E3DSPerfPart part)
	: Part(part), Start(I3DSPerfRunning ? svcGetSystemTick() : 0)
{
}

F3DSPerfScope::~F3DSPerfScope()
{
	if (Start != 0 && I3DSPerfRunning) I_3DSPerfAdd(Part, svcGetSystemTick() - Start);
}

void I_3DSPerfAdd(E3DSPerfPart part, uint64_t ticks)
{
	if (!I3DSPerfRunning) return;
	CurrentTicks[static_cast<unsigned>(part)] += static_cast<uint32_t>(ticks);
}

uint64_t I_3DSPerfNow()
{
	return I3DSPerfRunning ? svcGetSystemTick() : 0;
}

void I_3DSPerfSince(E3DSPerfPart part, uint64_t start)
{
	if (start != 0) I_3DSPerfAdd(part, svcGetSystemTick() - start);
}

void I_3DSPerfTic()
{
	if (I3DSPerfRunning) ++CurrentTics;
}

void I_3DSPerfFrameDone(int canvasWidth, int canvasHeight)
{
	I_3DSSectionsFrameDone();
	if (!I3DSPerfRunning || RowCount >= PerfCapacity) return;
	FPerfRow &row = Rows[RowCount++];
	row.EndTick = svcGetSystemTick();
	std::memcpy(row.PartTicks, CurrentTicks, sizeof(CurrentTicks));
	row.Tics = CurrentTics;
	row.CanvasWidth = static_cast<uint16_t>(canvasWidth);
	row.CanvasHeight = static_cast<uint16_t>(canvasHeight);
	row.GameState = static_cast<uint8_t>(gamestate);
	row.Menu = menuactive != MENU_Off;
	std::memset(CurrentTicks, 0, sizeof(CurrentTicks));
	CurrentTics = 0;
}

bool I_3DSPerfStart()
{
	if (I3DSPerfRunning) return true;
	Rows = static_cast<FPerfRow *>(std::malloc(sizeof(FPerfRow) * PerfCapacity));
	if (Rows == nullptr) return false;
	RowCount = 0;
	std::memset(CurrentTicks, 0, sizeof(CurrentTicks));
	CurrentTics = 0;
	StartTick = svcGetSystemTick();
	I3DSPerfRunning = true;
	return true;
}

int I_3DSPerfStop()
{
	if (!I3DSPerfRunning) return -1;
	I3DSPerfRunning = false;
	mkdir(PerfDirectory, 0777);
	int number = NextFileNumber();
	char path[160];
	std::snprintf(path, sizeof(path), "%s/perf-%04d.csv", PerfDirectory, number);
	FILE *file = std::fopen(path, "w");
	if (file != nullptr)
	{
		const double ms = 1000.0 / SYSCLOCK_ARM11;
		bool isNew3DS = false;
		APT_CheckNew3DS(&isNew3DS);
		std::fprintf(file, "# Legend of Doom 3DS v%s (upstream %s), build %s, %s\n",
			LOD3DS_PORT_VERSION_LABEL, LOD3DS_UPSTREAM_VERSION, LOD3DS_BUILD_ID,
			isNew3DS ? "New 3DS" : "Old 3DS");
		std::fprintf(file, "frame,t_ms,interval_ms,logic_ms,tics,world_ms,raster_wait_ms,twod_ms,"
			"compose_ms,present_ms,limit_ms,bottom_ms,input_ms,sound_ms,framestart_ms,hud_ms,"
			"other_ms,state,menu,canvas_w,canvas_h,walk_ms,capture_ms\n");
		uint64_t previous = StartTick;
		for (unsigned i = 0; i < RowCount; ++i)
		{
			const FPerfRow &row = Rows[i];
			const double interval = (row.EndTick - previous) * ms;
			double covered = 0.0;
			double parts[PartCount];
			for (unsigned part = 0; part < PartCount; ++part)
			{
				parts[part] = row.PartTicks[part] * ms;
				if (part < static_cast<unsigned>(E3DSPerfPart::Walk)) covered += parts[part];
			}
			const double other = interval > covered ? interval - covered : 0.0;
			std::fprintf(file, "%u,%.2f,%.2f,%.2f,%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
				"%.2f,%.2f,%s,%u,%u,%u,%.2f,%.2f\n",
				i, (row.EndTick - StartTick) * ms, interval,
				parts[static_cast<unsigned>(E3DSPerfPart::Logic)], row.Tics,
				parts[static_cast<unsigned>(E3DSPerfPart::World)],
				parts[static_cast<unsigned>(E3DSPerfPart::RasterWait)],
				parts[static_cast<unsigned>(E3DSPerfPart::TwoD)],
				parts[static_cast<unsigned>(E3DSPerfPart::Compose)],
				parts[static_cast<unsigned>(E3DSPerfPart::Present)],
				parts[static_cast<unsigned>(E3DSPerfPart::Limit)],
				parts[static_cast<unsigned>(E3DSPerfPart::Bottom)],
				parts[static_cast<unsigned>(E3DSPerfPart::Input)],
				parts[static_cast<unsigned>(E3DSPerfPart::Sound)],
				parts[static_cast<unsigned>(E3DSPerfPart::FrameStart)],
				parts[static_cast<unsigned>(E3DSPerfPart::Hud)],
				other, StateName(row.GameState), row.Menu, row.CanvasWidth, row.CanvasHeight,
				parts[static_cast<unsigned>(E3DSPerfPart::Walk)],
				parts[static_cast<unsigned>(E3DSPerfPart::Capture)]);
			previous = row.EndTick;
		}
		if (std::fclose(file) != 0) number = -1;
	}
	else
	{
		number = -1;
	}
	if (number > 0) Printf(PRINT_LOG, "perf recorder: %u rows -> %s\n", RowCount, path);
	std::free(Rows);
	Rows = nullptr;
	RowCount = 0;
	return number;
}

#if LOD3DS_DEBUG_TOOLS
CCMD(lod3ds_perf)
{
	const char *verb = argv.argc() > 1 ? argv[1] : "status";
	if (std::strcmp(verb, "start") == 0)
	{
		Printf("perf recorder: %s\n", I_3DSPerfStart() ? "started" : "no memory");
	}
	else if (std::strcmp(verb, "stop") == 0)
	{
		const int number = I_3DSPerfStop();
		if (number > 0) Printf("perf recorder: debug/perf-%04d.csv\n", number);
		else Printf("perf recorder: not running, or the file could not be written\n");
	}
	else
	{
		Printf("perf recorder: %s, %u of %u rows\n", I3DSPerfRunning ? "running" : "stopped",
			RowCount, PerfCapacity);
	}
}
#endif

// Named sections (perf_3ds.h). A small table; the names are literals, compared by address.
namespace
{
struct FSectionTotal
{
	const char *Name;
	uint64_t Ticks;
	uint64_t Calls;
};
FSectionTotal SectionTotals[24];
int SectionCount;
uint64_t SectionFrames;
}

static int SectionSlot(const char *name)
{
	for (int i = 0; i < SectionCount; ++i)
		if (SectionTotals[i].Name == name) return i;
	if (SectionCount >= int(sizeof(SectionTotals) / sizeof(SectionTotals[0]))) return -1;
	SectionTotals[SectionCount] = { name, 0, 0 };
	return SectionCount++;
}

F3DSSection::F3DSSection(const char *name)
{
	Slot = SectionSlot(name);
	Start = svcGetSystemTick();
}

void I_3DSSectionAdd(const char *name, uint64_t ticks)
{
	const int slot = SectionSlot(name);
	if (slot < 0) return;
	SectionTotals[slot].Ticks += ticks;
	SectionTotals[slot].Calls++;
}

F3DSSection::~F3DSSection()
{
	if (Slot < 0) return;
	SectionTotals[Slot].Ticks += svcGetSystemTick() - Start;
	SectionTotals[Slot].Calls++;
}

void I_3DSSectionsFrameDone()
{
	++SectionFrames;
}

#if LOD3DS_DEBUG_TOOLS
CCMD(lod3ds_sections)
{
	const double frames = SectionFrames > 0 ? double(SectionFrames) : 1.0;
	Printf("sections over %llu frames, ms a frame:\n", (unsigned long long)SectionFrames);
	for (int i = 0; i < SectionCount; ++i)
		Printf("  %-22s %7.3f  (%llu calls)\n", SectionTotals[i].Name,
			SectionTotals[i].Ticks * 1000.0 / SYSCLOCK_ARM11 / frames,
			(unsigned long long)SectionTotals[i].Calls);
	if (argv.argc() > 1)
	{
		for (int i = 0; i < SectionCount; ++i) SectionTotals[i].Ticks = SectionTotals[i].Calls = 0;
		SectionFrames = 0;
	}
}
#endif
