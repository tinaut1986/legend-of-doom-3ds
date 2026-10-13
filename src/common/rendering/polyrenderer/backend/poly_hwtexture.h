#pragma once

#ifdef LoadImage
#undef LoadImage
#endif

#define SHADED_TEXTURE -1
#define DIRECT_PALETTE -2

#include "tarray.h"
#include "hw_ihwtexture.h"
#include "volk/volk.h"

struct FMaterialState;
class PolyBuffer;
#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
struct F3DSGpuTexture;
#endif

class PolyHardwareTexture : public IHardwareTexture
{
public:
	PolyHardwareTexture();
	~PolyHardwareTexture();

	static void ResetAll();
	void Reset();

	DCanvas *GetImage(FTexture *tex, int translation, int flags);
	PolyDepthStencil *GetDepthStencil(FTexture *tex);

	// Software renderer stuff
	void AllocateBuffer(int w, int h, int texelsize) override;
	uint8_t *MapBuffer() override;
	unsigned int CreateTexture(unsigned char * buffer, int w, int h, int texunit, bool mipmap, const char *name) override;

	// Wipe screen
	void CreateWipeTexture(int w, int h, const char *name);

#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
	// The PICA200 copy of this image for the GPU 2D path (gpu_3ds.h), rebuilt when the
	// image changes; nullptr if it cannot be one.
	F3DSGpuTexture *GpuTexture(FTexture *tex, int translation, int flags, bool wrap);
	// For images drawn into by the renderer (camera textures).
	void MarkContentChanged() { ++mContentSerial; }
#endif

private:
	void CreateImage(FTexture *tex, int translation, int flags);

	static PolyHardwareTexture *First;
	PolyHardwareTexture *Prev = nullptr;
	PolyHardwareTexture *Next = nullptr;
	std::unique_ptr<DCanvas> mCanvas;
	std::unique_ptr<PolyDepthStencil> mDepthStencil;
#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)
	void ReleaseGpuTextures();
	F3DSGpuTexture *mGpuTexture[2] = {}; // clamped, wrapped
	uint32_t mGpuSerial[2] = {};
	uint32_t mContentSerial = 1;
#endif
};
