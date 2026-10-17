// Memory census (PLAN P4.5): what holds the heap, written to memory-census-<label>.txt in the data
// folder. The heap part walks newlib's malloc chunks (the free list's shape is what decides
// whether a large block fits); the engine part counts the garbage collector's objects by class,
// the cached lumps and the port's pool and reserves. Debug builds only.

#include <3ds.h>
#include <malloc.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "lod3ds_version.h"
#include "app_paths_3ds.h"
#include "diagnostics_3ds.h"
#include "memory_3ds.h"

#include "c_dispatch.h"
#include "dobject.h"
#include "dobjgc.h"
#include "filesystem.h"
#include "name.h"
#include "printf.h"
#include "tarray.h"

size_t M_3DSPoolUsed(size_t *high);

extern "C"
{
	// newlib's malloc (mallocr.c): the heap starts at the first sbrk and its top chunk is
	// __malloc_av_[2]; a chunk is { prev_size, size }, bit 0 of size saying the previous chunk
	// is in use.
	extern char *__malloc_sbrk_base;
	extern void *__malloc_av_[];
	void __malloc_lock(struct _reent *);
	void __malloc_unlock(struct _reent *);
}

namespace
{
constexpr size_t BucketLimits[] = { 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, ~size_t(0) };
constexpr int BucketCount = sizeof(BucketLimits) / sizeof(BucketLimits[0]);
constexpr int MapCells = 64;
constexpr int LargestKept = 8;

struct FHeapShape
{
	bool Walked = false;
	const char *Broken = nullptr;
	uintptr_t Base = 0, Top = 0;
	size_t TopSize = 0;
	size_t FreeCount[BucketCount] = {}, FreeBytes[BucketCount] = {};
	size_t UsedCount[BucketCount] = {}, UsedBytes[BucketCount] = {};
	size_t Largest[LargestKept] = {};
	uintptr_t LargestAt[LargestKept] = {};
	size_t CellFree[MapCells] = {};
	size_t CellBytes = 0;
};

int Bucket(size_t size)
{
	int i = 0;
	while (size > BucketLimits[i]) ++i;
	return i;
}

void AddFreeToMap(FHeapShape &shape, uintptr_t start, size_t size)
{
	for (uintptr_t at = start; at < start + size;)
	{
		const size_t cell = (at - shape.Base) / shape.CellBytes;
		if (cell >= MapCells) break;
		const uintptr_t cellEnd = shape.Base + (cell + 1) * shape.CellBytes;
		const uintptr_t end = std::min<uintptr_t>(cellEnd, start + size);
		shape.CellFree[cell] += end - at;
		at = end;
	}
}

// Read-only walk under malloc's lock; nothing here may allocate.
void WalkHeap(FHeapShape &shape)
{
	__malloc_lock(_REENT);
	shape.Base = reinterpret_cast<uintptr_t>(__malloc_sbrk_base);
	shape.Top = reinterpret_cast<uintptr_t>(__malloc_av_[2]);
	if (shape.Base == 0 || shape.Base == ~uintptr_t(0) || shape.Top <= shape.Base)
	{
		shape.Broken = "no heap bounds";
		__malloc_unlock(_REENT);
		return;
	}
	shape.TopSize = reinterpret_cast<const size_t *>(shape.Top)[1] & ~size_t(3);
	shape.CellBytes = (shape.Top + shape.TopSize - shape.Base + MapCells - 1) / MapCells;

	uintptr_t chunk = shape.Base;
	while (chunk < shape.Top)
	{
		const size_t size = reinterpret_cast<const size_t *>(chunk)[1] & ~size_t(3);
		const uintptr_t next = chunk + size;
		if (size < 16 || next > shape.Top)
		{
			shape.Broken = "chunk walk left the heap";
			break;
		}
		const bool used = (reinterpret_cast<const size_t *>(next)[1] & 1) != 0;
		const int bucket = Bucket(size);
		if (used)
		{
			++shape.UsedCount[bucket];
			shape.UsedBytes[bucket] += size;
		}
		else
		{
			++shape.FreeCount[bucket];
			shape.FreeBytes[bucket] += size;
			AddFreeToMap(shape, chunk, size);
			for (int i = 0; i < LargestKept; ++i)
			{
				if (size <= shape.Largest[i]) continue;
				for (int j = LargestKept - 1; j > i; --j)
				{
					shape.Largest[j] = shape.Largest[j - 1];
					shape.LargestAt[j] = shape.LargestAt[j - 1];
				}
				shape.Largest[i] = size;
				shape.LargestAt[i] = chunk;
				break;
			}
		}
		chunk = next;
	}
	AddFreeToMap(shape, shape.Top, shape.TopSize);
	shape.Walked = true;
	__malloc_unlock(_REENT);
}

struct FClassCount
{
	FName Name;
	size_t Count;
	size_t Bytes;
};

void WriteSize(FILE *out, const char *label, size_t bytes)
{
	std::fprintf(out, "%-28s %10zu  (%.2f MB)\n", label, bytes, bytes / 1048576.0);
}
}

