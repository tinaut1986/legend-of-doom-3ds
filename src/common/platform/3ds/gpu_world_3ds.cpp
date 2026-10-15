// The 3D world on the PICA200, P2.3 spike (docs/gpu-renderer-design.md, option (c)).
//
// The swrenderer's opaque pass walks the BSP as usual; right after it, I_3DSGpuWorldCapture
// takes the subsectors it found visible (front to back, PvsSubsectors) and builds their walls
// and flats as triangles for the GPU (gpu_3ds.cpp draws them with depth in place of the canvas).
// The swrenderer still draws its canvas meanwhile: this spike measures what the GPU side costs.
// Sprites come from the walk itself (RenderSprite::Project hands them over instead of keeping
// them for the swrenderer's translucent pass), drawn as masked billboards.
// Not yet: light, sky, translucency, masked mid-textures, 3D floors, portals.
//
// On in every build (owner 2026-10-14: the world never falls back to the CPU). lod3ds_gpu_world
// (not saved, so every start is on the GPU) turns it off in debug builds to compare;
// lod3ds_gpu_world_stats reads the CPU time of the capture, the draws and vertices, and the
// GPU's drawing time.
#include "gpu_world_3ds.h"

#if defined(__3DS__) && defined(LOD3DS_HYBRID_PERFORMANCE)

#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "c_cvars.h"
#include "g_levellocals.h"
#include "r_defs.h"
#include "r_sky.h"
#include "r_state.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "gametexture.h"
#include "swrenderer/textures/r_swtexture.h"
#include "swrenderer/r_renderthread.h"
#include "swrenderer/scene/r_opaque_pass.h"
#include "swrenderer/viewport/r_viewport.h"
#include "gpu_3ds.h"
#include "lod3ds_version.h"

