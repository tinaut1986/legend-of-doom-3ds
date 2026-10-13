// The PICA200 side of the hybrid profile (PLAN U2); see gpu_3ds.h for the frame layout.
#include "gpu_3ds.h"

#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "v_2ddrawer.h"
#include "v_video.h"
#include "hw_material.h"
#include "textures.h"
#include "gametexture.h"
#include "polyrenderer/backend/poly_framebuffer.h"
#include "polyrenderer/backend/poly_hwtexture.h"

#include "gpu_3ds_shbin.h"
#include "present_pixels.h"

extern "C" void C3Di_RenderQueueWaitDone(void);

namespace
{
constexpr int TopWidth = 400;
constexpr int TopHeight = 240;
constexpr int BottomWidth = 320;
constexpr int BottomHeight = 240;
constexpr int CanvasTextureWidth = 512;
constexpr int CanvasTextureHeight = 256;
// Each arena holds one frame's vertices; two alternate so the CPU never writes what the GPU
// may still read (C3D_FrameBegin waits for the previous frame, so one is ever in flight).
constexpr size_t ArenaVertices = 32768;
// Bounds the frame's GPU command list (C3D_Init below): a textured draw costs ~150 bytes.
constexpr size_t MaxDrawsPerFrame = 2048;
constexpr size_t CommandBufferBytes = 0x80000;
constexpr int MaxTextureSize = 1024;

using FVertex = F3DSGpuVertex;

enum class EMode : uint8_t
{
	Canvas,       // the BGRA canvas texture, swizzled back to RGB, opaque
	Image,        // overlay images: texture * vertex colour
	NoTexture,
	Normal,
	Stencil,
	Opaque,
	Inverse,
	InvertOpaque,
	AlphaTexture,
};

struct FDraw
{
	// A copy, not a pointer: the engine may release the texture between recording and the
	// present, and its memory is only freed two frames later (CollectReleases).
	C3D_Tex Texture;
	bool Textured;
	uint32_t First;
	uint32_t Count;
	uint32_t Overlay;    // added to the colour (the 2D "fog" overlay), 0xRRGGBBAA
	float SpaceWidth;    // the pixel space the vertices are in
	float SpaceHeight;
	int16_t Scissor[4];  // screen pixels: left, top, right, bottom; left < 0 = off
	E3DSGpuScreen Screen;
	EMode Mode;
	uint8_t BlendOp;
	uint8_t BlendSrc;
	uint8_t BlendDst;
};

struct FArena
{
	FVertex *Vertices = nullptr;
	size_t Used = 0;
};

struct FPendingRelease
{
	C3D_Tex Texture;
	uint32_t Frame;
};

}

struct F3DSGpuTexture
{
	C3D_Tex Texture;
	float ScaleS;
	float ScaleT;
	uint32_t Bytes;
};

struct F3DSGpuImage
{
	C3D_Tex Textures[2];
	int Current = -1;
	int Width = 0;
	int Height = 0;
	int TextureWidth = 0;
	int TextureHeight = 0;
};

namespace
{
bool Ready;
bool C3DReady;
DVLB_s *ShaderBinary;
shaderProgram_s Program;
int ProjectionLocation = -1;
C3D_RenderTarget *TopTarget;
C3D_RenderTarget *BottomTarget;
// The CPU's bottom interface, in the framebuffer's layout (gpu_3ds.h).
uint8_t *BottomStaging;
bool BottomDirty;
bool BottomHadDraws;
std::vector<FDraw> BottomDraws;
uint64_t LastPresentMilliseconds;
C3D_Tex CanvasTexture;
uint32_t *CanvasUpload;
FArena Arenas[2];
int CurrentArena;
std::vector<FDraw> Draws;
std::vector<FDraw> OverlayDraws;
std::vector<FPendingRelease> PendingReleases;
uint32_t SubmittedFrames;
bool TopPresented;
F3DSGpuStats Stats;

// Last state sent to citro3d, to skip redundant changes within a frame.
EMode AppliedMode;
uint32_t AppliedOverlay;
bool AppliedModeValid;
float AppliedSpaceWidth;
float AppliedSpaceHeight;

int NextPowerOfTwo(int value)
{
	int result = 8;
	while (result < value) result <<= 1;
	return result;
}

u32 TextureTransferFlags()
{
	return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
		GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
		GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
		GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

u32 OutputTransferFlags()
{
	return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
		GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
		GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
		GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

// Offset of texel (x, y) in a PICA tiled texture of the given width: 8x8 tiles in rows, Morton
// order inside a tile. Row 0 is the first row in memory, sampled at t = 1 (the image top).
inline size_t TiledOffset(int x, int y, int width)
{
	const unsigned tx = static_cast<unsigned>(x) & 7u;
	const unsigned ty = static_cast<unsigned>(y) & 7u;
	const unsigned morton = (tx & 1u) | ((ty & 1u) << 1) | ((tx & 2u) << 1) |
		((ty & 2u) << 2) | ((tx & 4u) << 2) | ((ty & 4u) << 3);
	return (static_cast<size_t>(y >> 3) * static_cast<size_t>(width >> 3) +
		static_cast<size_t>(x >> 3)) * 64u + morton;
}

// Frees the textures whose last possible use has retired.
void CollectReleases()
{
	size_t kept = 0;
	for (auto &pending : PendingReleases)
	{
		if (pending.Frame + 2u <= SubmittedFrames) C3D_TexDelete(&pending.Texture);
		else PendingReleases[kept++] = pending;
	}
	PendingReleases.resize(kept);
}

void ReleaseTexture(const C3D_Tex &texture)
{
	if (texture.data == nullptr) return;
	PendingReleases.push_back(FPendingRelease{ texture, SubmittedFrames });
}

uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	// Vertex colour attribute: four bytes in memory order R, G, B, A.
	return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		(static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

// TexEnv constants are 0xAABBGGRR.
uint32_t TevColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
		(static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

void ResetStage(int id)
{
	C3D_TexEnv *env = C3D_GetTexEnv(id);
	C3D_TexEnvInit(env);
}

// Adds the 2D overlay colour in stage `id` (PolySoftPoly's "simple 2D" fog: frag.rgb + fog.rgb).
void OverlayStage(int id, uint32_t overlay)
{
	C3D_TexEnv *env = C3D_GetTexEnv(id);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	C3D_TexEnvColor(env, overlay);
}

// Mirrors the SoftPoly 2D shading (screen_shader.cpp, uFogEnabled == -3): the texture mode,
// times the vertex colour, plus the overlay colour.
void ApplyMode(EMode mode, uint32_t overlay)
{
	if (AppliedModeValid && AppliedMode == mode && AppliedOverlay == overlay) return;
	AppliedModeValid = true;
	AppliedMode = mode;
	AppliedOverlay = overlay;

	for (int id = 0; id < 6; ++id) ResetStage(id);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	switch (mode)
	{
	case EMode::Canvas:
		// The canvas is uploaded as BGRA bytes into an RGBA8 texture: rebuild R, G and B from
		// the sampled G, B and A channels.
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		C3D_TexEnvColor(env, TevColor(255, 0, 0));

		env = C3D_GetTexEnv(1);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, TevColor(0, 255, 0));

		env = C3D_GetTexEnv(2);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, TevColor(0, 0, 255));
		return;

	case EMode::Image:
	case EMode::Normal:
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		break;

	case EMode::NoTexture:
		C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		break;

	case EMode::Stencil:
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
		break;

	case EMode::Opaque:
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		break;

	case EMode::Inverse:
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		break;

	case EMode::InvertOpaque:
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
		break;

	case EMode::AlphaTexture:
	{
		// (1, 1, 1, gray(texel) * texel.a), gray with SoftPoly's 77/143/37 weights, built
		// channel by channel because a TEV stage clamps every intermediate to [0, 1].
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_R,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
		C3D_TexEnvColor(env, TevColor(77, 77, 77));

		env = C3D_GetTexEnv(1);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, TevColor(143, 143, 143));

		env = C3D_GetTexEnv(2);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B,
			GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvColor(env, TevColor(37, 37, 37));

		env = C3D_GetTexEnv(3);
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PRIMARY_COLOR);
		C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_R,
			GPU_TEVOP_A_SRC_ALPHA);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
		OverlayStage(4, overlay);
		return;
	}
	}
	OverlayStage(1, overlay);
}

