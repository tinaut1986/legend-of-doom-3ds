// The 3D world on the PICA200, P2.3 spike (docs/gpu-renderer-design.md, option (c)).
//
// The swrenderer's opaque pass walks the BSP as usual; right after it, I_3DSGpuWorldCapture
// takes the subsectors it found visible (front to back, PvsSubsectors) and builds their walls
// and flats as triangles for the GPU (gpu_3ds.cpp draws them with depth in place of the canvas).
// The swrenderer still draws its canvas meanwhile: this spike measures what the GPU side costs.
// Sprites come from the walk itself (RenderSprite::Project hands them over instead of keeping
// them for the swrenderer's translucent pass), drawn as masked billboards.
// Light is the swrenderer's, per vertex (VertexColor): the sector's level, the walls' fake
// contrast and the fading with depth, and MAP01's distance fog outdoors; long walls are cut so
// that it changes smoothly along them. The sky is a cylinder around the eye drawn first as the
// background, mapped as the swrenderer maps it (AddSky).
// 3D floors (MAP01's platforms and glowing sheets): their planes in the subsectors of the sector
// that holds them and their sides on the boundary segs seen from the neighbouring sector, opaque
// or see-through (translucent in depth order, additive batched, drawn with the sprites).
// Not yet: portals, coloured sector light and fades.
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
#include "p_3dfloors.h"
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
#include "swrenderer/scene/r_light.h"
#include "swrenderer/r_swcolormaps.h"
#include "swrenderer/distance_fog_math.h"
#include "gpu_3ds.h"
#include "lod3ds_version.h"

CVAR(Bool, lod3ds_gpu_world, true, 0)
CVAR(String, lod3ds_gpu_world_stats, "", CVAR_NOSAVE)
// Stereo 3D with the 3D slider (P3); off draws one image whatever the slider says.
CVAR(Bool, lod3ds_stereo, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
#if LOD3DS_DEBUG_TOOLS
// OPTIONS -> DEVELOPER -> FORCE 3D: both eyes drawn as with the slider all the way up, whatever it
// says, so a 2DS (no 3D screen, it shows the left eye) measures what an Old 3DS pays in 3D.
// Not saved: it is a test.
CVAR(Bool, lod3ds_force_stereo, false, 0)
#endif

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
	bool Fullbright;
	bool Exterior;
	uint8_t Light;
	uint8_t Style; // I_3DSGpuWorldAddSprite's: 0 opaque, 1 translucent, 2 additive, 3 stencil
	uint8_t Alpha;
	uint32_t StencilColor;
	float Depth;   // for drawing the see-through ones far to near
};
std::vector<FSprite> Sprites;
// A see-through face of a 3D floor (a wall or a plane) waiting for its draw.
struct FFace
{
	F3DSGpuTexture *Texture;
	E3DSGpuWorldStyle Style;
	uint8_t Alpha;
	float Depth;
	std::vector<FVertex> Vertices;
};
std::vector<FFace> Faces;
size_t FacesUsed;
// What the see-through pass draws, far to near: a sprite or a face.
struct FSeeThrough
{
	float Depth;
	const FSprite *Sprite;
	const FFace *Face;
};
std::vector<FSeeThrough> SeeThrough;
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

// The frame's light (VertexColor): the swrenderer's own visibility, the eye, the view's depth
// axis (as the projection's), and whether MAP01's distance fog is on.
struct FLightContext
{
	const swrenderer::LightVisibility *Light;
	swrenderer::RenderViewport *Viewport;
	double EyeX, EyeY, TanCos, TanSin;
	bool Map01Fog;
} LightContext;

