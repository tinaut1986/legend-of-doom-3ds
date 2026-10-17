// Large-block arena (PLAN P4.6). Blocks of LargeBlockMin bytes or more do not come from newlib's
// heap: they are carved downwards from the top of the heap region while newlib's break grows
// upwards from the bottom, so the two meet wherever the game's mix of small and large memory puts
// them. On the 2DS the heap failed with megabytes free because large transient blocks (the
// ZScript compiler's 64 KB arena blocks, decode buffers, renderer blocks, array growth) left
// holes between long-lived small blocks once freed (census, 2026-10-17). Here the large blocks
// only fragment among themselves, and freeing the lowest one hands its room back to newlib.
//
// The program is linked with -Wl,--wrap for malloc, free, realloc, calloc, memalign,
// posix_memalign, aligned_alloc and malloc_usable_size; newlib's own internal calls (_malloc_r,
// stdio buffers) stay on its heap, which is fine as long as nothing frees an arena block through
// them. The boundary is libsysbase's fake_heap_end, which _sbrk_r checks on every call.

#include <3ds.h>
#include <malloc.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "memory_3ds.h"

extern "C"
{
	extern char *fake_heap_end;
	void *_sbrk_r(struct _reent *, ptrdiff_t);

	void *__real_malloc(size_t size);
	void __real_free(void *block);
	void *__real_realloc(void *block, size_t size);
	void *__real_calloc(size_t count, size_t size);
	void *__real_memalign(size_t alignment, size_t size);
	size_t __real_malloc_usable_size(void *block);
}

namespace
{
constexpr size_t LargeBlockMin = 32 * 1024;
constexpr size_t Granule = 16;
constexpr uint32_t BlockMagic = 0x4c415247; // "LARG"

// Every arena block, used or free, starts with this header; blocks are contiguous from Low to High.
struct FArenaBlock
{
	size_t Size;		// whole block, header included, a multiple of Granule
	uint32_t Magic;
	uint32_t Used;
	uint32_t Pad;
};
static_assert(sizeof(FArenaBlock) == Granule, "arena header must keep 16-byte alignment");

LightLock ArenaLock;
bool ArenaReady;
char *Low;			// lowest block (== fake_heap_end)
char *High;			// end of the heap region
size_t UsedBytes, PeakUsedBytes, FailedRequests;

void Lock()
{
	if (!ArenaReady)
	{
		LightLock_Init(&ArenaLock);
		High = Low = fake_heap_end;
		ArenaReady = true;
	}
	LightLock_Lock(&ArenaLock);
}

void Unlock()
{
	LightLock_Unlock(&ArenaLock);
}

FArenaBlock *Next(FArenaBlock *block)
{
	return reinterpret_cast<FArenaBlock *>(reinterpret_cast<char *>(block) + block->Size);
}

bool InArena(const void *pointer)
{
	return ArenaReady && pointer >= Low && pointer < High;
}

// Header of the block a user pointer belongs to (aligned blocks keep a back offset before it).
FArenaBlock *HeaderOf(void *pointer)
{
	FArenaBlock *block = reinterpret_cast<FArenaBlock *>(static_cast<char *>(pointer) - Granule);
	if (block->Magic == BlockMagic) return block;
	const size_t back = reinterpret_cast<size_t *>(pointer)[-1];
	return reinterpret_cast<FArenaBlock *>(static_cast<char *>(pointer) - back);
}

// Moves the boundary down by `bytes` if newlib's break leaves room; the new space is one free block.
FArenaBlock *GrowDown(size_t bytes)
{
	char *brk = static_cast<char *>(_sbrk_r(_REENT, 0));
	if (brk == reinterpret_cast<char *>(-1) || Low - bytes < brk || Low - bytes > Low) return nullptr;
	char *newLow = Low - bytes;
	FArenaBlock *block = reinterpret_cast<FArenaBlock *>(newLow);
	block->Size = bytes;
	block->Magic = BlockMagic;
	block->Used = 0;
	Low = newLow;
	fake_heap_end = Low;
	// Merge with the old lowest block if it was free.
	FArenaBlock *next = Next(block);
	if (reinterpret_cast<char *>(next) < High && !next->Used) block->Size += next->Size;
	return block;
}

// Gives free room at the bottom of the arena back to newlib.
void ShrinkUp()
{
	while (Low < High)
	{
		FArenaBlock *lowest = reinterpret_cast<FArenaBlock *>(Low);
		if (lowest->Used) break;
		Low += lowest->Size;
	}
	fake_heap_end = Low;
}

void *Carve(size_t size, size_t alignment)
{
	if (alignment < Granule) alignment = Granule;
	const size_t padding = alignment > Granule ? alignment : 0;
	const size_t need = (size + Granule + padding + Granule - 1) & ~(Granule - 1);
	if (need < size) return nullptr;

	// First fit, from the top down (keeps the low end free so newlib can have it back).
	FArenaBlock *found = nullptr;
	for (FArenaBlock *block = reinterpret_cast<FArenaBlock *>(Low);
		reinterpret_cast<char *>(block) < High; block = Next(block))
	{
		if (!block->Used && block->Size >= need) found = block;
	}
	if (found == nullptr)
	{
		FArenaBlock *lowest = reinterpret_cast<FArenaBlock *>(Low);
		const size_t have = (Low < High && !lowest->Used) ? lowest->Size : 0;
		found = GrowDown(((need - have) + 4095) & ~size_t(4095));
		if (found == nullptr || found->Size < need)
		{
			if (found != nullptr) ShrinkUp();
			++FailedRequests;
			return nullptr;
		}
	}
	// Split: the used part goes at the top of the free block.
	if (found->Size >= need + LargeBlockMin)
	{
		const size_t rest = found->Size - need;
		found->Size = rest;
		found = Next(found);
		found->Size = need;
		found->Magic = BlockMagic;
	}
	found->Used = 1;
	UsedBytes += found->Size;
	if (UsedBytes > PeakUsedBytes) PeakUsedBytes = UsedBytes;

	char *user = reinterpret_cast<char *>(found) + Granule;
	if (alignment > Granule)
	{
		char *aligned = reinterpret_cast<char *>((reinterpret_cast<uintptr_t>(user) + alignment - 1) & ~(alignment - 1));
		if (aligned != user)
		{
			// Zeroed so that no stale word before `aligned` can pass for a header's magic.
			std::memset(user, 0, static_cast<size_t>(aligned - user));
			reinterpret_cast<size_t *>(aligned)[-1] = static_cast<size_t>(aligned - reinterpret_cast<char *>(found));
			user = aligned;
		}
	}
	return user;
}

void Release(void *pointer)
{
	FArenaBlock *block = HeaderOf(pointer);
	block->Used = 0;
	UsedBytes -= block->Size;
	// Coalesce with the following free block, then with the preceding one (found by a walk).
	FArenaBlock *next = Next(block);
	if (reinterpret_cast<char *>(next) < High && !next->Used) block->Size += next->Size;
	for (FArenaBlock *walk = reinterpret_cast<FArenaBlock *>(Low);
		reinterpret_cast<char *>(walk) < reinterpret_cast<char *>(block); walk = Next(walk))
	{
		if (Next(walk) == block)
		{
			if (!walk->Used) walk->Size += block->Size;
			break;
		}
	}
	ShrinkUp();
}

size_t Capacity(void *pointer)
{
	FArenaBlock *block = HeaderOf(pointer);
	return block->Size - static_cast<size_t>(static_cast<char *>(pointer) - reinterpret_cast<char *>(block));
}

void *AllocateLarge(size_t size, size_t alignment)
{
	Lock();
	void *pointer = Carve(size, alignment);
	Unlock();
	return pointer;
}
}