void ApplyProjection(float width, float height)
{
	if (AppliedSpaceWidth == width && AppliedSpaceHeight == height) return;
	AppliedSpaceWidth = width;
	AppliedSpaceHeight = height;
	C3D_Mtx projection;
	Mtx_OrthoTilt(&projection, 0.0f, width, height, 0.0f, 1.0f, -1.0f, true);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, ProjectionLocation, &projection);
}

GPU_BLENDFACTOR BlendFactor(int alpha)
{
	switch (alpha)
	{
	case STYLEALPHA_Zero: return GPU_ZERO;
	case STYLEALPHA_One: return GPU_ONE;
	case STYLEALPHA_Src: return GPU_SRC_ALPHA;
	case STYLEALPHA_InvSrc: return GPU_ONE_MINUS_SRC_ALPHA;
	case STYLEALPHA_SrcCol: return GPU_SRC_COLOR;
	case STYLEALPHA_InvSrcCol: return GPU_ONE_MINUS_SRC_COLOR;
	case STYLEALPHA_DstCol: return GPU_DST_COLOR;
	case STYLEALPHA_InvDstCol: return GPU_ONE_MINUS_DST_COLOR;
	case STYLEALPHA_Dst: return GPU_DST_ALPHA;
	case STYLEALPHA_InvDst: return GPU_ONE_MINUS_DST_ALPHA;
	default: return GPU_ONE;
	}
}

void ApplyDraw(const FDraw &draw)
{
	ApplyMode(draw.Mode, draw.Overlay);
	ApplyProjection(draw.SpaceWidth, draw.SpaceHeight);
	C3D_TexBind(0, draw.Textured ? const_cast<C3D_Tex *>(&draw.Texture) : nullptr);

	// SoftPoly's choice of operator (screen_blend.cpp SelectWriteColorFunc).
	const GPU_BLENDEQUATION equation = draw.BlendOp == STYLEOP_Add ? GPU_BLEND_ADD :
		(draw.BlendOp == STYLEOP_Sub ? GPU_BLEND_SUBTRACT : GPU_BLEND_REVERSE_SUBTRACT);
	const GPU_BLENDFACTOR source = BlendFactor(draw.BlendSrc);
	const GPU_BLENDFACTOR destination = BlendFactor(draw.BlendDst);
	C3D_AlphaBlend(equation, equation, source, destination, source, destination);

	if (draw.Scissor[0] < 0)
	{
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	}
	else
	{
		// The LCDs are rotated: screen x runs along the target's long axis, reversed, and
		// screen y along its 240-pixel axis, reversed.
		const int width = draw.Screen == E3DSGpuScreen::Top ? TopWidth : BottomWidth;
		const int height = draw.Screen == E3DSGpuScreen::Top ? TopHeight : BottomHeight;
		const int left = draw.Scissor[0];
		const int top = draw.Scissor[1];
		const int right = draw.Scissor[2];
		const int bottom = draw.Scissor[3];
		C3D_SetScissor(GPU_SCISSOR_NORMAL, height - bottom, width - right,
			height - top, width - left);
	}
	C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(draw.First), static_cast<int>(draw.Count));
}

bool ArenaReserve(size_t count, FVertex *&out, uint32_t &first)
{
	FArena &arena = Arenas[CurrentArena];
	if (arena.Used + count > ArenaVertices) return false;
	first = static_cast<uint32_t>(arena.Used);
	out = arena.Vertices + arena.Used;
	arena.Used += count;
	return true;
}