// The colour of a vertex at (x, y) lit by `lightLevel`, as the swrenderer lights its columns:
// colormap index = shade - min(24, visibility at that depth), 0 (full) to 31 (darkest), the
// texel times 1 - index / 32. Outdoors in MAP01 (exterior sectors) the index is the distance
// fog's instead and the texel fades to the fog colour (vertex alpha, gpu_3ds.cpp's stage 1).
uint32_t VertexColor(double x, double y, int lightLevel, bool exterior, bool fullbright = false)
{
	const FLightContext &c = LightContext;
	const double dx = x - c.EyeX, dy = y - c.EyeY;
	const double depth = std::max(dx * c.TanCos + dy * c.TanSin, 1e-3);
	const int level = fullbright ? 255 : std::clamp(lightLevel, 0, 255);
	const double shade = FIXED2DBL(swrenderer::LightVisibility::LightLevelToShade(level, false,
		c.Viewport));
	if (exterior && c.Map01Fog)
	{
		const double amount = DistanceFogAmount(std::sqrt(dx * dx + dy * dy),
			Map01DistanceFogStart, Map01DistanceFogEnd);
		const double index = std::clamp(std::max(amount * (NUMCOLORMAPS - 1), shade - MAXLIGHTVIS),
			0.0, NUMCOLORMAPS - 1.0);
		const int keep = static_cast<int>(std::lround(255.0 * (1.0 - index / NUMCOLORMAPS)));
		return I_3DSGpuColor(255, 255, 255, uint8_t(keep));
	}
	const double visibility = fullbright ? 0.0 : c.Light->WallVis(depth, false);
	const double index = std::clamp(shade - std::min(MAXLIGHTVIS, visibility), 0.0,
		NUMCOLORMAPS - 1.0);
	const int light = static_cast<int>(std::lround(255.0 * (1.0 - index / NUMCOLORMAPS)));
	return I_3DSGpuColor(uint8_t(light), uint8_t(light), uint8_t(light));
}

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
	uint32_t color = VertexColor((sprite.Ax + sprite.Bx) * 0.5, (sprite.Ay + sprite.By) * 0.5,
		sprite.Light, sprite.Exterior, sprite.Fullbright);
	if (sprite.Style == 3)
	{
		// The stencil colour, lit: the light is in R (= G = B), the fog in A.
		const uint32_t light = color & 0xff;
		auto lit = [&](int shift) { return uint8_t(((sprite.StencilColor >> shift) & 0xff) * light / 255); };
		color = I_3DSGpuColor(lit(16), lit(8), lit(0), uint8_t(color >> 24));
	}
	const FVertex topLeft{ sprite.Ax, sprite.Ay, sprite.Top, s0, tTop, color };
	const FVertex bottomLeft{ sprite.Ax, sprite.Ay, sprite.Bottom, s0, tBottom, color };
	const FVertex topRight{ sprite.Bx, sprite.By, sprite.Top, s1, tTop, color };
	const FVertex bottomRight{ sprite.Bx, sprite.By, sprite.Bottom, s1, tBottom, color };
	const FVertex quad[6] = { topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight };
	if (sprite.Style != 0)
	{
		// Its own draw, after the opaque world (the caller queues them far to near).
		static constexpr E3DSGpuWorldStyle styles[] = { E3DSGpuWorldStyle::Translucent,
			E3DSGpuWorldStyle::Translucent, E3DSGpuWorldStyle::Add, E3DSGpuWorldStyle::Stencil };
		if (FVertex *out = I_3DSGpuWorldQueueBlended(texture, 6, styles[sprite.Style], sprite.Alpha))
			std::memcpy(out, quad, sizeof(quad));
		return;
	}
	std::vector<FVertex> &out = BatchFor(texture, true);
	out.insert(out.end(), quad, quad + 6);
}

bool Exterior(const sector_t *sector)
{
	return LightContext.Map01Fog && Is3DSMap01ExteriorSector(sector);
}

FGameTexture *PartTexture(const side_t *side, int part)
{
	const FTextureID id = side->GetTexture(part);
	if (!id.isValid()) return nullptr;
	FGameTexture *texture = TexMan.GetGameTexture(id, true);
	return texture != nullptr && texture->isValid() ? texture : nullptr;
}

