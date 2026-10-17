#pragma once

#include <memory>
#include <vector>

// Memory needed for the duration of a frame rendering
class RenderMemory
{
public:
	void Clear();
		
	template<typename T>
	T *AllocMemory(int size = 1)
	{
		return (T*)AllocBytes(sizeof(T) * size);
	}
		
	template<typename T, typename... Types>
	T *NewObject(Types &&... args)
	{
		void *ptr = AllocBytes(sizeof(T));
		return new (ptr)T(std::forward<Types>(args)...);
	}
		
private:
	void *AllocBytes(int size);
		
#ifdef __3DS__
	// 256 KB: a 1 MB block found no room on the 2DS's fragmented heap with 8.6 MB free (2026-10-17).
	enum { BlockSize = 256 * 1024 };
#else
	enum { BlockSize = 1024 * 1024 };
#endif
		
	struct MemoryBlock
	{
		MemoryBlock();
		~MemoryBlock();
			
		MemoryBlock(const MemoryBlock &) = delete;
		MemoryBlock &operator=(const MemoryBlock &) = delete;
			
		uint8_t *Data;
		uint32_t Position;
	};
	std::vector<std::unique_ptr<MemoryBlock>> UsedBlocks;
	std::vector<std::unique_ptr<MemoryBlock>> FreeBlocks;
};