FDraw MakeScreenDraw(EMode mode, C3D_Tex *texture, uint32_t first, uint32_t count)
{
	FDraw draw{};
	draw.Textured = texture != nullptr;
	if (texture != nullptr) draw.Texture = *texture;
	draw.First = first;
	draw.Count = count;
	draw.Overlay = 0;
	draw.SpaceWidth = TopWidth;
	draw.SpaceHeight = TopHeight;
	draw.Scissor[0] = -1;
	draw.Screen = E3DSGpuScreen::Top;
	draw.Mode = mode;
	draw.BlendOp = STYLEOP_Add;
	draw.BlendSrc = STYLEALPHA_Src;
	draw.BlendDst = STYLEALPHA_InvSrc;
	return draw;
}

void WriteQuad(FVertex *vertices, float x0, float y0, float x1, float y1,
	float s0, float t0, float s1, float t1, uint32_t color)
{
	const FVertex topLeft{ x0, y0, s0, t0, color };
	const FVertex topRight{ x1, y0, s1, t0, color };
	const FVertex bottomLeft{ x0, y1, s0, t1, color };
	const FVertex bottomRight{ x1, y1, s1, t1, color };
	vertices[0] = topLeft;
	vertices[1] = bottomLeft;
	vertices[2] = topRight;
	vertices[3] = topRight;
	vertices[4] = bottomLeft;
	vertices[5] = bottomRight;
}

// Copies image (BGRA rows) into a new tiled RGBA8 texture. Without wrap the image keeps its
// texel size and the texture is padded to powers of two; with wrap (or past 1024 texels) it is
// stretched to fill the texture so that GPU_REPEAT tiles at the image's own period.
bool BuildTexture(C3D_Tex &texture, float &scaleS, float &scaleT, DCanvas *image, bool wrap)
{
	const int width = image->GetWidth();
	const int height = image->GetHeight();
	if (width <= 0 || height <= 0 || image->GetPixels() == nullptr || !image->IsBgra())
		return false;
	int textureWidth = std::min(NextPowerOfTwo(width), MaxTextureSize);
	int textureHeight = std::min(NextPowerOfTwo(height), MaxTextureSize);
	const bool stretchX = wrap || width > MaxTextureSize;
	const bool stretchY = wrap || height > MaxTextureSize;
	if (!C3D_TexInit(&texture, static_cast<u16>(textureWidth),
		static_cast<u16>(textureHeight), GPU_RGBA8))
	{
		return false;
	}
	uint32_t *texels = static_cast<uint32_t *>(texture.data);
	const int pitch = image->GetPitch();
	const uint32_t *source = reinterpret_cast<const uint32_t *>(image->GetPixels());
	for (int y = 0; y < textureHeight; ++y)
	{
		const int sourceY = stretchY ? y * height / textureHeight : y;
		const bool rowInside = sourceY < height;
		const uint32_t *row = source + static_cast<size_t>(sourceY) * pitch;
		for (int x = 0; x < textureWidth; ++x)
		{
			const int sourceX = stretchX ? x * width / textureWidth : x;
			uint32_t texel = 0;
			if (rowInside && sourceX < width)
			{
				// 0xAARRGGBB from BGRA bytes to the PICA's 0xRRGGBBAA.
				const uint32_t pixel = row[sourceX];
				texel = (pixel << 8) | (pixel >> 24);
			}
			texels[TiledOffset(x, y, textureWidth)] = texel;
		}
	}
	C3D_TexFlush(&texture);
	C3D_TexSetFilter(&texture, GPU_NEAREST, GPU_NEAREST);
	if (wrap) C3D_TexSetWrap(&texture, GPU_REPEAT, GPU_REPEAT);
	else C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	scaleS = stretchX ? 1.0f : static_cast<float>(width) / textureWidth;
	scaleT = stretchY ? 1.0f : static_cast<float>(height) / textureHeight;
	return true;
}

// The engine texture of a 2D command, as PolyRenderState::ApplyMaterial finds it.
F3DSGpuTexture *CommandTexture(const F2DDrawer::RenderCommand &command)
{
	FGameTexture *gameTexture = command.mTexture;
	const ETextureType useType = gameTexture->GetUseType();
	const EUpscaleFlags upscale = useType >= ETextureType::Special ? UF_None :
		(useType == ETextureType::FontChar ? UF_Font : UF_Texture);
	int scaleFlags = 0;
	if (shouldUpscale(gameTexture, upscale)) scaleFlags |= CTF_Upscale;
	FMaterial *material = FMaterial::ValidateTexture(gameTexture, scaleFlags);
	if (material == nullptr) return nullptr;
	if (gameTexture->isHardwareCanvas())
		static_cast<FCanvasTexture *>(gameTexture->GetTexture())->NeedUpdate();
	MaterialLayerInfo *layer = nullptr;
	auto *base = static_cast<PolyHardwareTexture *>(
		material->GetLayer(0, command.mTranslationId, &layer));
	if (base == nullptr || layer == nullptr) return nullptr;
	return base->GpuTexture(layer->layerTexture, command.mTranslationId, layer->scaleFlags,
		(command.mFlags & F2DDrawer::DTF_Wrap) != 0);
}

bool CommandSupported(const F2DDrawer::RenderCommand &command)
{
	if (command.shape2DBufInfo != nullptr) return false;
	if ((command.mFlags & (F2DDrawer::DTF_Burn | F2DDrawer::DTF_Indexed)) != 0) return false;
	if (command.mDesaturate != 0 || command.mSpecialColormap[0].a != 0) return false;
	if (command.mTexture != nullptr && command.mTexture->isValid() &&
		command.mDrawMode == TM_FOGLAYER)
	{
		return false;
	}
	return true;
}