// One wall quad from (x1, y1) to (x2, y2), bottom z1/z2 to top z3/z4 (at each end), texture
// top at topRef (map z where v = 0), u from u0 at the first end; lit by lightLevel, cut into
// pieces of at most WallPiece map units so that the light follows the depth along it.
constexpr double WallPiece = 128.0;
void AddWall(FGameTexture *gameTexture, const DVector2 &a, const DVector2 &b,
	double bottomA, double bottomB, double topA, double topB, double topRef,
	double u0, const side_t *side, int part, int lightLevel, bool exterior, bool masked = false,
	FFace *face = nullptr)
{
	if (topA <= bottomA && topB <= bottomB) return;
	F3DSGpuTexture *texture = GpuTexture(gameTexture);
	if (texture == nullptr) return;
	const double width = gameTexture->GetDisplayWidth() / side->GetTextureXScale(part);
	const double height = gameTexture->GetDisplayHeight() / side->GetTextureYScale(part);
	if (width <= 0 || height <= 0) return;
	const double length = (b - a).Length();
	const double yOffset = side->GetTextureYOffset(part);
	auto t = [&](double z) { return static_cast<float>(1.0 - (topRef - z + yOffset) / height); };

	if (face != nullptr) face->Texture = texture;
	std::vector<FVertex> &out = face != nullptr ? face->Vertices : BatchFor(texture, masked);
	const int pieces = std::clamp(static_cast<int>(std::ceil(length / WallPiece)), 1, 16);
	auto edge = [&](int i, FVertex &bottom, FVertex &top)
	{
		const double f = static_cast<double>(i) / pieces;
		const DVector2 p = a + (b - a) * f;
		const double low = bottomA + (bottomB - bottomA) * f;
		const double high = topA + (topB - topA) * f;
		const float s = float((u0 + length * f) / width);
		const uint32_t color = VertexColor(p.X, p.Y, lightLevel, exterior);
		bottom = FVertex{ float(p.X), float(p.Y), float(low), s, t(low), color };
		top = FVertex{ float(p.X), float(p.Y), float(high), s, t(high), color };
	};
	FVertex bottomLeft, topLeft, bottomRight, topRight;
	edge(0, bottomLeft, topLeft);
	for (int i = 1; i <= pieces; ++i)
	{
		edge(i, bottomRight, topRight);
		out.push_back(topLeft);
		out.push_back(bottomLeft);
		out.push_back(topRight);
		out.push_back(topRight);
		out.push_back(bottomLeft);
		out.push_back(bottomRight);
		bottomLeft = bottomRight;
		topLeft = topRight;
	}
}

// A subsector's plane at the height of `plane`: a fan over its (convex) polygon, flats on the map
// grid, mapped with `model`'s offsets, rotation and scale for `which` (sector_t::floor or
// ceiling). Into `face` when given, else the opaque batches.
void AddPlane(const subsector_t *sub, const secplane_t &plane, FTextureID id, const sector_t *model,
	int which, int lightLevel, bool exterior, FFace *face = nullptr)
{
	if (!id.isValid() || id == skyflatnum) return;
	FGameTexture *gameTexture = TexMan.GetGameTexture(id, true);
	if (gameTexture == nullptr || !gameTexture->isValid()) return;
	F3DSGpuTexture *texture = GpuTexture(gameTexture);
	if (texture == nullptr || sub->numlines < 3) return;
	const double width = gameTexture->GetDisplayWidth();
	const double height = gameTexture->GetDisplayHeight();
	if (width <= 0 || height <= 0) return;
	// The hardware renderer's flat mapping (hw_SetPlaneTextureRotation): (x / 64, -y / 64)
	// rotated, scaled to the texture, offset, scaled by the sector's scale; t is minus v.
	const double angle = -model->GetAngle(which).Radians();
	const double cosine = std::cos(angle), sine = std::sin(angle);
	const double offsetU = model->GetXOffset(which) / width;
	const double offsetV = model->GetYOffset(which) / height;
	const double scaleU = model->GetXScale(which), scaleV = model->GetYScale(which);
	auto vertex = [&](double x, double y)
	{
		const double u0 = x / 64.0, v0 = -y / 64.0;
		const double u = ((cosine * u0 - sine * v0) * 64.0 / width + offsetU) * scaleU;
		const double vv = ((sine * u0 + cosine * v0) * 64.0 / height + offsetV) * scaleV;
		return FVertex{ float(x), float(y), float(plane.ZatPoint(DVector2(x, y))),
			float(u), float(-vv), VertexColor(x, y, lightLevel, exterior) };
	};
	// A fan from the polygon's centre, so that its middle is lit at its own depth too.
	double cx = 0, cy = 0;
	for (uint32_t i = 0; i < sub->numlines; ++i)
	{
		cx += sub->firstline[i].v1->fX();
		cy += sub->firstline[i].v1->fY();
	}
	const FVertex centre = vertex(cx / sub->numlines, cy / sub->numlines);
	if (face != nullptr) face->Texture = texture;
	std::vector<FVertex> &out = face != nullptr ? face->Vertices : BatchFor(texture);
	FVertex previous = vertex(sub->firstline[0].v1->fX(), sub->firstline[0].v1->fY());
	const FVertex first = previous;
	for (uint32_t i = 1; i <= sub->numlines; ++i)
	{
		const FVertex next = i == sub->numlines ? first :
			vertex(sub->firstline[i].v1->fX(), sub->firstline[i].v1->fY());
		out.push_back(centre);
		out.push_back(previous);
		out.push_back(next);
		previous = next;
	}
}

