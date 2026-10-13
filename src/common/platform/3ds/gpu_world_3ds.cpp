// The 3D world on the PICA200, P2.3 spike (docs/gpu-renderer-design.md, option (c)).
//
// The swrenderer's opaque pass walks the BSP as usual; right after it, I_3DSGpuWorldCapture
// takes the subsectors it found visible (front to back, PvsSubsectors) and builds their walls
// and flats as triangles for the GPU (gpu_3ds.cpp draws them with depth in place of the canvas).
// The swrenderer still draws its canvas meanwhile: this spike measures what the GPU side costs.
// Not yet: light, sprites, sky, masked mid-textures, 3D floors, portals.
//
// lod3ds_gpu_world (debug builds, not saved) turns it on; lod3ds_gpu_world_stats reads the CPU
// time of the capture, the draws and vertices, and the GPU's drawing time.
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

CVAR(Bool, lod3ds_gpu_world, false, 0)
CVAR(String, lod3ds_gpu_world_stats, "", CVAR_NOSAVE)

namespace
{
using FVertex = F3DSGpuWorldVertex;

// Triangles waiting for their texture's draw, so that each texture is one draw.
struct FBatch
{
	F3DSGpuTexture *Texture;
	std::vector<FVertex> Vertices;
};
std::unordered_map<F3DSGpuTexture *, size_t> BatchIndex;
// This frame's GPU copies of the textures met, as I_3DSGpuGameTexture finds them.
std::unordered_map<FGameTexture *, F3DSGpuTexture *> TextureCache;
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

std::vector<FVertex> &BatchFor(F3DSGpuTexture *texture)
{
	auto found = BatchIndex.find(texture);
	if (found != BatchIndex.end()) return Batches[found->second].Vertices;
	if (BatchesUsed == Batches.size()) Batches.push_back(FBatch{});
	FBatch &batch = Batches[BatchesUsed];
	batch.Texture = texture;
	batch.Vertices.clear();
	BatchIndex.emplace(texture, BatchesUsed++);
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
	double u0, const side_t *side, int part, uint32_t color)
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

	std::vector<FVertex> &out = BatchFor(texture);
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
	auto vertex = [&](const vertex_t *v)
	{
		return FVertex{ float(v->fX()), float(v->fY()), float(plane.ZatPoint(v)),
			float(v->fX() / width), float(-v->fY() / height), color };
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
	char text[160];
	const double ms = static_cast<double>(StatTicks) / StatFrames / (SYSCLOCK_ARM11 / 1000.0);
	std::snprintf(text, sizeof(text), "capture %.2f ms, %u draws, %u vertices, gpu %.2f ms",
		ms, StatDraws / StatFrames, StatVertices / StatFrames, I_3DSGpuLastDrawingTime());
	lod3ds_gpu_world_stats = text;
	StatTicks = 0;
	StatFrames = StatVertices = StatDraws = 0;
	StatLastPublish = now;
}
}

void I_3DSGpuWorldCapture(swrenderer::RenderThread *thread)
{
	if (thread == nullptr || !thread->MainThread) return;
	Active = false;
	if (!LOD3DS_DEBUG_TOOLS || !lod3ds_gpu_world || !I_3DSGpuReady()) return;
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
	const double nearZ = 4.0 * viewport->viewwindow.FocalTangent, farZ = 8192.0 * viewport->viewwindow.FocalTangent;
	const double az = nearZ / (farZ - nearZ), bz = -nearZ * farZ / (farZ - nearZ);
	float matrix[16];
	for (int i = 0; i < 4; ++i)
	{
		matrix[i] = float(ax * right[i] + bx * depth[i]);
		matrix[4 + i] = float(ay * up[i] + by * depth[i]);
		matrix[8 + i] = float(az * depth[i] + (i == 3 ? bz : 0.0));
		matrix[12 + i] = float(depth[i]);
	}
	// The NES overworld's sky blue until the sky has its own pass.
	I_3DSGpuWorldBegin(matrix, 0x5c94fcff);
	Active = true;

	BatchIndex.clear();
	TextureCache.clear();
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

	uint32_t vertices = 0;
	for (size_t i = 0; i < BatchesUsed; ++i)
	{
		const FBatch &batch = Batches[i];
		if (batch.Vertices.empty()) continue;
		FVertex *out = I_3DSGpuWorldQueue(batch.Texture, static_cast<int>(batch.Vertices.size()));
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
	return Active && lod3ds_gpu_world;
}

#else

void I_3DSGpuWorldCapture(swrenderer::RenderThread *) {}
bool I_3DSGpuWorldActive() { return false; }

#endif