EMode CommandMode(const F2DDrawer::RenderCommand &command, bool textured)
{
	if (!textured) return EMode::NoTexture;
	int mode = command.mDrawMode;
	// PolyRenderState::ApplyMaterial draws a hardware canvas as opaque.
	if (mode == TM_NORMAL && command.mTexture->isHardwareCanvas()) mode = TM_OPAQUE;
	switch (mode)
	{
	case TM_STENCIL: return EMode::Stencil;
	case TM_OPAQUE: return EMode::Opaque;
	case TM_INVERSE: return EMode::Inverse;
	case TM_ALPHATEXTURE: return EMode::AlphaTexture;
	case TM_INVERTOPAQUE: return EMode::InvertOpaque;
	default: return EMode::Normal; // TM_NORMAL, TM_CLAMPY
	}
}

// The state every pass starts from: our program, the vertex layout of the current arena, no
// depth or culling.
void BeginPass()
{
	C3D_BindProgram(&Program);
	C3D_AttrInfo *attributes = C3D_GetAttrInfo();
	AttrInfo_Init(attributes);
	AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attributes, 2, GPU_UNSIGNED_BYTE, 4);
	C3D_BufInfo *buffers = C3D_GetBufInfo();
	BufInfo_Init(buffers);
	BufInfo_Add(buffers, Arenas[CurrentArena].Vertices, sizeof(FVertex), 3, 0x210);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	AppliedModeValid = false;
	AppliedSpaceWidth = 0.0f;
	AppliedSpaceHeight = 0.0f;
}

// Copies the CPU's bottom interface into the bottom target when it changed or when last
// frame's GPU draws have to be covered. Called before C3D_FrameBegin: outside a frame,
// C3D_SyncDisplayTransfer waits for the GX queue to empty, runs the transfer on its own and
// waits for it. Inside the frame it would run alongside the frame's drawing into that same
// target (libctru starts up to three GX commands at once), which left the GX queue waiting
// forever on the console. The CPU cannot write VRAM itself (a data abort).
// Returns whether the bottom target has to be drawn and output this frame.
bool PrepareBottom()
{
	if (BottomTarget == nullptr) return false;
	const bool draws = !BottomDraws.empty();
	if (!BottomDirty && !draws && !BottomHadDraws) return false;
	const size_t bytes = static_cast<size_t>(BottomWidth) * BottomHeight * 4u;
	GSPGPU_FlushDataCache(BottomStaging, bytes);
	// Linear to tiled with no other change: the target's output transfer undoes it exactly.
	C3D_SyncDisplayTransfer(reinterpret_cast<u32 *>(BottomStaging),
		GX_BUFFER_DIM(BottomHeight, BottomWidth),
		static_cast<u32 *>(BottomTarget->frameBuf.colorBuf),
		GX_BUFFER_DIM(BottomHeight, BottomWidth), TextureTransferFlags());
	C3Di_RenderQueueWaitDone();
	BottomDirty = false;
	return true;
}

// The bottom target's draws, inside the frame being built, after the top screen's.
void DrawBottom()
{
	const bool draws = !BottomDraws.empty();
	C3D_FrameDrawOn(BottomTarget);
	if (draws)
	{
		BeginPass();
		C3D_AlphaTest(true, GPU_GREATER, 0);
		for (const FDraw &draw : BottomDraws) ApplyDraw(draw);
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	}
	BottomHadDraws = draws;
	BottomDraws.clear();
}

}

bool I_3DSGpuInit()
{
	if (Ready) return true;
	const size_t uploadBytes = CanvasTextureWidth * CanvasTextureHeight * sizeof(uint32_t);
	CanvasUpload = static_cast<uint32_t *>(linearMemAlign(uploadBytes, 0x80));
	if (CanvasUpload == nullptr) return false;
	std::memset(CanvasUpload, 0, uploadBytes);
	GSPGPU_FlushDataCache(CanvasUpload, uploadBytes);

	for (auto &arena : Arenas)
	{
		arena.Vertices = static_cast<FVertex *>(
			linearMemAlign(ArenaVertices * sizeof(FVertex), 0x80));
		arena.Used = 0;
		if (arena.Vertices == nullptr)
		{
			I_3DSGpuShutdown();
			return false;
		}
	}

	if (!C3D_Init(CommandBufferBytes))
	{
		I_3DSGpuShutdown();
		return false;
	}
	C3DReady = true;
	// Allow presentation up to the LCD refresh rate.
	C3D_FrameRate(60.0f);

	ShaderBinary = DVLB_ParseFile(const_cast<u32 *>(reinterpret_cast<const u32 *>(gpu_3ds_shbin)),
		gpu_3ds_shbin_size);
	if (ShaderBinary == nullptr)
	{
		I_3DSGpuShutdown();
		return false;
	}
	shaderProgramInit(&Program);
	shaderProgramSetVsh(&Program, &ShaderBinary->DVLE[0]);
	ProjectionLocation = shaderInstanceGetUniformLocation(Program.vertexShader, "projection");

	if (!C3D_TexInitVRAM(&CanvasTexture, CanvasTextureWidth, CanvasTextureHeight, GPU_RGBA8))
	{
		I_3DSGpuShutdown();
		return false;
	}
	// One PICA200 bilinear sample performs the requested smooth upscale; the software world
	// renderer still shades only its selected internal pixels.
	C3D_TexSetFilter(&CanvasTexture, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&CanvasTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	TopTarget = C3D_RenderTargetCreate(TopHeight, TopWidth, GPU_RB_RGBA8, GPU_RB_DEPTH16);
	if (TopTarget == nullptr)
	{
		I_3DSGpuShutdown();
		return false;
	}
	C3D_RenderTargetSetOutput(TopTarget, GFX_TOP, GFX_LEFT, OutputTransferFlags());

	// The bottom screen starts from what the framebuffer shows now.
	const size_t bottomBytes = static_cast<size_t>(BottomWidth) * BottomHeight * 4u;
	BottomStaging = static_cast<uint8_t *>(linearMemAlign(bottomBytes, 0x80));
	BottomTarget = C3D_RenderTargetCreate(BottomHeight, BottomWidth, GPU_RB_RGBA8,
		GPU_RB_DEPTH16);
	if (BottomStaging == nullptr || BottomTarget == nullptr)
	{
		I_3DSGpuShutdown();
		return false;
	}
	u16 framebufferWidth = 0;
	u16 framebufferHeight = 0;
	const uint8_t *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &framebufferWidth,
		&framebufferHeight);
	if (framebuffer != nullptr && framebufferWidth == BottomHeight &&
		framebufferHeight == BottomWidth && gfxGetScreenFormat(GFX_BOTTOM) == GSP_RGBA8_OES)
	{
		std::memcpy(BottomStaging, framebuffer, bottomBytes);
	}
	else
	{
		std::memset(BottomStaging, 0, bottomBytes);
	}
	C3D_RenderTargetSetOutput(BottomTarget, GFX_BOTTOM, GFX_LEFT, OutputTransferFlags());
	BottomDirty = true;

	Draws.reserve(MaxDrawsPerFrame);
	BottomDraws.reserve(64);
	OverlayDraws.reserve(64);
	Ready = true;
	return true;
}