// The subsector's floor or ceiling.
void AddFlat(const subsector_t *sub, const sector_t *sector, int which, const DVector3 &eye)
{
	const secplane_t &plane = which == sector_t::floor ? sector->floorplane : sector->ceilingplane;
	// Only the side of the plane the eye sees.
	const double planeZ = plane.ZatPoint(eye.XY());
	if (which == sector_t::floor ? eye.Z <= planeZ : eye.Z >= planeZ) return;
	AddPlane(sub, plane, sector->GetTexture(which), sector, which, sector->lightlevel,
		Exterior(sector));
}

// The face a 3D floor part goes into: nullptr when it is opaque (the caller draws it in the
// batches). Additive ones share a face per texture, the others get their own.
FFace *FaceFor(const F3DFloor *ff, double depth)
{
	const bool additive = (ff->flags & FF_ADDITIVETRANS) != 0;
	const int alpha = std::clamp(ff->alpha, 0, 255);
	if (!additive && (!(ff->flags & FF_TRANSLUCENT) || alpha >= 255)) return nullptr;
	if (FacesUsed == Faces.size()) Faces.push_back(FFace{});
	FFace &face = Faces[FacesUsed++];
	face.Vertices.clear();
	face.Style = additive ? E3DSGpuWorldStyle::Add : E3DSGpuWorldStyle::Translucent;
	face.Alpha = uint8_t(alpha);
	face.Depth = float(depth);
	face.Texture = nullptr;
	return &face;
}

double ViewDepth(double x, double y)
{
	const FLightContext &c = LightContext;
	return (x - c.EyeX) * c.TanCos + (y - c.EyeY) * c.TanSin;
}

// The planes of the sector's 3D floors in one of its subsectors: the top when the eye is above
// it, the bottom when below (both for FF_BOTHPLANES, such as water seen from inside).
void AddFFloorPlanes(const subsector_t *sub, const sector_t *sector, const DVector3 &eye)
{
	const auto &floors = sector->e->XFloor.ffloors;
	if (floors.Size() == 0) return;
	double cx = 0, cy = 0;
	for (uint32_t i = 0; i < sub->numlines; ++i)
	{
		cx += sub->firstline[i].v1->fX();
		cy += sub->firstline[i].v1->fY();
	}
	const double depth = sub->numlines > 0 ? ViewDepth(cx / sub->numlines, cy / sub->numlines) : 0.0;
	const bool exterior = Exterior(sector);
	for (unsigned i = 0; i < floors.Size(); ++i)
	{
		const F3DFloor *ff = floors[i];
		if (!(ff->flags & FF_EXISTS) || !(ff->flags & FF_RENDERPLANES) || (ff->flags & FF_FOG)) continue;
		const bool both = (ff->flags & FF_BOTHPLANES) != 0;
		const double topZ = ff->top.plane->ZatPoint(eye.XY());
		const double bottomZ = ff->bottom.plane->ZatPoint(eye.XY());
		if (eye.Z > topZ || both)
			AddPlane(sub, *ff->top.plane, *ff->top.texture, ff->top.model, ff->top.isceiling,
				sector->lightlevel, exterior, FaceFor(ff, depth));
		if (eye.Z < bottomZ || both)
			AddPlane(sub, *ff->bottom.plane, *ff->bottom.texture, ff->bottom.model,
				ff->bottom.isceiling, sector->lightlevel, exterior, FaceFor(ff, depth));
	}
}