CVAR(Bool, lod3ds_gpu_world, true, 0)
CVAR(String, lod3ds_gpu_world_stats, "", CVAR_NOSAVE)
// Stereo 3D with the 3D slider (P3); off draws one image whatever the slider says.
CVAR(Bool, lod3ds_stereo, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{
using FVertex = F3DSGpuWorldVertex;

// Stereo: the distance in map units that sits on the screen (zero parallax), and how far behind
// it infinity goes per eye at full slider, in top-screen pixels. The convergence is the
// player's radius, the nearest a wall or a thing can be: the whole world stays behind the
// screen, so the weapon and the HUD, drawn at the screen's depth, are never in front of
// something that looks nearer (owner, 2026-10-13: at 128 near enemies came out over them).
constexpr double StereoConvergence = 16.0;
constexpr double StereoMaxPixels = 5.0;

// Triangles waiting for their texture's draw, so that each texture is one draw (masked ones,
// the sprites, apart).
struct FBatch
{
	F3DSGpuTexture *Texture;
	bool Masked;
	std::vector<FVertex> Vertices;
};
std::unordered_map<uintptr_t, size_t> BatchIndex;

// The sprites the walk projected this frame (I_3DSGpuWorldAddSprite).
struct FSprite
{
	FGameTexture *Texture;
	float Ax, Ay, Bx, By;
	float Bottom, Top;
	bool FlipX;
	uint8_t Light;
};
std::vector<FSprite> Sprites;
// This frame's GPU copies of the textures met, as I_3DSGpuGameTexture finds them.
std::unordered_map<FGameTexture *, F3DSGpuTexture *> TextureCache;
std::unordered_map<FGameTexture *, F3DSGpuTexture *> SpriteTextureCache;
std::vector<FBatch> Batches;
size_t BatchesUsed;

// Rolling averages for lod3ds_gpu_world_stats.
uint64_t StatTicks;
uint32_t StatFrames;
uint32_t StatVertices;
uint32_t StatDraws;
uint64_t StatLastPublish;
// Set by the capture for the frame being rendered.
bool Active;
// A picture drawn into a canvas (the save game's) is rendered by the CPU alone.
bool Suspended;
bool ActiveBeforeSuspend;

std::vector<FVertex> &BatchFor(F3DSGpuTexture *texture, bool masked = false)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(texture) | (masked ? 1u : 0u);
	auto found = BatchIndex.find(key);
	if (found != BatchIndex.end()) return Batches[found->second].Vertices;
	if (BatchesUsed == Batches.size()) Batches.push_back(FBatch{});
	FBatch &batch = Batches[BatchesUsed];
	batch.Texture = texture;
	batch.Masked = masked;
	batch.Vertices.clear();
	BatchIndex.emplace(key, BatchesUsed++);
	return batch.Vertices;
}

F3DSGpuTexture *GpuTexture(FGameTexture *gameTexture)
{
	auto found = TextureCache.find(gameTexture);
	if (found != TextureCache.end()) return found->second;
	F3DSGpuTexture *texture = I_3DSGpuGameTexture(gameTexture, true);
	TextureCache.emplace(gameTexture, texture);
	return texture;
}

// Sprites: the copy padded to powers of two, not stretched (no wrap).
F3DSGpuTexture *SpriteTexture(FGameTexture *gameTexture)
{
	auto found = SpriteTextureCache.find(gameTexture);
	if (found != SpriteTextureCache.end()) return found->second;
	F3DSGpuTexture *texture = I_3DSGpuGameTexture(gameTexture, false);
	SpriteTextureCache.emplace(gameTexture, texture);
	return texture;
}

void AddSpriteQuad(const FSprite &sprite)
{
	F3DSGpuTexture *texture = SpriteTexture(sprite.Texture);
	if (texture == nullptr) return;
	float scaleS = 1.0f, scaleT = 1.0f;
	I_3DSGpuTextureScale(texture, scaleS, scaleT);
	const float s0 = sprite.FlipX ? scaleS : 0.0f;
	const float s1 = sprite.FlipX ? 0.0f : scaleS;
	const float tTop = 1.0f, tBottom = 1.0f - scaleT;
	const uint32_t color = I_3DSGpuColor(sprite.Light, sprite.Light, sprite.Light);
	const FVertex topLeft{ sprite.Ax, sprite.Ay, sprite.Top, s0, tTop, color };
	const FVertex bottomLeft{ sprite.Ax, sprite.Ay, sprite.Bottom, s0, tBottom, color };
	const FVertex topRight{ sprite.Bx, sprite.By, sprite.Top, s1, tTop, color };
	const FVertex bottomRight{ sprite.Bx, sprite.By, sprite.Bottom, s1, tBottom, color };
	std::vector<FVertex> &out = BatchFor(texture, true);
	out.push_back(topLeft);
	out.push_back(bottomLeft);
	out.push_back(topRight);
	out.push_back(topRight);
	out.push_back(bottomLeft);
	out.push_back(bottomRight);
}

uint32_t LightColor(const sector_t *sector)
{
	const int light = std::clamp(static_cast<int>(sector->lightlevel), 0, 255);
	return I_3DSGpuColor(light, light, light);
}

FGameTexture *PartTexture(const side_t *side, int part)
{
	const FTextureID id = side->GetTexture(part);
	if (!id.isValid()) return nullptr;
	FGameTexture *texture = TexMan.GetGameTexture(id, true);
	return texture != nullptr && texture->isValid() ? texture : nullptr;
}

// One wall quad from (x1, y1) to (x2, y2), bottom z1/z2 to top z3/z4 (at each end), texture
// top at topRef (map z where v = 0), u from u0 at the first end.
void AddWall(FGameTexture *gameTexture, const DVector2 &a, const DVector2 &b,
	double bottomA, double bottomB, double topA, double topB, double topRef,
	double u0, const side_t *side, int part, uint32_t color, bool masked = false)
{
	if (topA <= bottomA && topB <= bottomB) return;
	F3DSGpuTexture *texture = GpuTexture(gameTexture);
	if (texture == nullptr) return;
	const double width = gameTexture->GetDisplayWidth() / side->GetTextureXScale(part);
	const double height = gameTexture->GetDisplayHeight() / side->GetTextureYScale(part);
	if (width <= 0 || height <= 0) return;
	const double length = (b - a).Length();
	const double s0 = u0 / width;
	const double s1 = (u0 + length) / width;
	const double yOffset = side->GetTextureYOffset(part);
	auto t = [&](double z) { return static_cast<float>(1.0 - (topRef - z + yOffset) / height); };

	std::vector<FVertex> &out = BatchFor(texture, masked);
	const FVertex bottomLeft{ float(a.X), float(a.Y), float(bottomA), float(s0), t(bottomA), color };
	const FVertex topLeft{ float(a.X), float(a.Y), float(topA), float(s0), t(topA), color };
	const FVertex bottomRight{ float(b.X), float(b.Y), float(bottomB), float(s1), t(bottomB), color };
	const FVertex topRight{ float(b.X), float(b.Y), float(topB), float(s1), t(topB), color };
	out.push_back(topLeft);
	out.push_back(bottomLeft);
	out.push_back(topRight);
	out.push_back(topRight);
	out.push_back(bottomLeft);
	out.push_back(bottomRight);
}

void AddSeg(const seg_t *seg, const DVector2 &eye)
{
	const side_t *side = seg->sidedef;
	const line_t *line = seg->linedef;
	const sector_t *front = seg->frontsector;
	if (side == nullptr || line == nullptr || front == nullptr) return; // a miniseg
	const DVector2 a(seg->v1->fX(), seg->v1->fY());
	const DVector2 b(seg->v2->fX(), seg->v2->fY());
	// Only the side facing the eye: the front side is on the right of v1 -> v2.
	const DVector2 delta = b - a;
	if (delta.X * (eye.Y - a.Y) - delta.Y * (eye.X - a.X) > 0) return;

	// u: distance along the sidedef from its own start, plus its offset.
	const vertex_t *sideStart = line->sidedef[0] == side ? line->v1 : line->v2;
	const double along = (a - DVector2(sideStart->fX(), sideStart->fY())).Length();
	const uint32_t color = LightColor(front);

	const double frontFloorA = front->floorplane.ZatPoint(seg->v1);
	const double frontFloorB = front->floorplane.ZatPoint(seg->v2);
	const double frontCeilA = front->ceilingplane.ZatPoint(seg->v1);
	const double frontCeilB = front->ceilingplane.ZatPoint(seg->v2);
	const sector_t *back = seg->backsector;
	if (back == nullptr)
	{
		FGameTexture *texture = PartTexture(side, side_t::mid);
		if (texture == nullptr) return;
		const double height = texture->GetDisplayHeight() / side->GetTextureYScale(side_t::mid);
		const double ref = (line->flags & ML_DONTPEGBOTTOM) ?
			front->GetPlaneTexZ(sector_t::floor) + height : front->GetPlaneTexZ(sector_t::ceiling);
		AddWall(texture, a, b, frontFloorA, frontFloorB, frontCeilA, frontCeilB, ref,
			along + side->GetTextureXOffset(side_t::mid), side, side_t::mid, color);
		return;
	}

	const double backFloorA = back->floorplane.ZatPoint(seg->v1);
	const double backFloorB = back->floorplane.ZatPoint(seg->v2);
	const double backCeilA = back->ceilingplane.ZatPoint(seg->v1);
	const double backCeilB = back->ceilingplane.ZatPoint(seg->v2);
	const bool bothSky = front->GetTexture(sector_t::ceiling) == skyflatnum &&
		back->GetTexture(sector_t::ceiling) == skyflatnum;
	if (!bothSky && (backCeilA < frontCeilA || backCeilB < frontCeilB))
	{
		if (FGameTexture *texture = PartTexture(side, side_t::top))
		{
			const double height = texture->GetDisplayHeight() / side->GetTextureYScale(side_t::top);
			const double ref = (line->flags & ML_DONTPEGTOP) ?
				front->GetPlaneTexZ(sector_t::ceiling) : back->GetPlaneTexZ(sector_t::ceiling) + height;
			AddWall(texture, a, b, backCeilA, backCeilB, frontCeilA, frontCeilB, ref,
				along + side->GetTextureXOffset(side_t::top), side, side_t::top, color);
		}
	}
	// A middle texture on a two-sided line: masked, one texture high unless it wraps, inside
	// the opening. MAP01 builds its bushes, rocks, statues and graves from these.
	if (FGameTexture *texture = PartTexture(side, side_t::mid))
	{
		const double height = texture->GetDisplayHeight() / side->GetTextureYScale(side_t::mid);
		const double ref = (line->flags & ML_DONTPEGBOTTOM) ?
			std::max(front->GetPlaneTexZ(sector_t::floor), back->GetPlaneTexZ(sector_t::floor)) + height :
			std::min(front->GetPlaneTexZ(sector_t::ceiling), back->GetPlaneTexZ(sector_t::ceiling));
		double bottomA = std::max(frontFloorA, backFloorA), bottomB = std::max(frontFloorB, backFloorB);
		double topA = std::min(frontCeilA, backCeilA), topB = std::min(frontCeilB, backCeilB);
		const bool wrap = (line->flags & ML_WRAP_MIDTEX) || (side->Flags & WALLF_WRAP_MIDTEX);
		if (!wrap)
		{
			const double textureTop = ref + side->GetTextureYOffset(side_t::mid);
			const double textureBottom = textureTop - height;
			topA = std::min(topA, textureTop);
			topB = std::min(topB, textureTop);
			bottomA = std::max(bottomA, textureBottom);
			bottomB = std::max(bottomB, textureBottom);
		}
		AddWall(texture, a, b, bottomA, bottomB, topA, topB, ref,
			along + side->GetTextureXOffset(side_t::mid), side, side_t::mid, color, true);
	}
	if (backFloorA > frontFloorA || backFloorB > frontFloorB)
	{
		if (FGameTexture *texture = PartTexture(side, side_t::bottom))
		{
			const double ref = (line->flags & ML_DONTPEGBOTTOM) ?
				front->GetPlaneTexZ(sector_t::ceiling) : back->GetPlaneTexZ(sector_t::floor);
			AddWall(texture, a, b, frontFloorA, frontFloorB, backFloorA, backFloorB, ref,
				along + side->GetTextureXOffset(side_t::bottom), side, side_t::bottom, color);
		}
	}
}

// The subsector's floor or ceiling: a fan over its (convex) polygon, flats on the map grid.
void AddFlat(const subsector_t *sub, const sector_t *sector, int which, const DVector3 &eye)
{
	const FTextureID id = sector->GetTexture(which);
	if (!id.isValid() || id == skyflatnum) return;
	const secplane_t &plane = which == sector_t::floor ? sector->floorplane : sector->ceilingplane;
	// Only the side of the plane the eye sees.
	const double planeZ = plane.ZatPoint(eye.XY());
	if (which == sector_t::floor ? eye.Z <= planeZ : eye.Z >= planeZ) return;
	FGameTexture *gameTexture = TexMan.GetGameTexture(id, true);
	if (gameTexture == nullptr || !gameTexture->isValid()) return;
	F3DSGpuTexture *texture = GpuTexture(gameTexture);
	if (texture == nullptr || sub->numlines < 3) return;
	const double width = gameTexture->GetDisplayWidth();
	const double height = gameTexture->GetDisplayHeight();
	if (width <= 0 || height <= 0) return;
	const uint32_t color = LightColor(sector);
	// The hardware renderer's flat mapping (hw_SetPlaneTextureRotation): (x / 64, -y / 64)
	// rotated, scaled to the texture, offset, scaled by the sector's scale; t is minus v.
	const double angle = -sector->GetAngle(which).Radians();
	const double cosine = std::cos(angle), sine = std::sin(angle);
	const double offsetU = sector->GetXOffset(which) / width;
	const double offsetV = sector->GetYOffset(which) / height;
	const double scaleU = sector->GetXScale(which), scaleV = sector->GetYScale(which);
	auto vertex = [&](const vertex_t *v)
	{
		const double u0 = v->fX() / 64.0, v0 = -v->fY() / 64.0;
		const double u = ((cosine * u0 - sine * v0) * 64.0 / width + offsetU) * scaleU;
		const double vv = ((sine * u0 + cosine * v0) * 64.0 / height + offsetV) * scaleV;
		return FVertex{ float(v->fX()), float(v->fY()), float(plane.ZatPoint(v)),
			float(u), float(-vv), color };
	};
	std::vector<FVertex> &out = BatchFor(texture);
	const FVertex first = vertex(sub->firstline[0].v1);
	for (uint32_t i = 1; i + 1 < sub->numlines; ++i)
	{
		out.push_back(first);
		out.push_back(vertex(sub->firstline[i].v1));
		out.push_back(vertex(sub->firstline[i + 1].v1));
	}
}

void PublishStats()
{
	const uint64_t now = osGetTime();
	if (now - StatLastPublish < 1000 || StatFrames == 0) return;
	char text[192];
	const double ms = static_cast<double>(StatTicks) / StatFrames / (SYSCLOCK_ARM11 / 1000.0);
	const F3DSGpuStats gpu = I_3DSGpuGetStats();
	std::snprintf(text, sizeof(text),
		"capture %.2f ms, %u draws, %u vertices, gpu %.2f ms, 2d gpu %u sw %u",
		ms, StatDraws / StatFrames, StatVertices / StatFrames, I_3DSGpuLastDrawingTime(),
		static_cast<unsigned>(gpu.Recorded2D), static_cast<unsigned>(gpu.Software2D));
	lod3ds_gpu_world_stats = text;
	StatTicks = 0;
	StatFrames = StatVertices = StatDraws = 0;
	StatLastPublish = now;
}
}