void I_3DSGpuShutdown()
{
	if (C3DReady) C3Di_RenderQueueWaitDone();
	Ready = false;
	for (auto &pending : PendingReleases) C3D_TexDelete(&pending.Texture);
	PendingReleases.clear();
	if (TopTarget != nullptr)
	{
		C3D_RenderTargetDelete(TopTarget);
		TopTarget = nullptr;
	}
	if (BottomTarget != nullptr)
	{
		C3D_RenderTargetDelete(BottomTarget);
		BottomTarget = nullptr;
	}
	if (BottomStaging != nullptr)
	{
		linearFree(BottomStaging);
		BottomStaging = nullptr;
	}
	BottomDraws.clear();
	if (CanvasTexture.data != nullptr)
	{
		C3D_TexDelete(&CanvasTexture);
		CanvasTexture = {};
	}
	if (ShaderBinary != nullptr)
	{
		shaderProgramFree(&Program);
		DVLB_Free(ShaderBinary);
		ShaderBinary = nullptr;
	}
	if (C3DReady)
	{
		C3D_Fini();
		C3DReady = false;
	}
	for (auto &arena : Arenas)
	{
		if (arena.Vertices != nullptr) linearFree(arena.Vertices);
		arena = {};
	}
	if (CanvasUpload != nullptr)
	{
		linearFree(CanvasUpload);
		CanvasUpload = nullptr;
	}
	Draws.clear();
	OverlayDraws.clear();
}

bool I_3DSGpuReady()
{
	return Ready;
}

void I_3DSGpuWaitIdle()
{
	if (C3DReady) C3Di_RenderQueueWaitDone();
}

bool I_3DSGpuRecord2D(F2DDrawer *drawer)
{
	if (!Ready || drawer == nullptr) return false;
	auto &commands = drawer->mData;
	if (commands.Size() == 0) return true;
	auto &vertices = drawer->mVertices;
	auto &indices = drawer->mIndices;

	// Everything is checked before anything is written, so a refusal leaves no partial frame.
	static std::vector<F3DSGpuTexture *> textures;
	textures.assign(commands.Size(), nullptr);
	size_t vertexCount = 0;
	for (unsigned i = 0; i < commands.Size(); ++i)
	{
		const auto &command = commands[i];
		if (!CommandSupported(command)) return false;
		switch (command.mType)
		{
		case F2DDrawer::DrawTypeLines: vertexCount += command.mVertCount / 2 * 6; break;
		case F2DDrawer::DrawTypePoints: break;
		default: vertexCount += command.mIndexCount; break;
		}
	}
	if (Draws.size() + commands.Size() > MaxDrawsPerFrame ||
		Arenas[CurrentArena].Used + vertexCount > ArenaVertices)
	{
		return false;
	}
	for (unsigned i = 0; i < commands.Size(); ++i)
	{
		const auto &command = commands[i];
		if (command.mTexture == nullptr || !command.mTexture->isValid()) continue;
		textures[i] = CommandTexture(command);
		if (textures[i] == nullptr) return false;
	}

	const float spaceWidth = static_cast<float>(drawer->GetWidth());
	const float spaceHeight = static_cast<float>(drawer->GetHeight());
	if (spaceWidth <= 0.0f || spaceHeight <= 0.0f) return false;
	// ::Draw2D swaps the vertex colours to RGBA in place on a drawer's first pass.
	const bool coloursSwapped = !drawer->mIsFirstPass;

	for (unsigned i = 0; i < commands.Size(); ++i)
	{
		const auto &command = commands[i];
		if (command.mType == F2DDrawer::DrawTypePoints) continue; // SoftPoly draws no points
		F3DSGpuTexture *texture = textures[i];
		const bool flipV = texture != nullptr && command.mTexture->isHardwareCanvas();
		const float scaleS = texture != nullptr ? texture->ScaleS : 0.0f;
		const float scaleT = texture != nullptr ? texture->ScaleT : 0.0f;
		const bool transform = command.useTransform;
		const auto &matrix = command.transform;

		auto convert = [&](const F2DDrawer::TwoDVertex &source, FVertex &target)
		{
			float x = source.x;
			float y = source.y;
			if (transform)
			{
				const double tx = matrix.Cells[0][0] * source.x + matrix.Cells[0][1] * source.y +
					matrix.Cells[0][2];
				const double ty = matrix.Cells[1][0] * source.x + matrix.Cells[1][1] * source.y +
					matrix.Cells[1][2];
				x = static_cast<float>(tx);
				y = static_cast<float>(ty);
			}
			const float v = flipV ? 1.0f - source.v : source.v;
			target.X = x;
			target.Y = y;
			target.S = source.u * scaleS;
			target.T = 1.0f - v * scaleT;
			const PalEntry colour = source.color0;
			target.Color = coloursSwapped ?
				PackColor(colour.b, colour.g, colour.r, colour.a) :
				PackColor(colour.r, colour.g, colour.b, colour.a);
		};

		FVertex *out = nullptr;
		uint32_t first = 0;
		uint32_t count = 0;
		if (command.mType == F2DDrawer::DrawTypeLines)
		{
			const int lines = command.mVertCount / 2;
			count = static_cast<uint32_t>(lines * 6);
			if (count == 0 || !ArenaReserve(count, out, first)) continue;
			// One LCD pixel wide, in the drawer's pixel space.
			const float halfX = 0.5f * spaceWidth / TopWidth;
			const float halfY = 0.5f * spaceHeight / TopHeight;
			for (int line = 0; line < lines; ++line)
			{
				FVertex a{}, b{};
				convert(vertices[command.mVertIndex + line * 2], a);
				convert(vertices[command.mVertIndex + line * 2 + 1], b);
				const float dx = b.X - a.X;
				const float dy = b.Y - a.Y;
				const float length = std::sqrt(dx * dx + dy * dy);
				const float nx = length > 0.0f ? -dy / length * halfX : halfX;
				const float ny = length > 0.0f ? dx / length * halfY : 0.0f;
				FVertex *quad = out + line * 6;
				quad[0] = a; quad[0].X += nx; quad[0].Y += ny;
				quad[1] = a; quad[1].X -= nx; quad[1].Y -= ny;
				quad[2] = b; quad[2].X += nx; quad[2].Y += ny;
				quad[3] = quad[2];
				quad[4] = quad[1];
				quad[5] = b; quad[5].X -= nx; quad[5].Y -= ny;
			}
		}
		else
		{
			count = static_cast<uint32_t>(command.mIndexCount);
			if (count == 0 || !ArenaReserve(count, out, first)) continue;
			for (uint32_t index = 0; index < count; ++index)
				convert(vertices[indices[command.mIndexIndex + index]], out[index]);
		}

		FDraw draw{};
		draw.Textured = texture != nullptr;
		if (texture != nullptr) draw.Texture = texture->Texture;
		draw.First = first;
		draw.Count = count;
		draw.Overlay = TevColor(command.mColor1.r, command.mColor1.g, command.mColor1.b, 0);
		draw.SpaceWidth = spaceWidth;
		draw.SpaceHeight = spaceHeight;
		draw.Scissor[0] = -1;
		draw.Screen = E3DSGpuScreen::Top;
		if ((command.mFlags & F2DDrawer::DTF_Scissor) != 0)
		{
			auto toX = [&](int value)
			{
				return static_cast<int16_t>(std::clamp(static_cast<int>(
					std::lround(value * TopWidth / spaceWidth)), 0, TopWidth));
			};
			auto toY = [&](int value)
			{
				return static_cast<int16_t>(std::clamp(static_cast<int>(
					std::lround(value * TopHeight / spaceHeight)), 0, TopHeight));
			};
			draw.Scissor[0] = toX(command.mScissor[0]);
			draw.Scissor[1] = toY(command.mScissor[1]);
			draw.Scissor[2] = toX(command.mScissor[2]);
			draw.Scissor[3] = toY(command.mScissor[3]);
			if (draw.Scissor[2] <= draw.Scissor[0] || draw.Scissor[3] <= draw.Scissor[1])
				continue;
		}
		draw.Mode = CommandMode(command, texture != nullptr);
		draw.BlendOp = static_cast<uint8_t>(command.mRenderStyle.BlendOp);
		draw.BlendSrc = static_cast<uint8_t>(command.mRenderStyle.SrcAlpha);
		draw.BlendDst = static_cast<uint8_t>(command.mRenderStyle.DestAlpha);
		Draws.push_back(draw);
	}
	++Stats.Recorded2D;
	return true;
}