// The sides of `owner`'s 3D floors on a seg that faces the eye. On a seg between two sectors
// they are drawn from the neighbour (`other`), unless it holds the same 3D floor (the line is
// inside it); from the owner itself only for FF_ALLSIDES.
void AddFFloorSides(const seg_t *seg, const sector_t *owner, const sector_t *other,
	const DVector2 &a, const DVector2 &b, double along, int lightLevel, bool exterior,
	bool allSidesOnly)
{
	const auto &floors = owner->e->XFloor.ffloors;
	const double depth = ViewDepth((a.X + b.X) * 0.5, (a.Y + b.Y) * 0.5);
	for (unsigned i = 0; i < floors.Size(); ++i)
	{
		const F3DFloor *ff = floors[i];
		if (!(ff->flags & FF_EXISTS) || !(ff->flags & FF_RENDERSIDES) || (ff->flags & FF_FOG)) continue;
		if (allSidesOnly && !(ff->flags & FF_ALLSIDES)) continue;
		if (ff->master == nullptr || ff->master->sidedef[0] == nullptr) continue;
		if (other != nullptr)
		{
			bool inside = false;
			const auto &otherFloors = other->e->XFloor.ffloors;
			for (unsigned j = 0; j < otherFloors.Size() && !inside; ++j)
				inside = otherFloors[j]->master == ff->master;
			if (inside) continue;
		}
		const side_t *master = ff->master->sidedef[0];
		FGameTexture *texture = PartTexture(master, side_t::mid);
		if (texture == nullptr) continue;
		const double topA = ff->top.plane->ZatPoint(seg->v1), topB = ff->top.plane->ZatPoint(seg->v2);
		const double bottomA = ff->bottom.plane->ZatPoint(seg->v1);
		const double bottomB = ff->bottom.plane->ZatPoint(seg->v2);
		const double height = texture->GetDisplayHeight() / master->GetTextureYScale(side_t::mid);
		const double ref = (ff->master->flags & ML_DONTPEGBOTTOM) ? bottomA + height : topA;
		AddWall(texture, a, b, bottomA, bottomB, topA, topB, ref,
			along + master->GetTextureXOffset(side_t::mid), master, side_t::mid, lightLevel,
			exterior, false, FaceFor(ff, depth));
	}
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
	// The front sector's light with the wall's fake contrast, as the swrenderer's wall setup.
	const int lightLevel = side->GetLightLevel(false, front->lightlevel);
	const bool exterior = Exterior(front);
	if (front->e->XFloor.ffloors.Size() != 0)
		AddFFloorSides(seg, front, seg->backsector, a, b, along, lightLevel, exterior, true);
	if (seg->backsector != nullptr && seg->backsector->e->XFloor.ffloors.Size() != 0)
		AddFFloorSides(seg, seg->backsector, front, a, b, along, lightLevel, exterior, false);

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
			along + side->GetTextureXOffset(side_t::mid), side, side_t::mid, lightLevel, exterior);
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
				along + side->GetTextureXOffset(side_t::top), side, side_t::top, lightLevel, exterior);
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
			along + side->GetTextureXOffset(side_t::mid), side, side_t::mid, lightLevel, exterior, true);
	}
	if (backFloorA > frontFloorA || backFloorB > frontFloorB)
	{
		if (FGameTexture *texture = PartTexture(side, side_t::bottom))
		{
			const double ref = (line->flags & ML_DONTPEGBOTTOM) ?
				front->GetPlaneTexZ(sector_t::ceiling) : back->GetPlaneTexZ(sector_t::floor);
			AddWall(texture, a, b, frontFloorA, frontFloorB, backFloorA, backFloorB, ref,
				along + side->GetTextureXOffset(side_t::bottom), side, side_t::bottom, lightLevel, exterior);
		}
	}
}