extern "C"
{
void *__wrap_malloc(size_t size)
{
	if (size >= LargeBlockMin)
	{
		if (void *pointer = AllocateLarge(size, Granule)) return pointer;
	}
	return __real_malloc(size);
}

void __wrap_free(void *block)
{
	if (block == nullptr) return;
	if (InArena(block))
	{
		Lock();
		Release(block);
		Unlock();
		return;
	}
	__real_free(block);
}

void *__wrap_calloc(size_t count, size_t size)
{
	const size_t total = count * size;
	if (size != 0 && total / size != count) return nullptr;
	if (total >= LargeBlockMin)
	{
		if (void *pointer = AllocateLarge(total, Granule))
		{
			std::memset(pointer, 0, total);
			return pointer;
		}
	}
	return __real_calloc(count, size);
}

size_t __wrap_malloc_usable_size(void *block)
{
	if (block != nullptr && InArena(block))
	{
		Lock();
		const size_t capacity = Capacity(block);
		Unlock();
		return capacity;
	}
	return __real_malloc_usable_size(block);
}

void *__wrap_realloc(void *block, size_t size)
{
	if (block == nullptr) return __wrap_malloc(size);
	if (size == 0)
	{
		__wrap_free(block);
		return nullptr;
	}
	const bool inArena = InArena(block);
	if (!inArena && size < LargeBlockMin) return __real_realloc(block, size);

	const size_t have = inArena ? __wrap_malloc_usable_size(block) : __real_malloc_usable_size(block);
	if (inArena && size <= have) return block;
	void *moved = __wrap_malloc(size);
	if (moved == nullptr) return nullptr;
	std::memcpy(moved, block, have < size ? have : size);
	__wrap_free(block);
	return moved;
}

void *__wrap_memalign(size_t alignment, size_t size)
{
	if (size >= LargeBlockMin && alignment <= 4096)
	{
		if (void *pointer = AllocateLarge(size, alignment)) return pointer;
	}
	return __real_memalign(alignment, size);
}

int __wrap_posix_memalign(void **result, size_t alignment, size_t size)
{
	void *pointer = __wrap_memalign(alignment, size);
	if (pointer == nullptr) return ENOMEM;
	*result = pointer;
	return 0;
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
	return __wrap_memalign(alignment, size);
}
}

void I_3DSLargeArenaStats(FLargeArenaStats &stats)
{
	stats = FLargeArenaStats{};
	if (!ArenaReady) return;
	Lock();
	stats.Low = reinterpret_cast<uintptr_t>(Low);
	stats.High = reinterpret_cast<uintptr_t>(High);
	stats.Used = UsedBytes;
	stats.Peak = PeakUsedBytes;
	stats.Failed = FailedRequests;
	for (FArenaBlock *block = reinterpret_cast<FArenaBlock *>(Low);
		reinterpret_cast<char *>(block) < High; block = Next(block))
	{
		if (block->Used) ++stats.UsedBlocks;
		else
		{
			++stats.FreeBlocks;
			stats.Free += block->Size;
			if (block->Size > stats.LargestFree) stats.LargestFree = block->Size;
		}
	}
	char *brk = static_cast<char *>(_sbrk_r(_REENT, 0));
	stats.Gap = brk != reinterpret_cast<char *>(-1) && Low > brk ? static_cast<size_t>(Low - brk) : 0;
	Unlock();
}