void I_3DSGpuWorldBeginScene(swrenderer::RenderThread *thread)
{
	if (thread == nullptr || !thread->MainThread) return;
	// Stable builds cannot turn it off; debug builds can, for comparisons.
	Active = (lod3ds_gpu_world || !LOD3DS_DEBUG_TOOLS) && I_3DSGpuReady() && !Suspended;
	Sprites.clear();
}

void I_3DSGpuWorldSuspend(bool suspend)
{
	if (suspend == Suspended) return;
	if (suspend)
	{
		ActiveBeforeSuspend = Active;
		Suspended = true;
		Active = false;
	}
	else
	{
		Suspended = false;
		Active = ActiveBeforeSuspend;
	}
}

void I_3DSGpuWorldAddSprite(FGameTexture *texture, double ax, double ay, double bx, double by,
	double bottom, double top, bool flipX, int lightLevel)
{
	if (!Active || texture == nullptr) return;
	Sprites.push_back(FSprite{ texture, float(ax), float(ay), float(bx), float(by),
		float(bottom), float(top), flipX, uint8_t(std::clamp(lightLevel, 0, 255)) });
}

void I_3DSGpuWorldCapture(swrenderer::RenderThread *thread)
{
	if (thread == nullptr || !thread->MainThread || !Active) return;
	const uint64_t start = svcGetSystemTick();
	swrenderer::RenderViewport *viewport = thread->Viewport.get();
	FLevelLocals *level = viewport->Level();
	if (level == nullptr || viewwidth <= 0 || viewheight <= 0) return;
	const FRenderViewpoint &vp = viewport->viewpoint;

	// World to clip for the upright canvas, as the swrenderer projects: view x right
	// (x Sin - y Cos), view depth scaled by the focal tangent (x TanCos + y TanSin),
	// screen x = CenterX + CenterX * right / depth, screen y = CenterY - InvZtoScale * up / depth.
	const double ex = vp.Pos.X, ey = vp.Pos.Y, ez = vp.Pos.Z;
	const double right[4] = { vp.Sin, -vp.Cos, 0, -(ex * vp.Sin - ey * vp.Cos) };
	const double up[4] = { 0, 0, 1, -ez };
	const double depth[4] = { vp.TanCos, vp.TanSin, 0, -(ex * vp.TanCos + ey * vp.TanSin) };
	const double w = viewwidth, h = viewheight;
	const double ax = 2 * viewport->CenterX / w, bx = 2 * viewport->CenterX / w - 1;
	const double ay = 2 * viewport->InvZtoScale / h, by = 1 - 2 * viewport->CenterY / h;
	// Depth: z/w = -1 at near, 0 at far (citro3d's range; greater = nearer after its mapping).
	const double focalTangent = viewport->viewwindow.FocalTangent;
	const double nearZ = 4.0 * focalTangent, farZ = 8192.0 * focalTangent;
	const double az = nearZ / (farZ - nearZ), bz = -nearZ * farZ / (farZ - nearZ);
	// Stereo (P3): each eye moved half the separation sideways (eye = -1 left, +1 right), its
	// image shifted back so that StereoConvergence sits on the screen; nearer pops out, farther
	// goes in, up to StereoMaxPixels per eye at infinity with the slider up. The visible set is
	// the centre eye's.
	const float slider = lod3ds_stereo ? osGet3DSliderState() : 0.0f;
	const double screenPixelsPerNdc = 200.0; // the top screen's half width
	const double halfSeparation = slider * StereoMaxPixels / screenPixelsPerNdc *
		StereoConvergence * focalTangent / ax;
	auto build = [&](float matrix[16], double eye)
	{
		const double shift = eye * halfSeparation;
		const double shear = shift * ax / (StereoConvergence * focalTangent);
		for (int i = 0; i < 4; ++i)
		{
			const double moved = right[i] - (i == 3 ? shift : 0.0);
			matrix[i] = float(ax * moved + (bx + shear) * depth[i]);
			matrix[4 + i] = float(ay * up[i] + by * depth[i]);
			matrix[8 + i] = float(az * depth[i] + (i == 3 ? bz : 0.0));
			matrix[12 + i] = float(depth[i]);
		}
	};
	float matrix[16];
	float rightMatrix[16];
	const bool stereo = halfSeparation > 0.0;
	build(matrix, stereo ? -1.0 : 0.0);
	if (stereo) build(rightMatrix, 1.0);
	// The NES overworld's sky blue until the sky has its own pass.
	I_3DSGpuWorldBegin(matrix, stereo ? rightMatrix : nullptr, 0x5c94fcff);

	BatchIndex.clear();
	TextureCache.clear();
	SpriteTextureCache.clear();
	BatchesUsed = 0;
	const DVector2 eye2(ex, ey);
	const DVector3 eye3(ex, ey, ez);
	for (uint32_t index : thread->OpaquePass->VisibleSubsectors())
	{
		if (index >= level->subsectors.Size()) continue;
		const subsector_t *sub = &level->subsectors[index];
		const sector_t *sector = sub->sector;
		if (sector == nullptr) continue;
		for (uint32_t i = 0; i < sub->numlines; ++i) AddSeg(&sub->firstline[i], eye2);
		AddFlat(sub, sector, sector_t::floor, eye3);
		AddFlat(sub, sector, sector_t::ceiling, eye3);
	}
	for (const FSprite &sprite : Sprites) AddSpriteQuad(sprite);

	uint32_t vertices = 0;
	for (size_t i = 0; i < BatchesUsed; ++i)
	{
		const FBatch &batch = Batches[i];
		if (batch.Vertices.empty()) continue;
		FVertex *out = I_3DSGpuWorldQueue(batch.Texture, static_cast<int>(batch.Vertices.size()),
			batch.Masked);
		if (out == nullptr) break;
		std::memcpy(out, batch.Vertices.data(), batch.Vertices.size() * sizeof(FVertex));
		vertices += static_cast<uint32_t>(batch.Vertices.size());
	}

	StatTicks += svcGetSystemTick() - start;
	StatFrames += 1;
	StatVertices += vertices;
	StatDraws += static_cast<uint32_t>(BatchesUsed);
	PublishStats();
}

bool I_3DSGpuWorldActive()
{
	return Active && (lod3ds_gpu_world || !LOD3DS_DEBUG_TOOLS);
}

#else

void I_3DSGpuWorldBeginScene(swrenderer::RenderThread *) {}
void I_3DSGpuWorldSuspend(bool) {}
void I_3DSGpuWorldAddSprite(FGameTexture *, double, double, double, double, double, double, bool,
	int) {}
void I_3DSGpuWorldCapture(swrenderer::RenderThread *) {}
bool I_3DSGpuWorldActive() { return false; }

#endif