// The sky (Level->skytexture1) as the swrenderer draws it (r_skyplane.cpp): 1024 texels around
// the 360 degrees (the texture's width if wider), the columns running against the view angle;
// texel row `mid` (100 for a 128-high sky: texturemid -28 + the height) on the horizon and one
// row per skyiscale screen rows. Here a cylinder around the eye, far enough to sit at infinity
// in stereo, drawn before the world with no depth (I_3DSGpuWorldQueue's sky); above and below
// its band the clear colour, the sky's top cap.
constexpr double SkyRadius = 4096.0;
constexpr int SkySides = 32;
void AddSky(FLevelLocals *level, const FRenderViewpoint &vp, swrenderer::RenderViewport *viewport)
{
	FGameTexture *gameTexture = TexMan.GetGameTexture(level->skytexture1, true);
	if (gameTexture == nullptr || !gameTexture->isValid()) return;
	F3DSGpuTexture *texture = GpuTexture(gameTexture);
	if (texture == nullptr) return;
	const double texWidth = gameTexture->GetDisplayWidth();
	const double texHeight = gameTexture->GetDisplayHeight();
	if (texWidth <= 0 || texHeight <= 0 || freelookviewheight <= 0) return;
	const double cylinder = std::max(texWidth, gameTexture->GetScaleX() * 1024.0);
	const double mid = (texHeight >= 128 && texHeight < 200) ? texHeight - 28 :
		texHeight >= 200 ? 200.0 : texHeight;
	const double skyiscale = 200.0 / freelookviewheight * (vp.FieldOfView.Degrees / 90.0);
	// Screen rows per map unit of height at the cylinder's distance, straight ahead.
	const double rowsPerUnit = viewport->InvZtoScale /
		(SkyRadius * viewport->viewwindow.FocalTangent);
	auto heightOfRow = [&](double row) { return (mid - row) / (skyiscale * rowsPerUnit); };
	const double top = vp.Pos.Z + heightOfRow(0.0);
	const double bottom = vp.Pos.Z + heightOfRow(texHeight);
	const double scroll = level->sky1pos / 65536.0 / texWidth;
	const uint32_t color = I_3DSGpuColor(255, 255, 255);
	std::vector<FVertex> sky;
	sky.reserve(SkySides * 6);
	for (int i = 0; i < SkySides; ++i)
	{
		const double a0 = 2.0 * M_PI * i / SkySides, a1 = 2.0 * M_PI * (i + 1) / SkySides;
		const float x0 = float(vp.Pos.X + SkyRadius * std::cos(a0));
		const float y0 = float(vp.Pos.Y + SkyRadius * std::sin(a0));
		const float x1 = float(vp.Pos.X + SkyRadius * std::cos(a1));
		const float y1 = float(vp.Pos.Y + SkyRadius * std::sin(a1));
		const float s0 = float(a0 / (2.0 * M_PI) * cylinder / texWidth + scroll);
		const float s1 = float(a1 / (2.0 * M_PI) * cylinder / texWidth + scroll);
		sky.push_back(FVertex{ x0, y0, float(top), s0, 1.0f, color });
		sky.push_back(FVertex{ x0, y0, float(bottom), s0, 0.0f, color });
		sky.push_back(FVertex{ x1, y1, float(top), s1, 1.0f, color });
		sky.push_back(FVertex{ x1, y1, float(top), s1, 1.0f, color });
		sky.push_back(FVertex{ x0, y0, float(bottom), s0, 0.0f, color });
		sky.push_back(FVertex{ x1, y1, float(bottom), s1, 0.0f, color });
	}
	FVertex *out = I_3DSGpuWorldQueue(texture, static_cast<int>(sky.size()), false, true);
	if (out != nullptr) std::memcpy(out, sky.data(), sky.size() * sizeof(FVertex));
}

