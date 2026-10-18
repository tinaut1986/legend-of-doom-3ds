#include <3ds.h>
#include <cstdlib>
#include <cstdint>
#include <malloc.h>
#include <new>
#include <typeinfo>

#include <cstdio>

#include "memory_3ds.h"
#include "diagnostics_3ds.h"

static size_t LastFailedNewRequest;
static uintptr_t LastFailedNewCaller;

namespace
{
constexpr u32 PageMask = 0xFFF;
constexpr u32 HeapRetryStepBytes = 4 * 1024 * 1024;
constexpr u32 MinimumUsableHeapBytes = 24 * 1024 * 1024;
constexpr u32 LinearHeapCapacityBytes = 32 * 1024 * 1024;
// What the GPU path needs in linear memory (render targets, textures, vertex arenas, audio): the
// New 3DS gets ~18 MB and uses ~9. On the Old 3DS (80 MB mode) the heap would otherwise take all
// but 4 MB and the GPU presenter falls back to SDL (2DS, 2026-10-14).
constexpr u32 LinearHeapTargetBytes = 12 * 1024 * 1024;
constexpr u32 ConventionalHeapAddressCapacity = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
constexpr u32 ConventionalHeapSafetyMargin = 4 * 1024 * 1024;
constexpr u32 ConventionalHeapMaximum =
	ConventionalHeapAddressCapacity - ConventionalHeapSafetyMargin;

#if defined(LOD3DS_SAFE_SOFTWARE)
constexpr u32 MinimumLinearHeapBytes = 4 * 1024 * 1024;
#else
constexpr u32 MinimumLinearHeapBytes = 32 * 1024 * 1024;
#endif

static_assert(ConventionalHeapAddressCapacity == 96 * 1024 * 1024,
	"Unexpected libctru conventional-heap address window");
static_assert(ConventionalHeapMaximum == 92 * 1024 * 1024,
	"Unexpected guarded conventional-heap maximum");
}

#if defined(LOD3DS_SAFE_SOFTWARE)
namespace
{
constexpr size_t RendererMemoryReserveBytes = 2 * 1024 * 1024;
void *RendererMemoryReserve;
}

bool I_3DSReserveRendererMemory()
{
	if (RendererMemoryReserve == nullptr)
	{
		RendererMemoryReserve = std::malloc(RendererMemoryReserveBytes);
	}
	return RendererMemoryReserve != nullptr;
}

void I_3DSReleaseRendererMemory()
{
	std::free(RendererMemoryReserve);
	RendererMemoryReserve = nullptr;
}
#else
bool I_3DSReserveRendererMemory()
{
	return true;
}

void I_3DSReleaseRendererMemory()
{
}
#endif

namespace
{
constexpr size_t MapLoadReserveBytes = 5 * 1024 * 1024; // 8 MB: ZScript ran out on the 2DS
void *MapLoadReserve;
}

bool I_3DSReserveMapLoadMemory()
{
	// The large-block arena (P4.6) keeps map loading's big blocks apart from the small ones;
	// a held 5 MB block would only sit in it.
	(void)MapLoadReserveBytes;
	return true;
}

bool I_3DSMapLoadReserveHeld()
{
	return MapLoadReserve != nullptr;
}

void I_3DSReleaseMapLoadMemory()
{
	std::free(MapLoadReserve);
	MapLoadReserve = nullptr;
}

static void *AllocateWithNewHandler(size_t size, uintptr_t caller)
{
	if (size == 0) size = 1;
	for (;;)
	{
		I_3DSAllocCaller = caller;
		void *fresh = std::malloc(size);
		I_3DSAllocCaller = 0;
		if (fresh != nullptr) return fresh;
		// The map-load reserve is there to be handed back when a large block finds no room; on the
		// 2DS a new game read a 2 MB lump before P_SetupLevel releases it (largest free block
		// 1.7 MB of 10.5 MB free, 2026-10-17).
		if (MapLoadReserve != nullptr)
		{
			I_3DSReleaseMapLoadMemory();
			char stage[80];
			std::snprintf(stage, sizeof(stage), "map-load reserve released for %zu bytes caller 0x%08lx",
				size, (unsigned long)caller);
			I_3DSStartupLog(stage);
			continue;
		}
		LastFailedNewRequest = size;
		LastFailedNewCaller = caller;
		auto handler = std::get_new_handler();
		if (handler == nullptr) throw std::bad_alloc();
		handler();
	}
}

void *operator new(size_t size)
{
	return AllocateWithNewHandler(size, (uintptr_t)__builtin_return_address(0));
}

void *operator new[](size_t size)
{
	return AllocateWithNewHandler(size, (uintptr_t)__builtin_return_address(0));
}

size_t I_3DSLargestFreeBlock()
{
	size_t low = 0, high = (size_t)mallinfo().fordblks;
	while (high - low > 4096)
	{
		const size_t mid = low + (high - low) / 2;
		void *block = std::malloc(mid);
		// Without this GCC drops the malloc/free pair and folds the test to "succeeded".
		__asm__ volatile("" : "+r"(block) : : "memory");
		if (block != nullptr)
		{
			std::free(block);
			low = mid;
		}
		else high = mid;
	}
	return low;
}

bool I_3DSIsOld3DS()
{
	bool isNew3DS = true;
	APT_CheckNew3DS(&isNew3DS);
	return !isNew3DS;
}

size_t I_3DSLastFailedNewRequest()
{
	return LastFailedNewRequest;
}

