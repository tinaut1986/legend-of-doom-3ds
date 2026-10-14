#pragma once
// The 3D world on the PICA200, P2.3 spike (gpu_world_3ds.cpp).

namespace swrenderer { class RenderThread; }

class FGameTexture;

// Before the swrenderer's opaque pass of the main view: decides whether the GPU draws this
// frame's world (always, unless lod3ds_gpu_world is turned off in a debug build), so that the
// walk can hand it the sprites.
void I_3DSGpuWorldBeginScene(swrenderer::RenderThread *thread);
// A sprite the walk projected (RenderSprite::Project): its billboard from (ax, ay) to (bx, by)
// in map units, bottom to top, the image mirrored or not, its sector's light level.
void I_3DSGpuWorldAddSprite(FGameTexture *texture, double ax, double ay, double bx, double by,
	double bottom, double top, bool flipX, int lightLevel);

// After the swrenderer's opaque pass of the main view: builds what it found visible for the
// GPU (always, unless lod3ds_gpu_world is turned off in a debug build).
void I_3DSGpuWorldCapture(swrenderer::RenderThread *thread);
// True while the GPU draws the world of the frame being rendered: the swrenderer then only
// walks the scene (its visibility) and draws no walls, planes, sky or translucent pass.
bool I_3DSGpuWorldActive();
