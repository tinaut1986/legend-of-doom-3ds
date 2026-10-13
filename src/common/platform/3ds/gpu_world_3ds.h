#pragma once
// The 3D world on the PICA200, P2.3 spike (gpu_world_3ds.cpp).

namespace swrenderer { class RenderThread; }

// After the swrenderer's opaque pass of the main view: builds what it found visible for the
// GPU when lod3ds_gpu_world is on (debug builds).
void I_3DSGpuWorldCapture(swrenderer::RenderThread *thread);
// True while the GPU draws the world of the frame being rendered: the swrenderer then only
// walks the scene (its visibility) and draws no walls, planes, sky or translucent pass.
bool I_3DSGpuWorldActive();
