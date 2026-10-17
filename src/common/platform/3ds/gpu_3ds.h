#pragma once
// The PICA200 side of the hybrid profile (PLAN U2), compiled only with LOD3DS_HYBRID_PERFORMANCE.
//
// One citro3d context, one small vertex shader (gpu_3ds.v.pica) and a render target per LCD.
// Each presented frame draws on the top screen, in order:
//   1. the SoftPoly canvas (the 3D view, at the render scale), stretched with bilinear filtering;
//   2. the engine's 2D (title pictures, HUD, menus drawn by the engine, wipes), recorded from
//      F2DDrawer during the frame and drawn at the LCD's resolution;
//   3. the port's overlays (FPS counter, crosshair, the top automap) at the LCD's resolution.
// and on the bottom screen, when it changed or has GPU draws:
//   1. the interface the CPU draws (diagnostics_3ds.cpp), from a buffer in the framebuffer's own
//      layout, copied by the GPU's DMA only when it changed;
//   2. the draws queued for it this frame (the map's terrain, lines and player arrow).
// The engine 2D falls back to SoftPoly for a drawer that uses something the GPU path does not
// do (see I_3DSGpuRecord2D) and whenever PolyFrameBuffer needs the 2D inside the canvas pixels.
//
// Everything here runs on the main thread.
#include <cstddef>
#include <cstdint>

class F2DDrawer;
class DCanvas;

// Presenter lifetime (sdlglvideo.cpp). Init after gfxInit; false leaves the SDL path in charge.
bool I_3DSGpuInit();
void I_3DSGpuShutdown();
bool I_3DSGpuReady();
// Waits until the GPU has finished the last submitted frame.
void I_3DSGpuWaitIdle();

// Engine 2D. Record converts the drawer's commands into GPU draws for the frame being built and
// returns true; false means nothing was recorded and the caller must draw it in software.
bool I_3DSGpuRecord2D(F2DDrawer *drawer);
// Drops whatever was recorded for a frame that will not be presented through the GPU.
void I_3DSGpuDiscard2D();
// Counters for the developer overlay and the perf notes: 2D drawers recorded and sent to
// software since start, and GPU draws in the last presented frame.
struct F3DSGpuStats
{
	uint32_t Recorded2D;
	uint32_t Software2D;
	uint32_t LastDraws;
	uint32_t LastVertices;
	uint32_t TextureBytes;
	uint32_t Textures;
};
F3DSGpuStats I_3DSGpuGetStats();
void I_3DSGpuCountSoftware2D();

// Presents the canvas (BGRA rows, at most 400x240) with the recorded 2D and the overlays.
bool I_3DSGpuPresentTop(const uint8_t *pixels, int pitchBytes, int width, int height);

// Copies the last presented top image into a BGRA canvas of width x height (scaled), for the
// wipes. flipRows stores the image bottom-up, as PolyHardwareTexture's wipe textures are.
bool I_3DSGpuReadTop(uint8_t *pixels, int pitchPixels, int width, int height, bool flipRows);

// The screen wipe on the GPU (#18): keeps the image the LCD shows now and fades it out over the
// frames presented in the next durationMilliseconds, the game running underneath. False when
// it cannot (no GPU frame yet, no linear memory): the engine's own wipe then runs.
bool I_3DSGpuWipeBegin(uint32_t durationMilliseconds);

// GPU copies of engine textures, owned by PolyHardwareTexture (one per wrap mode).
struct F3DSGpuTexture;
// Builds or refreshes the copy of image; nullptr if the image cannot be a PICA texture.
F3DSGpuTexture *I_3DSGpuTextureUpdate(F3DSGpuTexture *texture, DCanvas *image, bool wrap);
// Frees it once the GPU is done with the frames that may still use it.
void I_3DSGpuTextureRelease(F3DSGpuTexture *texture);

// The bottom screen. Buffer: where the CPU draws its interface, 240x320 in the LCD
// framebuffer's layout (one 240-pixel column per screen x, bottom first, bytes A, B, G, R);
// nullptr while the GPU does not own the bottom screen (draw into the framebuffer then).
// Changed: after drawing into it. With no frame presented for a while (a dump, a level load)
// it presents the bottom screen on its own.
unsigned char *I_3DSGpuBottomBuffer();
void I_3DSGpuBottomChanged();

