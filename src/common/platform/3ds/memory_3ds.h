#pragma once

#include <cstddef>
#include <cstdint>

// Hold one contiguous block while GZDoom parses ZScript, then return it just
// before SoftPoly creates its large vertex buffer. This prevents abundant but
// fragmented free heap from making that allocation fail on physical hardware.
bool I_3DSReserveRendererMemory();
void I_3DSReleaseRendererMemory();

// One contiguous block taken at start, before ZScript's compile leaves the heap in pieces, and
// handed back right before each map load, whose large blocks (MAP01's 2 MB TEXTMAP, 2.5 MB of
// sides) then find room; taken again once the map is loaded. On the Old 3DS (80 MB) the 2 MB
// TEXTMAP found no room with 17 MB free (2DS, 2026-10-14). The take returns false when no block
// of that size is free.
bool I_3DSReserveMapLoadMemory();
void I_3DSReleaseMapLoadMemory();

bool I_3DSMapLoadReserveHeld();

// PLAN P4.6: blocks of 32 KB or more come from an arena carved down from the top of the heap
// region (large_arena_3ds.cpp, linked through -Wl,--wrap=malloc and friends).
struct FLargeArenaStats
{
	uintptr_t Low = 0, High = 0;
	size_t Used = 0, Peak = 0, Free = 0, LargestFree = 0, Gap = 0, Failed = 0;
	unsigned UsedBlocks = 0, FreeBlocks = 0;
};
void I_3DSLargeArenaStats(FLargeArenaStats &stats);

// PLAN P4.5: writes memory-census-<label>.txt (heap chunks, GC objects by class, cached lumps,
// pool and reserves) to the data folder. The second one only when census-on-load.txt is there
// (debug builds); the console command lod3ds_memcensus [label] runs the first.
void I_3DSMemoryCensus(const char *label);
void I_3DSMemoryCensusIfRequested(const char *label);

// True on an Old 3DS / 2DS.
bool I_3DSIsOld3DS();

size_t I_3DSLastFailedNewRequest();
uintptr_t I_3DSLastFailedNewCaller();

// Largest block malloc can hand out right now (probed by binary search up to the free total), to
// tell fragmentation from a heap that is simply full.
size_t I_3DSLargestFreeBlock();
