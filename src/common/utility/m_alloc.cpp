/*
** m_alloc.cpp
** Wrappers for the malloc family of functions that count used bytes.
**
**---------------------------------------------------------------------------
** Copyright 1998-2008 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#if defined(__FreeBSD__)
#include <stdlib.h>
#include <malloc_np.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#include <malloc/malloc.h>
#elif defined(__OpenBSD__) || defined(__DragonFly__)
#include <stdlib.h>
#else
#include <malloc.h>
#endif

#include "engineerrors.h"
#include "dobject.h"
#ifdef __3DS__
#include <3ds.h>
#include "printf.h"
#include "common/platform/3ds/diagnostics_3ds.h"

size_t M_3DSPoolUsed(size_t *high);

void M_3DSLogHeap(const char *stage)
{
	const struct mallinfo heap = mallinfo();
	Printf(PRINT_LOG, "[3DS memory] %s: used=%d arena=%d free=%d engine=%zu linear=%lu\n",
		stage, heap.uordblks, heap.arena, heap.fordblks, GC::AllocBytes,
		(unsigned long)linearSpaceFree());
	char full[160];
	size_t high = 0;
	const size_t used = M_3DSPoolUsed(&high);
	snprintf(full, sizeof(full), "%s pool=%zu/%zu", stage, used, high);
	I_3DSStartupLog(full);
}

// caller: the return address in M_Malloc/M_Realloc, for addr2line on the build's ELF.
static void M_FatalOutOfMemory(const char *operation, size_t size, void *caller)
{
	const struct mallinfo heap = mallinfo();
	I_FatalError("Could not %s %zu bytes (caller 0x%08lx; heap %d/%d free %d, app %lu/%lu, linear %lu)",
		operation, size, (unsigned long)(uintptr_t)caller,
		heap.uordblks, heap.arena, heap.fordblks,
		(unsigned long)osGetMemRegionFree(MEMREGION_APPLICATION),
		(unsigned long)osGetMemRegionSize(MEMREGION_APPLICATION),
		(unsigned long)linearSpaceFree());
}
#endif

#ifndef _MSC_VER
#define _NORMAL_BLOCK			0
#define _malloc_dbg(s,b,f,l)	malloc(s)
#define _realloc_dbg(p,s,b,f,l)	realloc(p,s)
#endif

#ifndef _DEBUG
#if !defined(__solaris__) && !defined(__OpenBSD__) && !defined(__DragonFly__)
void *M_Malloc(size_t size)
{
	void *block = malloc(size);

	if (block == NULL)
#ifdef __3DS__
		M_FatalOutOfMemory("malloc", size, __builtin_return_address(0));
#else
		I_FatalError("Could not malloc %zu bytes", size);
#endif

	GC::AllocBytes += _msize(block);
	return block;
}

void *M_Realloc(void *memblock, size_t size)
{
	if (memblock != NULL)
	{
		GC::AllocBytes -= _msize(memblock);
	}
	void *block = realloc(memblock, size);
#ifdef __3DS__
	// Large array growth in startup.log with its caller (addr2line on the ELF): these
	// multi-megabyte blocks are what a fragmented heap fails to find.
	if (size >= 256u * 1024u && block != NULL)
	{
		char stage[64];
		snprintf(stage, sizeof(stage), "realloc %zu caller 0x%08lx", size,
			(unsigned long)(uintptr_t)__builtin_return_address(0));
		I_3DSStartupLog(stage);
	}
#endif
	if (block == NULL)
	{
#ifdef __3DS__
		M_FatalOutOfMemory("realloc", size, __builtin_return_address(0));
#else
		I_FatalError("Could not realloc %zu bytes", size);
#endif
	}
	GC::AllocBytes += _msize(block);
	return block;
}
#else
void *M_Malloc(size_t size)
{
	void *block = malloc(size+sizeof(size_t));

	if (block == NULL)
		I_FatalError("Could not malloc %zu bytes", size);

	size_t *sizeStore = (size_t *) block;
	*sizeStore = size;
	block = sizeStore+1;

	GC::AllocBytes += _msize(block);
	return block;
}

void *M_Realloc(void *memblock, size_t size)
{
	if(memblock == NULL)
		return M_Malloc(size);

	if (memblock != NULL)
	{
		GC::AllocBytes -= _msize(memblock);
	}
	void *block = realloc(((size_t*) memblock)-1, size+sizeof(size_t));
	if (block == NULL)
	{
		I_FatalError("Could not realloc %zu bytes", size);
	}

	size_t *sizeStore = (size_t *) block;
	*sizeStore = size;
	block = sizeStore+1;

	GC::AllocBytes += _msize(block);
	return block;
}
#endif
#else
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#if !defined(__solaris__) && !defined(__OpenBSD__) && !defined(__DragonFly__)
void *M_Malloc_Dbg(size_t size, const char *file, int lineno)
{
	void *block = _malloc_dbg(size, _NORMAL_BLOCK, file, lineno);

	if (block == NULL)
		I_FatalError("Could not malloc %zu bytes in %s, line %d", size, file, lineno);

	GC::AllocBytes += _msize(block);
	return block;
}

void *M_Realloc_Dbg(void *memblock, size_t size, const char *file, int lineno)
{
	if (memblock != NULL)
	{
		GC::AllocBytes -= _msize(memblock);
	}
	void *block = _realloc_dbg(memblock, size, _NORMAL_BLOCK, file, lineno);
	if (block == NULL)
	{
		I_FatalError("Could not realloc %zu bytes in %s, line %d", size, file, lineno);
	}
	GC::AllocBytes += _msize(block);
	return block;
}
#else
void *M_Malloc_Dbg(size_t size, const char *file, int lineno)
{
	void *block = _malloc_dbg(size+sizeof(size_t), _NORMAL_BLOCK, file, lineno);

	if (block == NULL)
		I_FatalError("Could not malloc %zu bytes in %s, line %d", size, file, lineno);

	size_t *sizeStore = (size_t *) block;
	*sizeStore = size;
	block = sizeStore+1;

	GC::AllocBytes += _msize(block);
	return block;
}

void *M_Realloc_Dbg(void *memblock, size_t size, const char *file, int lineno)
{
	if(memblock == NULL)
		return M_Malloc_Dbg(size, file, lineno);

	if (memblock != NULL)
	{
		GC::AllocBytes -= _msize(memblock);
	}
	void *block = _realloc_dbg(((size_t*) memblock)-1, size+sizeof(size_t), _NORMAL_BLOCK, file, lineno);

	if (block == NULL)
	{
		I_FatalError("Could not realloc %zu bytes in %s, line %d", size, file, lineno);
	}

	size_t *sizeStore = (size_t *) block;
	*sizeStore = size;
	block = sizeStore+1;

	GC::AllocBytes += _msize(block);
	return block;
}
#endif
#endif

#if !defined(__solaris__) && !defined(__OpenBSD__) && !defined(__DragonFly__)
#ifdef __3DS__
namespace { bool InPool(const void *p); void PoolFree(void *p); }
void M_Free (void *block)
{
	if (block != NULL)
	{
		if (InPool(block)) { PoolFree(block); return; }
		GC::AllocBytes -= _msize(block);
		free(block);
	}
}
#else
void M_Free (void *block)
{
	if (block != NULL)
	{
		GC::AllocBytes -= _msize(block);
		free(block);
	}
}
#endif
#else
void M_Free (void *block)
{
	if(block != NULL)
	{
		GC::AllocBytes -= _msize(block);
		free(((size_t*) block)-1);
	}
}
#endif

#ifdef __3DS__
// Large-array pool ------------------------------------------------------------------------------
// The 2DS gives the game a ~55 MB heap and map loading asks for several multi-megabyte arrays at
// once; by then the heap holds 10 MB free in pieces smaller than 0.5 MB and the request fails
// (MAP01, 2026-10-15). TArray's big blocks come from a pool taken at the first allocation instead,
// as a first-fit list with coalescing. A block that does not fit the pool falls back to the heap.
namespace
{
	constexpr size_t PoolBytes = 10u * 1024u * 1024u;
	constexpr size_t PoolMin = 192u * 1024u;		// smaller requests stay on the heap
	struct PoolHeader { size_t size; size_t used; size_t pad0, pad1; };	// 16 bytes
	char *Pool = nullptr;
	bool PoolTried = false;
	size_t PoolHigh = 0;

	void PoolInit()
	{
		if (PoolTried) return;
		PoolTried = true;
		// No pool: the large-block arena (P4.6, large_arena_3ds.cpp) keeps every block of 32 KB or
		// more apart from the small ones, which is what the pool did for TArray's big blocks.
		constexpr bool UsePool = false;
		if (!UsePool) return;
		Pool = (char *)malloc(PoolBytes);
		if (Pool != nullptr)
		{
			PoolHeader *h = (PoolHeader *)Pool;
			h->size = PoolBytes;
			h->used = 0;
		}
	}

	bool InPool(const void *p) { return Pool != nullptr && (const char *)p > Pool && (const char *)p < Pool + PoolBytes; }

	size_t PoolUsed()
	{
		size_t used = 0;
		for (char *c = Pool; c < Pool + PoolBytes; c += ((PoolHeader *)c)->size)
			if (((PoolHeader *)c)->used) used += ((PoolHeader *)c)->size;
		return used;
	}

	void PoolCoalesce()
	{
		for (char *c = Pool; c < Pool + PoolBytes;)
		{
			PoolHeader *h = (PoolHeader *)c;
			char *n = c + h->size;
			if (!h->used && n < Pool + PoolBytes && !((PoolHeader *)n)->used)
				h->size += ((PoolHeader *)n)->size;
			else
				c = n;
		}
	}

	void *PoolAlloc(size_t size)
	{
		const size_t need = ((size + 15) & ~(size_t)15) + sizeof(PoolHeader);
		for (char *c = Pool; c < Pool + PoolBytes; c += ((PoolHeader *)c)->size)
		{
			PoolHeader *h = (PoolHeader *)c;
			if (h->used || h->size < need) continue;
			if (h->size - need >= 64)
			{
				PoolHeader *rest = (PoolHeader *)(c + need);
				rest->size = h->size - need;
				rest->used = 0;
				h->size = need;
			}
			h->used = 1;
			const size_t used = PoolUsed();
			if (used > PoolHigh) PoolHigh = used;
			return c + sizeof(PoolHeader);
		}
		return nullptr;
	}

	size_t PoolSize(void *p) { return ((PoolHeader *)((char *)p - sizeof(PoolHeader)))->size - sizeof(PoolHeader); }

	void PoolFree(void *p)
	{
		((PoolHeader *)((char *)p - sizeof(PoolHeader)))->used = 0;
		PoolCoalesce();
	}
}

size_t M_3DSPoolUsed(size_t *high)
{
	if (high != nullptr) *high = PoolHigh;
	return Pool != nullptr ? PoolUsed() : 0;
}

void *M_ReallocBig(void *memblock, size_t size)
{
	PoolInit();
	if (Pool == nullptr) return M_Realloc(memblock, size);

	if (memblock != nullptr && InPool(memblock))
	{
		const size_t have = PoolSize(memblock);
		if (size <= have) return memblock;	// the pool does not give memory back by shrinking
		void *nb = PoolAlloc(size);
		if (nb == nullptr)
		{
			// Pool full: move the array out to the heap (its content is copied there).
			void *hb = malloc(size);
			if (hb == nullptr) M_FatalOutOfMemory("realloc", size, __builtin_return_address(0));
			memcpy(hb, memblock, have);
			PoolFree(memblock);
			GC::AllocBytes += _msize(hb);
			return hb;
		}
		memcpy(nb, memblock, have);
		PoolFree(memblock);
		return nb;
	}
	if (size >= PoolMin)
	{
		void *nb = PoolAlloc(size);
		if (nb != nullptr)
		{
			if (memblock != nullptr)
			{
				const size_t have = _msize(memblock);
				memcpy(nb, memblock, have < size ? have : size);
				M_Free(memblock);
			}
			return nb;
		}
		char stage[64];
		snprintf(stage, sizeof(stage), "pool full: %zu (high %zu)", size, PoolHigh);
		I_3DSStartupLog(stage);
	}
	return M_Realloc(memblock, size);
}
#endif