void I_3DSGpuDiscard2D()
{
	Draws.clear();
	OverlayDraws.clear();
	BottomDraws.clear();
	if (Ready) Arenas[CurrentArena].Used = 0;
}

F3DSGpuStats I_3DSGpuGetStats()
{
	return Stats;
}

void I_3DSGpuCountSoftware2D()
{
	++Stats.Software2D;
}

bool I_3DSGpuPresentTop(const uint8_t *pixels, int pitchBytes, int width, int height)
{
	if (!Ready || pixels == nullptr || width < 1 || height < 1 || width > TopWidth ||
		height > TopHeight || pitchBytes < width * 4)
	{
		return false;
	}

	const size_t uploadBytes = lod3ds::CopyPresentPixels(CanvasUpload, CanvasTextureWidth,
		pixels, pitchBytes, width, height);
	if (R_FAILED(GSPGPU_FlushDataCache(CanvasUpload, uploadBytes))) return false;

	FVertex *quad = nullptr;
	uint32_t quadFirst = 0;
	if (!ArenaReserve(6, quad, quadFirst)) return false;
	WriteQuad(quad, 0.0f, 0.0f, TopWidth, TopHeight,
		0.0f, 1.0f, static_cast<float>(width) / CanvasTextureWidth,
		1.0f - static_cast<float>(height) / CanvasTextureHeight, 0xffffffffu);

	const bool bottom = PrepareBottom();
	if (!C3D_FrameBegin(0)) return false;
	CollectReleases();
	C3D_SyncDisplayTransfer(CanvasUpload,
		GX_BUFFER_DIM(CanvasTextureWidth, CanvasTextureHeight),
		static_cast<u32 *>(CanvasTexture.data),
		GX_BUFFER_DIM(CanvasTextureWidth, CanvasTextureHeight),
		TextureTransferFlags());

	C3D_RenderTargetClear(TopTarget, C3D_CLEAR_ALL, 0x000000ff, 0);
	C3D_FrameDrawOn(TopTarget);
	FArena &arena = Arenas[CurrentArena];
	BeginPass();

	// The canvas, opaque.
	FDraw canvas = MakeScreenDraw(EMode::Canvas, &CanvasTexture, quadFirst, 6);
	canvas.BlendSrc = STYLEALPHA_One;
	canvas.BlendDst = STYLEALPHA_Zero;
	ApplyDraw(canvas);

	// SoftPoly's 2D alpha test discards fully transparent fragments.
	C3D_AlphaTest(true, GPU_GREATER, 0);
	for (const FDraw &draw : Draws) ApplyDraw(draw);
	for (const FDraw &draw : OverlayDraws) ApplyDraw(draw);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	const size_t bottomDraws = BottomDraws.size();
	if (bottom) DrawBottom();
	else BottomDraws.clear();

	Stats.LastDraws = static_cast<uint32_t>(Draws.size() + OverlayDraws.size() + bottomDraws + 1);
	Stats.LastVertices = static_cast<uint32_t>(arena.Used);
	// The draws' texture copies go with the vectors below; leave citro3d no pointer to them.
	C3D_TexBind(0, nullptr);
	GSPGPU_FlushDataCache(arena.Vertices, arena.Used * sizeof(FVertex));
	C3D_FrameEnd(GX_CMDLIST_FLUSH);
	++SubmittedFrames;
	TopPresented = true;
	LastPresentMilliseconds = osGetTime();

	Draws.clear();
	OverlayDraws.clear();
	CurrentArena ^= 1;
	Arenas[CurrentArena].Used = 0;
	return true;
}