// Starts the bottom screen's GPU draws of a new tic: they replace the previous tic's on the next
// presented frame, which stay on screen until then (frames come faster than tics).
void I_3DSGpuBottomNewTic();

// Draws queued on either screen, after everything else there (top: this frame; bottom: this
// tic, see I_3DSGpuBottomNewTic), in that screen's pixels (400x240 or 320x240).
// Color: bytes R, G, B, A (I_3DSGpuColor).
enum class E3DSGpuScreen { Top, Bottom };
struct F3DSGpuVertex
{
	float X, Y;
	float S, T; // PICA convention: t = 1 at the image top
	uint32_t Color;
};
inline uint32_t I_3DSGpuColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		(static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}
// Room for count vertices of a triangle list, textured with texture (nullptr: vertex colour
// only) and clipped to scissor (left, top, right, bottom; nullptr: none); nullptr when full.
F3DSGpuVertex *I_3DSGpuQueueTriangles(E3DSGpuScreen screen, F3DSGpuTexture *texture,
	int count, const int *scissor);
// The GPU copy of an engine texture, untranslated (the map's floors).
class FGameTexture;
F3DSGpuTexture *I_3DSGpuGameTexture(FGameTexture *texture, bool wrap);

// Small retained RGBA images for the overlays, uploaded when their pixels change.
struct F3DSGpuImage;
F3DSGpuImage *I_3DSGpuImageCreate(int width, int height);
// rgba: rows top to bottom, one uint32_t per pixel as 0xRRGGBBAA.
void I_3DSGpuImageUpload(F3DSGpuImage *image, const uint32_t *rgba, int pitchPixels);
// Queued for this frame's overlay pass, in top-screen pixels (400x240).
void I_3DSGpuOverlayImage(F3DSGpuImage *image, float x, float y, float width, float height);
void I_3DSGpuOverlayRect(float x, float y, float width, float height, uint32_t rgba);

// The 3D world drawn by the PICA200 (P2.3 spike, docs/gpu-renderer-design.md): when a frame has
// world draws, they are drawn with depth in place of the canvas, under the 2D and the overlays.
// Matrix: world (x, y, z, 1) to clip space for an upright 400x240 screen, row-major, depth z/w
// in [-1, 0] from near to far; the LCD's rotation is applied here. With rightMatrix (the right
// eye's; matrix is then the left eye's) the frame is drawn in stereo: the top screen switches
// to 3D for it and the 2D is drawn at the screen's depth in both eyes.
// Colour: R, G, B multiply the texture (the light); A is how much of the texel is kept against
// the frame's fog colour (255: none of the fog, the MAP01 distance fog lowers it).
struct F3DSGpuWorldVertex
{
	float X, Y, Z;
	float S, T; // PICA convention: t = 1 at the image top
	uint32_t Color; // bytes R, G, B, A
};
// clearColor and fogColor: 0xRRGGBBAA.
void I_3DSGpuWorldBegin(const float matrix[16], const float *rightMatrix, uint32_t clearColor,
	uint32_t fogColor = 0x000000ff);
// Room for count vertices of a triangle list textured with texture (nullptr: vertex colour);
// nullptr when the frame's world arena is full. Masked: fully transparent texels are dropped
// (sprites). Sky: drawn before everything else, with no depth test or write (the background).
F3DSGpuWorldVertex *I_3DSGpuWorldQueue(F3DSGpuTexture *texture, int count, bool masked = false,
	bool sky = false);
// A see-through sprite (masked), drawn after every opaque draw in the order queued (far to
// near), depth-tested but not writing depth: Translucent mixes it in by alpha (0-255), Add adds
// it times alpha, Stencil draws its shape in the vertex colour, mixed in by alpha.
enum class E3DSGpuWorldStyle : uint8_t { Translucent, Add, Stencil };
F3DSGpuWorldVertex *I_3DSGpuWorldQueueBlended(F3DSGpuTexture *texture, int count,
	E3DSGpuWorldStyle style, uint8_t alpha);
// The part of a texture's copy the image fills (it is padded to powers of two unless wrapped).
void I_3DSGpuTextureScale(const F3DSGpuTexture *texture, float &scaleS, float &scaleT);
// GPU time of the last frame (citro3d's drawing time), in ms.
float I_3DSGpuLastDrawingTime();