void I_3DSMemoryCensus(const char *label)
{
	static FHeapShape shape;
	shape = FHeapShape{};
	WalkHeap(shape);

	TMap<FName, FClassCount> classes;
	size_t objects = 0, objectBytes = 0;
	for (DObject *object = GC::Root; object != nullptr; object = object->ObjNext)
	{
		PClass *type = object->GetClass();
		const FName name = type != nullptr ? type->TypeName : FName(NAME_None);
		const size_t bytes = type != nullptr ? type->Size : sizeof(DObject);
		FClassCount *entry = classes.CheckKey(name);
		if (entry == nullptr) entry = &classes.Insert(name, FClassCount{ name, 0, 0 });
		++entry->Count;
		entry->Bytes += bytes;
		++objects;
		objectBytes += bytes;
	}
	TArray<FClassCount> sorted;
	TMap<FName, FClassCount>::Iterator it(classes);
	TMap<FName, FClassCount>::Pair *pair;
	while (it.NextPair(pair)) sorted.Push(pair->Value);
	std::sort(sorted.begin(), sorted.end(),
		[](const FClassCount &a, const FClassCount &b) { return a.Bytes > b.Bytes; });

	size_t cachedLumps = 0, cachedBytes = 0;
	for (int i = 0; i < fileSystem.GetNumEntries(); ++i)
	{
		const FResourceLump *lump = fileSystem.GetFileAt(i);
		if (lump != nullptr && lump->Cache != nullptr && lump->RefCount >= 0)
		{
			++cachedLumps;
			cachedBytes += static_cast<size_t>(lump->LumpSize);
		}
	}

	char path[256];
	std::snprintf(path, sizeof(path), LOD3DS_APP_DIR "/memory-census-%s.txt", label);
	FILE *out = std::fopen(path, "w");
	if (out == nullptr) return;

	const struct mallinfo heap = mallinfo();
	size_t poolHigh = 0;
	const size_t poolUsed = M_3DSPoolUsed(&poolHigh);
	std::fprintf(out, "Memory census '%s', build %s (%s)\n\n", label, LOD3DS_BUILD_ID, LOD3DS_PORT_VERSION);
	WriteSize(out, "heap arena", heap.arena);
	WriteSize(out, "heap in use", heap.uordblks);
	WriteSize(out, "heap free", heap.fordblks);
	WriteSize(out, "gc objects (AllocBytes)", GC::AllocBytes);
	WriteSize(out, "gc objects (class sizes)", objectBytes);
	WriteSize(out, "cached lumps", cachedBytes);
	WriteSize(out, "TArray pool in use", poolUsed);
	WriteSize(out, "TArray pool high", poolHigh);
	std::fprintf(out, "%-28s %10s\n", "map-load reserve", I_3DSMapLoadReserveHeld() ? "held" : "free");
	FLargeArenaStats arena;
	I_3DSLargeArenaStats(arena);
	WriteSize(out, "large arena in use", arena.Used);
	WriteSize(out, "large arena peak", arena.Peak);
	WriteSize(out, "large arena free inside", arena.Free);
	WriteSize(out, "large arena largest free", arena.LargestFree);
	WriteSize(out, "gap heap break..arena", arena.Gap);
	std::fprintf(out, "%-28s %10u used, %u free, %zu failed, 0x%08lx-0x%08lx\n", "large arena blocks",
		arena.UsedBlocks, arena.FreeBlocks, arena.Failed, (unsigned long)arena.Low, (unsigned long)arena.High);
	WriteSize(out, "linear free", linearSpaceFree());
	WriteSize(out, "vram free", vramSpaceFree());
	WriteSize(out, "app region free", osGetMemRegionFree(MEMREGION_APPLICATION));

	std::fprintf(out, "\nHeap chunks%s%s\n", shape.Broken ? ": walk stopped, " : "",
		shape.Broken ? shape.Broken : "");
	if (shape.Base != 0)
	{
		std::fprintf(out, "base 0x%08lx top 0x%08lx top chunk %zu\n", (unsigned long)shape.Base,
			(unsigned long)shape.Top, shape.TopSize);
		std::fprintf(out, "%-10s %8s %12s %8s %12s\n", "up to", "free n", "free bytes", "used n", "used bytes");
		for (int i = 0; i < BucketCount; ++i)
		{
			char limit[16];
			if (i == BucketCount - 1) std::snprintf(limit, sizeof(limit), "more");
			else if (BucketLimits[i] >= 1048576) std::snprintf(limit, sizeof(limit), "%zuM", BucketLimits[i] >> 20);
			else if (BucketLimits[i] >= 1024) std::snprintf(limit, sizeof(limit), "%zuK", BucketLimits[i] >> 10);
			else std::snprintf(limit, sizeof(limit), "%zu", BucketLimits[i]);
			std::fprintf(out, "%-10s %8zu %12zu %8zu %12zu\n", limit, shape.FreeCount[i],
				shape.FreeBytes[i], shape.UsedCount[i], shape.UsedBytes[i]);
		}
		std::fprintf(out, "largest free (not counting the top chunk):");
		for (int i = 0; i < LargestKept && shape.Largest[i] != 0; ++i)
			std::fprintf(out, " %zu@+%luK", shape.Largest[i], (unsigned long)((shape.LargestAt[i] - shape.Base) >> 10));
		std::fprintf(out, "\nmap, %zu KB a cell, '#' full ... ' ' free:\n[", shape.CellBytes >> 10);
		for (int i = 0; i < MapCells; ++i)
		{
			const double free = shape.CellBytes ? double(shape.CellFree[i]) / double(shape.CellBytes) : 0.0;
			std::fputc(free >= 0.9 ? ' ' : free >= 0.6 ? '.' : free >= 0.3 ? '-' : free >= 0.05 ? '+' : '#', out);
		}
		std::fprintf(out, "]\n");
	}

	std::fprintf(out, "\nGC objects: %zu, by class (bytes from the class size, top 30):\n", objects);
	for (unsigned i = 0; i < sorted.Size() && i < 30; ++i)
		std::fprintf(out, "%-32s %7zu %10zu\n", sorted[i].Name.GetChars(), sorted[i].Count, sorted[i].Bytes);
	std::fprintf(out, "\nCached lumps: %zu\n", cachedLumps);
	std::fclose(out);

	char stage[96];
	std::snprintf(stage, sizeof(stage), "memory-census %s written", label);
	I_3DSStartupLog(stage);
}

void I_3DSMemoryCensusIfRequested(const char *label)
{
#if LOD3DS_DEBUG_TOOLS
	if (FILE *flag = std::fopen(LOD3DS_APP_DIR "/census-on-load.txt", "rb"))
	{
		std::fclose(flag);
		I_3DSMemoryCensus(label);
	}
#endif
}

#if LOD3DS_DEBUG_TOOLS
CCMD(lod3ds_memcensus)
{
	I_3DSMemoryCensus(argv.argc() > 1 ? argv[1] : "manual");
	Printf("memory census written\n");
}
#endif