unsigned char *I_3DSGpuBottomBuffer()
{
	return Ready ? BottomStaging : nullptr;
}

void I_3DSGpuBottomChanged()
{
	if (!Ready) return;
	BottomDirty = true;
	// Nothing presented lately (a dump, a level load): show it now. Not through a citro3d
	// frame: one with no draw is never submitted, and the next C3D_FrameBegin then waits for
	// it forever. With the GPU idle the CPU copies the buffer into the framebuffer instead;
	// BottomDirty stays set, so the next GPU frame outputs it again into both buffers.
	if (osGetTime() - LastPresentMilliseconds < 200u) return;
	C3Di_RenderQueueWaitDone();
	u16 framebufferWidth = 0;
	u16 framebufferHeight = 0;
	uint8_t *framebuffer = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &framebufferWidth,
		&framebufferHeight);
	if (framebuffer == nullptr || framebufferWidth != BottomHeight ||
		framebufferHeight != BottomWidth)
	{
		return;
	}
	const size_t bytes = static_cast<size_t>(BottomWidth) * BottomHeight * 4u;
	std::memcpy(framebuffer, BottomStaging, bytes);
	GSPGPU_FlushDataCache(framebuffer, bytes);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
	BottomHadDraws = true;
}

F3DSGpuVertex *I_3DSGpuQueueTriangles(E3DSGpuScreen screen, F3DSGpuTexture *texture,
	int count, const int *scissor)
{
	if (!Ready || count <= 0 || count % 3 != 0) return nullptr;
	std::vector<FDraw> &queue = screen == E3DSGpuScreen::Top ? OverlayDraws : BottomDraws;
	if (queue.size() >= MaxDrawsPerFrame) return nullptr;
	FVertex *vertices = nullptr;
	uint32_t first = 0;
	if (!ArenaReserve(static_cast<size_t>(count), vertices, first)) return nullptr;
	FDraw draw = MakeScreenDraw(texture != nullptr ? EMode::Normal : EMode::NoTexture,
		texture != nullptr ? &texture->Texture : nullptr, first, static_cast<uint32_t>(count));
	draw.Screen = screen;
	if (screen == E3DSGpuScreen::Bottom)
	{
		draw.SpaceWidth = BottomWidth;
		draw.SpaceHeight = BottomHeight;
	}
	if (scissor != nullptr)
	{
		for (int i = 0; i < 4; ++i) draw.Scissor[i] = static_cast<int16_t>(scissor[i]);
	}
	queue.push_back(draw);
	return vertices;
}

F3DSGpuTexture *I_3DSGpuGameTexture(FGameTexture *gameTexture, bool wrap)
{
	if (!Ready || gameTexture == nullptr || !gameTexture->isValid()) return nullptr;
	FMaterial *material = FMaterial::ValidateTexture(gameTexture, 0);
	if (material == nullptr) return nullptr;
	MaterialLayerInfo *layer = nullptr;
	auto *base = static_cast<PolyHardwareTexture *>(material->GetLayer(0, 0, &layer));
	if (base == nullptr || layer == nullptr) return nullptr;
	return base->GpuTexture(layer->layerTexture, 0, layer->scaleFlags, wrap);
}

bool I_3DSGpuReadTop(uint8_t *pixels, int pitchPixels, int width, int height, bool flipRows)
{
	if (!Ready || !TopPresented || pixels == nullptr || width <= 0 || height <= 0) return false;
	const size_t bytes = static_cast<size_t>(TopWidth) * TopHeight * 4u;
	uint8_t *linear = static_cast<uint8_t *>(linearMemAlign(bytes, 0x80));
	if (linear == nullptr) return false;
	GSPGPU_FlushDataCache(linear, bytes);
	// Outside a frame: citro3d's safe transfer, alone in the GX queue (see PrepareBottom). The
	// CPU cannot read the target in VRAM itself.
	C3D_SyncDisplayTransfer(static_cast<u32 *>(TopTarget->frameBuf.colorBuf),
		GX_BUFFER_DIM(TopHeight, TopWidth), reinterpret_cast<u32 *>(linear),
		GX_BUFFER_DIM(TopHeight, TopWidth), OutputTransferFlags());
	C3Di_RenderQueueWaitDone();
	GSPGPU_InvalidateDataCache(linear, bytes);

	// The same layout as the LCD framebuffer: one 240-pixel column per screen x, bottom first,
	// bytes A, B, G, R.
	uint32_t *destination = reinterpret_cast<uint32_t *>(pixels);
	for (int y = 0; y < height; ++y)
	{
		const int screenY = y * TopHeight / height;
		uint32_t *row = destination + static_cast<size_t>(flipRows ? height - 1 - y : y) *
			pitchPixels;
		for (int x = 0; x < width; ++x)
		{
			const int screenX = x * TopWidth / width;
			const uint8_t *source = linear +
				(static_cast<size_t>(screenX) * TopHeight + (TopHeight - 1 - screenY)) * 4u;
			row[x] = 0xff000000u | (static_cast<uint32_t>(source[3]) << 16) |
				(static_cast<uint32_t>(source[2]) << 8) | source[1];
		}
	}
	linearFree(linear);
	return true;
}