// Whether a level has sky ceilings (once per level): no sky pass in the dungeons.
bool LevelHasSky(FLevelLocals *level)
{
	static FLevelLocals *checked = nullptr;
	static FString checkedMap;
	static bool hasSky = false;
	if (checked != level || checkedMap.CompareNoCase(level->MapName) != 0)
	{
		checked = level;
		checkedMap = level->MapName;
		hasSky = false;
		for (const sector_t &sector : level->sectors)
			if (sector.GetTexture(sector_t::ceiling) == skyflatnum) { hasSky = true; break; }
	}
	return hasSky;
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
	double bottom, double top, bool flipX, int lightLevel, bool fullbright, const sector_t *sector,
	int style, double alpha, uint32_t stencilColor)
{
	if (!Active || texture == nullptr) return;
	const int alphaByte = static_cast<int>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
	// Invisible (Ganon's A_SetTranslucent(0)): not drawn at all.
	if (style != 0 && alphaByte == 0) return;
	if (style == 1 && alphaByte == 255) style = 0;
	Sprites.push_back(FSprite{ texture, float(ax), float(ay), float(bx), float(by),
		float(bottom), float(top), flipX, fullbright,
		sector != nullptr && Is3DSMap01ExteriorSector(sector),
		uint8_t(std::clamp(lightLevel, 0, 255)), uint8_t(style), uint8_t(alphaByte), stencilColor,
		0.0f });
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
	float slider = lod3ds_stereo ? osGet3DSliderState() : 0.0f;
#if LOD3DS_DEBUG_TOOLS
	if (lod3ds_force_stereo) slider = 1.0f;
#endif
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
	LightContext = FLightContext{ thread->Light.get(), viewport, ex, ey, vp.TanCos, vp.TanSin,
		level->MapName.CompareNoCase("MAP01") == 0 };
	// The background: the sky's top cap where the sky band does not reach, black indoors.
	const bool sky = LevelHasSky(level);
	uint32_t clear = 0x000000ff;
	if (sky)
	{
		if (FGameTexture *skyTexture = TexMan.GetGameTexture(level->skytexture1, true))
		{
			const PalEntry cap = R_GetSkyCapColor(skyTexture).first;
			clear = (uint32_t(cap.r) << 24) | (uint32_t(cap.g) << 16) | (uint32_t(cap.b) << 8) | 0xff;
		}
	}
	I_3DSGpuWorldBegin(matrix, stereo ? rightMatrix : nullptr, clear,
		(uint32_t(Map01DistanceFogRed) << 24) | (uint32_t(Map01DistanceFogGreen) << 16) |
		(uint32_t(Map01DistanceFogBlue) << 8) | 0xff);

	BatchIndex.clear();
	TextureCache.clear();
	SpriteTextureCache.clear();
	BatchesUsed = 0;
	FacesUsed = 0;
	if (sky) AddSky(level, vp, viewport);
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
		AddFFloorPlanes(sub, sector, eye3);
	}
	// The opaque sprites with the world; the see-through ones after every opaque batch, far
	// to near (I_3DSGpuWorldQueueBlended draws them in that order).
	for (const FSprite &sprite : Sprites)
		if (sprite.Style == 0) AddSpriteQuad(sprite);
	SeeThrough.clear();
	for (FSprite &sprite : Sprites)
	{
		if (sprite.Style == 0) continue;
		const float depth = float(ViewDepth((sprite.Ax + sprite.Bx) * 0.5, (sprite.Ay + sprite.By) * 0.5));
		SeeThrough.push_back(FSeeThrough{ depth, &sprite, nullptr });
	}
	// Additive faces commute: those of one texture and alpha become one draw.
	for (size_t i = 0; i < FacesUsed; ++i)
	{
		FFace &face = Faces[i];
		if (face.Texture == nullptr || face.Vertices.empty()) continue;
		if (face.Style == E3DSGpuWorldStyle::Add)
		{
			bool merged = false;
			for (size_t j = 0; j < i && !merged; ++j)
			{
				FFace &other = Faces[j];
				if (other.Style == E3DSGpuWorldStyle::Add && other.Texture == face.Texture &&
					other.Alpha == face.Alpha && !other.Vertices.empty())
				{
					other.Vertices.insert(other.Vertices.end(), face.Vertices.begin(), face.Vertices.end());
					face.Vertices.clear();
					merged = true;
				}
			}
			if (merged) continue;
		}
		SeeThrough.push_back(FSeeThrough{ face.Depth, nullptr, &face });
	}
	std::sort(SeeThrough.begin(), SeeThrough.end(),
		[](const FSeeThrough &a, const FSeeThrough &b) { return a.Depth > b.Depth; });

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

	for (const FSeeThrough &item : SeeThrough)
	{
		if (item.Sprite != nullptr)
		{
			AddSpriteQuad(*item.Sprite);
			continue;
		}
		const FFace &face = *item.Face;
		FVertex *out = I_3DSGpuWorldQueueBlended(face.Texture, static_cast<int>(face.Vertices.size()),
			face.Style, face.Alpha);
		if (out == nullptr) continue;
		std::memcpy(out, face.Vertices.data(), face.Vertices.size() * sizeof(FVertex));
		vertices += static_cast<uint32_t>(face.Vertices.size());
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

bool I_3DSGpuWorldEnabled()
{
	return lod3ds_gpu_world || !LOD3DS_DEBUG_TOOLS;
}

#else

void I_3DSGpuWorldBeginScene(swrenderer::RenderThread *) {}
void I_3DSGpuWorldSuspend(bool) {}
void I_3DSGpuWorldAddSprite(FGameTexture *, double, double, double, double, double, double, bool,
	int, bool, const sector_t *, int, double, uint32_t) {}
void I_3DSGpuWorldCapture(swrenderer::RenderThread *) {}
bool I_3DSGpuWorldActive() { return false; }
bool I_3DSGpuWorldEnabled() { return false; }

#endif