uintptr_t I_3DSLastFailedNewCaller()
{
	return LastFailedNewCaller;
}

extern "C"
{
	extern u32 __ctru_heap;
	extern u32 __ctru_linear_heap;
	extern char *fake_heap_start;
	extern char *fake_heap_end;

	// Expose the initial linear minimum until heap allocation completes.
	u32 __ctru_heap_size = 0;
	#ifdef LOD3DS_SAFE_SOFTWARE
	u32 __ctru_linear_heap_size = 4 * 1024 * 1024;
	#else
	u32 __ctru_linear_heap_size = 32 * 1024 * 1024;
	#endif

	// Size heaps from the granted limit; keep the conventional heap below 96 MiB.
	void __system_allocateHeaps(void)
	{
		Handle resourceLimit = 0;
		Result result = svcGetResourceLimit(&resourceLimit, CUR_PROCESS_HANDLE);
		if (R_FAILED(result))
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		s64 maximumCommit = 0;
		s64 currentCommit = 0;
		ResourceLimitType resourceType = RESLIMIT_COMMIT;
		result = svcGetResourceLimitLimitValues(
			&maximumCommit, resourceLimit, &resourceType, 1);
		if (R_SUCCEEDED(result))
		{
			result = svcGetResourceLimitCurrentValues(
				&currentCommit, resourceLimit, &resourceType, 1);
		}
		svcCloseHandle(resourceLimit);

		if (R_FAILED(result) || maximumCommit <= currentCommit)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		const u32 remaining = static_cast<u32>(maximumCommit - currentCommit) & ~PageMask;
		if (remaining <= MinimumLinearHeapBytes + MinimumUsableHeapBytes)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}

		u32 heapBytes = remaining - MinimumLinearHeapBytes;
		if (remaining > LinearHeapTargetBytes + MinimumUsableHeapBytes &&
			heapBytes > remaining - LinearHeapTargetBytes)
		{
			heapBytes = remaining - LinearHeapTargetBytes;
		}
		if (heapBytes > ConventionalHeapMaximum)
		{
			heapBytes = ConventionalHeapMaximum;
		}
		heapBytes &= ~PageMask;

		// Keep a 4 MiB guard and retry smaller heaps when needed.
		for (;;)
		{
			__ctru_heap = 0;
			result = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0,
				heapBytes, MEMOP_ALLOC, static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE));
			if (R_SUCCEEDED(result)) break;
			if (heapBytes < MinimumUsableHeapBytes + HeapRetryStepBytes)
			{
				svcBreak(USERBREAK_PANIC);
				return;
			}
			heapBytes -= HeapRetryStepBytes;
		}
		__ctru_heap_size = heapBytes;

		// Bound remaining linear memory to the libctru 32 MiB limit.
		u32 linearBytes = remaining - heapBytes;
		if (linearBytes > LinearHeapCapacityBytes)
		{
			linearBytes = LinearHeapCapacityBytes;
		}
		linearBytes &= ~PageMask;
		if (linearBytes < MinimumLinearHeapBytes)
		{
			svcBreak(USERBREAK_PANIC);
			return;
		}
		// Contiguous linear memory can be scarcer than the commit counter implies.
		// If the optional remainder is unavailable, retry down to the measured
		// minimum instead of turning that nonessential headroom into another
		// pre-main panic.
		for (;;)
		{
			__ctru_linear_heap = 0;
			result = svcControlMemory(&__ctru_linear_heap, 0, 0,
				linearBytes, MEMOP_ALLOC_LINEAR,
				static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE));
			if (R_SUCCEEDED(result)) break;
			if (linearBytes == MinimumLinearHeapBytes)
			{
				svcBreak(USERBREAK_PANIC);
				return;
			}
			linearBytes = linearBytes > MinimumLinearHeapBytes + HeapRetryStepBytes
				? linearBytes - HeapRetryStepBytes : MinimumLinearHeapBytes;
		}
		__ctru_linear_heap_size = linearBytes;

		mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
		fake_heap_start = reinterpret_cast<char *>(__ctru_heap);
		fake_heap_end = fake_heap_start + __ctru_heap_size;
	}
}

// Linked with -Wl,--wrap=__cxa_throw: a std::bad_alloc that does not come through the new handler
// (the 2DS ended with a bare "std::bad_alloc" in fatal.log, 2026-10-17) logs the code addresses on
// its stack, for addr2line on the build's ELF.
extern "C" [[noreturn]] void __real___cxa_throw(void *object, std::type_info *type,
	void (*destructor)(void *));

extern "C" [[noreturn]] void __wrap___cxa_throw(void *object, std::type_info *type,
	void (*destructor)(void *))
{
	if (type != nullptr && *type == typeid(std::bad_alloc))
	{
		char stage[200];
		int length = std::snprintf(stage, sizeof(stage), "bad_alloc thrown, stack:");
		const uintptr_t *stack = static_cast<const uintptr_t *>(__builtin_frame_address(0));
		int found = 0;
		for (int i = 0; i < 512 && found < 14 && length < static_cast<int>(sizeof(stage)) - 12; ++i)
		{
			const uintptr_t word = stack[i];
			if (word < 0x00100000u || word >= 0x00a00000u) continue;
			length += std::snprintf(stage + length, sizeof(stage) - length, " %lx",
				(unsigned long)word);
			++found;
		}
		I_3DSStartupLog(stage);
	}
	__real___cxa_throw(object, type, destructor);
}