F3DSGpuTexture *I_3DSGpuTextureUpdate(F3DSGpuTexture *texture, DCanvas *image, bool wrap)
{
	if (!Ready || image == nullptr) return nullptr;
	F3DSGpuTexture *updated = new F3DSGpuTexture{};
	if (!BuildTexture(updated->Texture, updated->ScaleS, updated->ScaleT, image, wrap))
	{
		delete updated;
		return nullptr;
	}
	updated->Bytes = updated->Texture.size;
	Stats.TextureBytes += updated->Bytes;
	++Stats.Textures;
	I_3DSGpuTextureRelease(texture);
	return updated;
}

void I_3DSGpuTextureRelease(F3DSGpuTexture *texture)
{
	if (texture == nullptr) return;
	Stats.TextureBytes -= std::min(Stats.TextureBytes, texture->Bytes);
	if (Stats.Textures > 0) --Stats.Textures;
	if (Ready) ReleaseTexture(texture->Texture);
	else if (texture->Texture.data != nullptr) C3D_TexDelete(&texture->Texture);
	delete texture;
}

F3DSGpuImage *I_3DSGpuImageCreate(int width, int height)
{
	if (!Ready || width <= 0 || height <= 0 || width > MaxTextureSize ||
		height > MaxTextureSize)
	{
		return nullptr;
	}
	auto *image = new F3DSGpuImage{};
	image->Width = width;
	image->Height = height;
	image->TextureWidth = NextPowerOfTwo(width);
	image->TextureHeight = NextPowerOfTwo(height);
	return image;
}

void I_3DSGpuImageUpload(F3DSGpuImage *image, const uint32_t *rgba, int pitchPixels)
{
	if (!Ready || image == nullptr || rgba == nullptr) return;
	// Alternate between two textures: the previous one may still be in the frame in flight.
	const int next = image->Current < 0 ? 0 : image->Current ^ 1;
	C3D_Tex &texture = image->Textures[next];
	if (texture.data == nullptr)
	{
		if (!C3D_TexInit(&texture, static_cast<u16>(image->TextureWidth),
			static_cast<u16>(image->TextureHeight), GPU_RGBA8))
		{
			return;
		}
		std::memset(texture.data, 0, texture.size);
		C3D_TexSetFilter(&texture, GPU_NEAREST, GPU_NEAREST);
		C3D_TexSetWrap(&texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	}
	uint32_t *texels = static_cast<uint32_t *>(texture.data);
	for (int y = 0; y < image->Height; ++y)
	{
		const uint32_t *row = rgba + static_cast<size_t>(y) * pitchPixels;
		for (int x = 0; x < image->Width; ++x)
			texels[TiledOffset(x, y, image->TextureWidth)] = row[x];
	}
	C3D_TexFlush(&texture);
	image->Current = next;
}

void I_3DSGpuOverlayImage(F3DSGpuImage *image, float x, float y, float width, float height)
{
	if (!Ready || image == nullptr || image->Current < 0) return;
	FVertex *quad = nullptr;
	uint32_t first = 0;
	if (!ArenaReserve(6, quad, first)) return;
	WriteQuad(quad, x, y, x + width, y + height, 0.0f, 1.0f,
		static_cast<float>(image->Width) / image->TextureWidth,
		1.0f - static_cast<float>(image->Height) / image->TextureHeight, 0xffffffffu);
	OverlayDraws.push_back(MakeScreenDraw(EMode::Image, &image->Textures[image->Current],
		first, 6));
}

void I_3DSGpuOverlayRect(float x, float y, float width, float height, uint32_t rgba)
{
	if (!Ready) return;
	FVertex *quad = nullptr;
	uint32_t first = 0;
	if (!ArenaReserve(6, quad, first)) return;
	WriteQuad(quad, x, y, x + width, y + height, 0.0f, 0.0f, 0.0f, 0.0f,
		PackColor(static_cast<uint8_t>(rgba >> 24), static_cast<uint8_t>(rgba >> 16),
			static_cast<uint8_t>(rgba >> 8), static_cast<uint8_t>(rgba)));
	OverlayDraws.push_back(MakeScreenDraw(EMode::NoTexture, nullptr, first, 6));
}

#else

bool I_3DSGpuInit() { return false; }
void I_3DSGpuShutdown() {}
bool I_3DSGpuReady() { return false; }
void I_3DSGpuWaitIdle() {}
bool I_3DSGpuRecord2D(F2DDrawer *) { return false; }
void I_3DSGpuDiscard2D() {}
F3DSGpuStats I_3DSGpuGetStats() { return {}; }
void I_3DSGpuCountSoftware2D() {}
bool I_3DSGpuPresentTop(const uint8_t *, int, int, int) { return false; }
bool I_3DSGpuReadTop(uint8_t *, int, int, int, bool) { return false; }
unsigned char *I_3DSGpuBottomBuffer() { return nullptr; }
void I_3DSGpuBottomChanged() {}
F3DSGpuVertex *I_3DSGpuQueueTriangles(E3DSGpuScreen, F3DSGpuTexture *, int, const int *)
{
	return nullptr;
}
F3DSGpuTexture *I_3DSGpuGameTexture(FGameTexture *, bool) { return nullptr; }
F3DSGpuTexture *I_3DSGpuTextureUpdate(F3DSGpuTexture *, DCanvas *, bool) { return nullptr; }
void I_3DSGpuTextureRelease(F3DSGpuTexture *) {}
F3DSGpuImage *I_3DSGpuImageCreate(int, int) { return nullptr; }
void I_3DSGpuImageUpload(F3DSGpuImage *, const uint32_t *, int) {}
void I_3DSGpuOverlayImage(F3DSGpuImage *, float, float, float, float) {}
void I_3DSGpuOverlayRect(float, float, float, float, uint32_t) {}

#endif
