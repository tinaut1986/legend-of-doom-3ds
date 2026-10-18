//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2006-2016 Christoph Oelckers
// Copyright 2016 Magnus Norddahl
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//		BSP traversal, handling of LineSegs for rendering.
//
//-----------------------------------------------------------------------------


#include <stdlib.h>

#include "templates.h"

#include "doomdef.h"

#include "m_bbox.h"

#include "engineerrors.h"
#include "p_lnspec.h"
#include "p_setup.h"
#include "texturemanager.h"

#include "swrenderer/drawers/r_draw.h"
#include "swrenderer/plane/r_visibleplane.h"
#include "swrenderer/plane/r_visibleplanelist.h"
#include "swrenderer/things/r_sprite.h"
#include "swrenderer/things/r_wallsprite.h"
#include "swrenderer/things/r_voxel.h"
#include "swrenderer/things/r_particle.h"
#include "swrenderer/things/r_model.h"
#include "swrenderer/segments/r_clipsegment.h"
#include "swrenderer/line/r_wallsetup.h"
#include "swrenderer/line/r_farclip_line.h"
#include "swrenderer/scene/r_scene.h"
#include "swrenderer/scene/r_light.h"
#include "swrenderer/viewport/r_viewport.h"
#include "swrenderer/r_renderthread.h"
#include "swrenderer/r_swcolormaps.h"
#include "r_3dfloors.h"
#include "r_portal.h"
#include "a_sharedglobal.h"
#include "g_level.h"
#include "p_effect.h"
#include "c_console.h"
#include "p_maputl.h"

// State.
#include "doomstat.h"
#include "r_state.h"
#include "r_opaque_pass.h"
#include "v_palette.h"
#include "r_sky.h"
#include "po_man.h"
#include "r_data/colormaps.h"
#include "g_levellocals.h"

#ifdef __3DS__
#include "common/platform/3ds/diagnostics_3ds.h"
#include "common/platform/3ds/perf_3ds.h"
#include "c_dispatch.h"
#include <3ds.h>
#endif

EXTERN_CVAR(Bool, r_fullbrightignoresectorcolor);
EXTERN_CVAR(Bool, r_drawvoxels);
EXTERN_CVAR(Bool, r_debug_disable_vis_filter);
extern uint32_t r_renderercaps;

double model_distance_cull = 1e16;

EXTERN_CVAR(Float, r_actorspriteshadowdist)

namespace
{
	double sprite_distance_cull = 1e16;
	double line_distance_cull = 1e16;
}

CUSTOM_CVAR(Float, r_sprite_distance_cull, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (r_sprite_distance_cull > 0.0)
	{
		sprite_distance_cull = r_sprite_distance_cull * r_sprite_distance_cull;
	}
	else
	{
		sprite_distance_cull = 1e16;
	}
}

CUSTOM_CVAR(Float, r_line_distance_cull, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (r_line_distance_cull > 0.0)
	{
		line_distance_cull = r_line_distance_cull * r_line_distance_cull;
	}
	else
	{
		line_distance_cull = 1e16;
	}
}

CUSTOM_CVAR(Float, r_model_distance_cull, 1024, 0/*CVAR_ARCHIVE | CVAR_GLOBALCONFIG*/) // Experimental for the moment until a good default is chosen 
{
	if (r_model_distance_cull > 0.0)
	{
		model_distance_cull = r_model_distance_cull * r_model_distance_cull;
	}
	else
	{
		model_distance_cull = 1e16;
	}
}

namespace
{
	double Map01CullDistanceSquared()
	{
		#ifdef __3DS__
		return Map01DistanceFogEnd * Map01DistanceFogEnd;
		#else
		return line_distance_cull;
		#endif
	}

	bool LineIsOutsideDistance(const DVector2 &viewpoint, const DVector2 &start,
		const DVector2 &end, double distanceSquared)
	{
		const DVector2 delta = end - start;
		const double lengthSquared = delta.LengthSquared();
		double fraction = lengthSquared > 0.0 ?
			((viewpoint - start) | delta) / lengthSquared : 0.0;
		fraction = clamp(fraction, 0.0, 1.0);
		return (start + delta * fraction - viewpoint).LengthSquared() >
			distanceSquared;
	}

	#ifdef __3DS__
	bool Map01RootDistanceCullActive(swrenderer::RenderThread *thread)
	{
		auto viewport = thread != nullptr ? thread->Viewport.get() : nullptr;
		auto level = viewport != nullptr ? viewport->Level() : nullptr;
		auto sector = viewport != nullptr ? viewport->viewpoint.sector : nullptr;
		return level != nullptr && level->MapName.CompareNoCase("MAP01") == 0 &&
			sector != nullptr &&
			(sector->GetTexture(sector_t::ceiling) == skyflatnum ||
				sector->ValidatePortal(sector_t::ceiling) != nullptr) &&
			thread->Portal->CurrentPortalUniq == 0 &&
			thread->Clip3D->CurrentSkybox == 0;
	}

	bool Map01ExteriorDistanceCullCandidate(const sector_t *sector)
	{
		return Is3DSMap01ExteriorSector(sector);
	}

	#else
	bool Map01RootDistanceCullActive(swrenderer::RenderThread *)
	{
		return false;
	}

	bool Map01ExteriorDistanceCullCandidate(const sector_t *)
	{
		return false;
	}
	#endif
}

namespace swrenderer
{
	RenderOpaquePass::RenderOpaquePass(RenderThread *thread) : renderline(thread)
	{
		Thread = thread;
	}

	sector_t *RenderOpaquePass::FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, seg_t *backline, int backx1, int backx2, double frontcz1, double frontcz2)
	{
		// If player's view height is underneath fake floor, lower the
		// drawn ceiling to be just under the floor height, and replace
		// the drawn floor and ceiling textures, and light level, with
		// the control sector's.
		//
		// Similar for ceiling, only reflected.

		// [RH] allow per-plane lighting
		if (floorlightlevel != nullptr)
		{
			*floorlightlevel = sec->GetFloorLight();
		}

		if (ceilinglightlevel != nullptr)
		{
			*ceilinglightlevel = sec->GetCeilingLight();
		}

		FakeSide = WaterFakeSide::Center;

		const sector_t *s = sec->GetHeightSec();
		if (s != nullptr)
		{
			sector_t *heightsec = Thread->Viewport->viewpoint.sector->heightsec;
			bool underwater = r_fakingunderwater ||
				(heightsec && heightsec->floorplane.PointOnSide(Thread->Viewport->viewpoint.Pos) <= 0);
			bool doorunderwater = false;
			int diffTex = (s->MoreFlags & SECMF_CLIPFAKEPLANES);

			// Replace sector being drawn with a copy to be hacked
			*tempsec = *sec;

			// Replace floor and ceiling height with control sector's heights.
			if (diffTex)
			{
				if (s->floorplane.CopyPlaneIfValid(&tempsec->floorplane, &sec->ceilingplane))
				{
					tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::floor), false);
				}
				else if (s->MoreFlags & SECMF_FAKEFLOORONLY)
				{
					if (underwater)
					{
						tempsec->Colormap = s->Colormap;
						if (!(s->MoreFlags & SECMF_NOFAKELIGHT))
						{
							tempsec->lightlevel = s->lightlevel;

							if (floorlightlevel != nullptr)
							{
								*floorlightlevel = s->GetFloorLight();
							}

							if (ceilinglightlevel != nullptr)
							{
								*ceilinglightlevel = s->GetCeilingLight();
							}
						}
						FakeSide = WaterFakeSide::BelowFloor;
						return tempsec;
					}
					return sec;
				}
			}
			else
			{
				tempsec->floorplane = s->floorplane;
			}

			if (!(s->MoreFlags & SECMF_FAKEFLOORONLY))
			{
				if (diffTex)
				{
					if (s->ceilingplane.CopyPlaneIfValid(&tempsec->ceilingplane, &sec->floorplane))
					{
						tempsec->SetTexture(sector_t::ceiling, s->GetTexture(sector_t::ceiling), false);
					}
				}
				else
				{
					tempsec->ceilingplane = s->ceilingplane;
				}
			}

			double refceilz = s->ceilingplane.ZatPoint(Thread->Viewport->viewpoint.Pos);
			double orgceilz = sec->ceilingplane.ZatPoint(Thread->Viewport->viewpoint.Pos);

#if 1
			// [RH] Allow viewing underwater areas through doors/windows that
			// are underwater but not in a water sector themselves.
			// Only works if you cannot see the top surface of any deep water
			// sectors at the same time.
			if (backline && !r_fakingunderwater && backline->frontsector->heightsec == nullptr)
			{
				if (frontcz1 <= s->floorplane.ZatPoint(backline->v1) &&
					frontcz2 <= s->floorplane.ZatPoint(backline->v2))
				{
					// Check that the window is actually visible
					for (int z = backx1; z < backx2; ++z)
					{
						if (floorclip[z] > ceilingclip[z])
						{
							doorunderwater = true;
							r_fakingunderwater = true;
							break;
						}
					}
				}
			}
#endif

			if (underwater || doorunderwater)
			{
				tempsec->floorplane = sec->floorplane;
				tempsec->ceilingplane = s->floorplane;
				tempsec->ceilingplane.FlipVert();
				tempsec->ceilingplane.ChangeHeight(-1 / 65536.);
				tempsec->Colormap = s->Colormap;
			}

			// killough 11/98: prevent sudden light changes from non-water sectors:
			if ((underwater && !backline) || doorunderwater)
			{					// head-below-floor hack
				tempsec->SetTexture(sector_t::floor, diffTex ? sec->GetTexture(sector_t::floor) : s->GetTexture(sector_t::floor), false);
				tempsec->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;

				tempsec->ceilingplane = s->floorplane;
				tempsec->ceilingplane.FlipVert();
				tempsec->ceilingplane.ChangeHeight(-1 / 65536.);
				if (s->GetTexture(sector_t::ceiling) == skyflatnum)
				{
					tempsec->floorplane = tempsec->ceilingplane;
					tempsec->floorplane.FlipVert();
					tempsec->floorplane.ChangeHeight(+1 / 65536.);
					tempsec->SetTexture(sector_t::ceiling, tempsec->GetTexture(sector_t::floor), false);
					tempsec->planes[sector_t::ceiling].xform = tempsec->planes[sector_t::floor].xform;
				}
				else
				{
					tempsec->SetTexture(sector_t::ceiling, diffTex ? s->GetTexture(sector_t::floor) : s->GetTexture(sector_t::ceiling), false);
					tempsec->planes[sector_t::ceiling].xform = s->planes[sector_t::ceiling].xform;
				}

				if (!(s->MoreFlags & SECMF_NOFAKELIGHT))
				{
					tempsec->lightlevel = s->lightlevel;

					if (floorlightlevel != nullptr)
					{
						*floorlightlevel = s->GetFloorLight();
					}

					if (ceilinglightlevel != nullptr)
					{
						*ceilinglightlevel = s->GetCeilingLight();
					}
				}
				FakeSide = WaterFakeSide::BelowFloor;
			}
			else if (heightsec && heightsec->ceilingplane.PointOnSide(Thread->Viewport->viewpoint.Pos) <= 0 &&
				orgceilz > refceilz && !(s->MoreFlags & SECMF_FAKEFLOORONLY))
			{	// Above-ceiling hack
				tempsec->ceilingplane = s->ceilingplane;
				tempsec->floorplane = s->ceilingplane;
				tempsec->floorplane.FlipVert();
				tempsec->floorplane.ChangeHeight(+1 / 65536.);
				tempsec->Colormap = s->Colormap;

				tempsec->SetTexture(sector_t::ceiling, diffTex ? sec->GetTexture(sector_t::ceiling) : s->GetTexture(sector_t::ceiling), false);
				tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::ceiling), false);
				tempsec->planes[sector_t::ceiling].xform = tempsec->planes[sector_t::floor].xform = s->planes[sector_t::ceiling].xform;

				if (s->GetTexture(sector_t::floor) != skyflatnum)
				{
					tempsec->ceilingplane = sec->ceilingplane;
					tempsec->SetTexture(sector_t::floor, s->GetTexture(sector_t::floor), false);
					tempsec->planes[sector_t::floor].xform = s->planes[sector_t::floor].xform;
				}

				if (!(s->MoreFlags & SECMF_NOFAKELIGHT))
				{
					tempsec->lightlevel = s->lightlevel;

					if (floorlightlevel != nullptr)
					{
						*floorlightlevel = s->GetFloorLight();
					}

					if (ceilinglightlevel != nullptr)
					{
						*ceilinglightlevel = s->GetCeilingLight();
					}
				}
				FakeSide = WaterFakeSide::AboveCeiling;
			}
			sec = tempsec;					// Use other sector
		}
		return sec;
	}



	// Checks BSP node/subtree bounding box.
	// Returns true if some part of the bbox might be visible.
	bool RenderOpaquePass::CheckBBox(float *bspcoord)
	{
		static const int checkcoord[12][4] =
		{
			{ 3,0,2,1 },
			{ 3,0,2,0 },
			{ 3,1,2,0 },
			{ 0 },
			{ 2,0,2,1 },
			{ 0,0,0,0 },
			{ 3,1,3,0 },
			{ 0 },
			{ 2,0,3,1 },
			{ 2,1,3,1 },
			{ 2,1,3,0 }
		};

		int 				boxx;
		int 				boxy;
		int 				boxpos;

		double	 			x1, y1, x2, y2;
		double				rx1, ry1, rx2, ry2;
		int					sx1, sx2;

		// Find the corners of the box
		// that define the edges from current viewpoint.
		if (Thread->Viewport->viewpoint.Pos.X <= bspcoord[BOXLEFT])
			boxx = 0;
		else if (Thread->Viewport->viewpoint.Pos.X < bspcoord[BOXRIGHT])
			boxx = 1;
		else
			boxx = 2;

		if (Thread->Viewport->viewpoint.Pos.Y >= bspcoord[BOXTOP])
			boxy = 0;
		else if (Thread->Viewport->viewpoint.Pos.Y > bspcoord[BOXBOTTOM])
			boxy = 1;
		else
			boxy = 2;

		boxpos = (boxy << 2) + boxx;
		if (boxpos == 5)
			return true;

		x1 = bspcoord[checkcoord[boxpos][0]] - Thread->Viewport->viewpoint.Pos.X;
		y1 = bspcoord[checkcoord[boxpos][1]] - Thread->Viewport->viewpoint.Pos.Y;
		x2 = bspcoord[checkcoord[boxpos][2]] - Thread->Viewport->viewpoint.Pos.X;
		y2 = bspcoord[checkcoord[boxpos][3]] - Thread->Viewport->viewpoint.Pos.Y;

		// check clip list for an open space

		// Sitting on a line?
		if (y1 * (x1 - x2) + x1 * (y2 - y1) >= -EQUAL_EPSILON)
			return true;

		rx1 = x1 * Thread->Viewport->viewpoint.Sin - y1 * Thread->Viewport->viewpoint.Cos;
		rx2 = x2 * Thread->Viewport->viewpoint.Sin - y2 * Thread->Viewport->viewpoint.Cos;
		ry1 = x1 * Thread->Viewport->viewpoint.TanCos + y1 * Thread->Viewport->viewpoint.TanSin;
		ry2 = x2 * Thread->Viewport->viewpoint.TanCos + y2 * Thread->Viewport->viewpoint.TanSin;

		if (Thread->Portal->MirrorFlags & RF_XFLIP)
		{
			double t = -rx1;
			rx1 = -rx2;
			rx2 = t;
			std::swap(ry1, ry2);
		}
		
		auto viewport = Thread->Viewport.get();

		if (rx1 >= -ry1)
		{
			if (rx1 > ry1) return false;	// left edge is off the right side
			if (ry1 == 0) return false;
			sx1 = xs_RoundToInt(viewport->CenterX + rx1 * viewport->CenterX / ry1);
		}
		else
		{
			if (rx2 < -ry2) return false;	// wall is off the left side
			if (rx1 - rx2 - ry2 + ry1 == 0) return false;	// wall does not intersect view volume
			sx1 = 0;
		}

		if (rx2 <= ry2)
		{
			if (rx2 < -ry2) return false;	// right edge is off the left side
			if (ry2 == 0) return false;
			sx2 = xs_RoundToInt(viewport->CenterX + rx2 * viewport->CenterX / ry2);
		}
		else
		{
			if (rx1 > ry1) return false;	// wall is off the right side
			if (ry2 - ry1 - rx2 + rx1 == 0) return false;	// wall does not intersect view volume
			sx2 = viewwidth;
		}

		// Find the first clippost that touches the source post
		//	(adjacent pixels are touching).

		return Thread->ClipSegments->IsVisible(sx1, sx2);
	}

	void RenderOpaquePass::AddPolyobjs(subsector_t *sub)
	{
		Thread->PreparePolyObject(sub);

		if (sub->BSP->Nodes.Size() == 0)
		{
			RenderSubsector(&sub->BSP->Subsectors[0]);
		}
		else
		{
			RenderBSPNode(&sub->BSP->Nodes.Last());
		}
	}

	// kg3D - add fake segs, never rendered
	void RenderOpaquePass::FakeDrawLoop(subsector_t *sub, sector_t *frontsector, VisiblePlane *floorplane, VisiblePlane *ceilingplane, Fake3DOpaque opaque3dfloor)
	{
		int 		 count;
		seg_t*		 line;

		count = sub->numlines;
		line = sub->firstline;

		while (count--)
		{
			if ((line->sidedef) && !(line->sidedef->Flags & WALLF_POLYOBJ))
			{
				renderline.Render(line, InSubsector, frontsector, nullptr, floorplane, ceilingplane, opaque3dfloor);
			}
			line++;
		}
	}

#ifdef __3DS__
	extern bool TraceSolid3DS; // r_line.cpp
	static RenderOpaquePass *LastMainOpaquePass3DS;
	// lod3ds_visible (debug): the subsectors the last main walk found visible, as index:sector.
	CCMD(lod3ds_visible)
	{
		if (LastMainOpaquePass3DS == nullptr || primaryLevel == nullptr) return;
		FString line;
		for (uint32_t index : LastMainOpaquePass3DS->VisibleSubsectors())
		{
			if (index >= primaryLevel->subsectors.Size()) continue;
			line.AppendFormat("%u:%d ", index, primaryLevel->subsectors[index].sector->Index());
		}
		Printf("visible: %s\n", line.GetChars());
	}

	// lod3ds_sectorsubs SECTOR (debug): that sector's subsectors, their segs' ends, and whether
	// the last main walk found each visible.
	CCMD(lod3ds_sectorsubs)
	{
		if (argv.argc() < 2 || primaryLevel == nullptr) return;
		const int wanted = atoi(argv[1]);
		for (unsigned i = 0; i < primaryLevel->subsectors.Size(); ++i)
		{
			const subsector_t &sub = primaryLevel->subsectors[i];
			if (sub.sector == nullptr || sub.sector->Index() != wanted) continue;
			bool visible = false;
			if (LastMainOpaquePass3DS != nullptr)
				for (uint32_t index : LastMainOpaquePass3DS->VisibleSubsectors())
					if (index == i) { visible = true; break; }
			FString line;
			line.AppendFormat("sub %u %s:", i, visible ? "VISIBLE" : "hidden");
			for (uint32_t k = 0; k < sub.numlines; ++k)
			{
				const seg_t &seg = sub.firstline[k];
				line.AppendFormat(" (%.0f,%.0f)", seg.v1->fX(), seg.v1->fY());
				if (seg.linedef != nullptr) line.AppendFormat("L%d", seg.linedef->Index());
			}
			Printf("%s\n", line.GetChars());
		}
	}

	// The walk's parts, summed over a RenderScene and handed to lod3ds_sections at its end.
	static uint64_t WalkPlaneTicks, WalkSpriteTicks, WalkLineTicks, WalkSubsectors;
#endif

	void RenderOpaquePass::RenderSubsector(subsector_t *sub)
	{
#ifdef __3DS__
		const uint64_t walkStart = svcGetSystemTick();
		++WalkSubsectors;
#endif
		// Determine floor/ceiling planes.
		// Add sprites of things in sector.
		// Draw one or more line segments.

		bool outersubsector;

		if (InSubsector != nullptr)
		{ // InSubsector is not nullptr. This means we are rendering from a mini-BSP.
			outersubsector = false;
		}
		else
		{
			outersubsector = true;
			InSubsector = sub;

			// Mark the visual sorting depth of this subsector
			uint32_t subsectorDepth = (uint32_t)PvsSubsectors.size();
			SubsectorDepths[sub->Index()] = subsectorDepth;
			PvsSubsectors.push_back(sub->Index());
		}

		auto Level = sub->sector->Level;
#ifdef RANGECHECK
		if (outersubsector && (unsigned)sub->Index() >= Level->subsectors.Size())
			I_Error("RenderSubsector: ss %i with numss = %u", sub->Index(), Level->subsectors.Size());
#endif

		if (sub->polys)
		{ // Render the polyobjs in the subsector first
			AddPolyobjs(sub);
			if (outersubsector)
			{
				InSubsector = nullptr;
			}
			return;
		}

		sub->sector->MoreFlags |= SECMF_DRAWN;

		// killough 3/8/98, 4/4/98: Deep water / fake ceiling effect
		sector_t tempsec;
		int floorlightlevel, ceilinglightlevel;
		sector_t *frontsector = FakeFlat(sub->sector, &tempsec, &floorlightlevel, &ceilinglightlevel, nullptr, 0, 0, 0, 0);

		// [RH] set foggy flag
		bool foggy = Level->fadeto || frontsector->Colormap.FadeColor ||
			(Level->flags & LEVEL_HASFADETABLE)
			#ifdef __3DS__
			|| Is3DSMap01ExteriorSector(frontsector)
			#endif
			;

		// kg3D - fake lights
		CameraLight *cameraLight = CameraLight::Instance();
		FDynamicColormap *basecolormap;
		int adjusted_ceilinglightlevel = ceilinglightlevel;
		if (cameraLight->FixedLightLevel() < 0 && frontsector->e && frontsector->e->XFloor.lightlist.Size())
		{
			lightlist_t *light = P_GetPlaneLight(frontsector, &frontsector->ceilingplane, false);
			basecolormap = GetColorTable(light->extra_colormap, frontsector->SpecialColors[sector_t::ceiling]);
			// If this is the real ceiling, don't discard plane lighting R_FakeFlat()
			// accounted for.
			if (light->p_lightlevel != &frontsector->lightlevel)
			{
				adjusted_ceilinglightlevel = *light->p_lightlevel;
			}
		}
		else
		{
			basecolormap = (r_fullbrightignoresectorcolor && cameraLight->FixedLightLevel() >= 0) ? &FullNormalLight : GetColorTable(frontsector->Colormap, frontsector->SpecialColors[sector_t::ceiling]);
		}
		#ifdef __3DS__
		basecolormap = Apply3DSMap01DistanceFog(frontsector, basecolormap);
		#endif

		FSectorPortal *portal = frontsector->ValidatePortal(sector_t::ceiling);

		VisiblePlane *ceilingplane = nullptr;
		if (frontsector->ceilingplane.PointOnSide(Thread->Viewport->viewpoint.Pos) > 0 ||
			frontsector->GetTexture(sector_t::ceiling) == skyflatnum ||
			portal ||
			(frontsector->GetHeightSec() && frontsector->heightsec->GetTexture(sector_t::floor) == skyflatnum))
		{
			ceilingplane = Thread->PlaneList->FindPlane(
				frontsector->ceilingplane,
				frontsector->GetTexture(sector_t::ceiling),
				adjusted_ceilinglightlevel, foggy,
				frontsector->GetAlpha(sector_t::ceiling),
				!!(frontsector->GetFlags(sector_t::ceiling) & PLANEF_ADDITIVE),
				frontsector->planes[sector_t::ceiling].xform,
				frontsector->sky,
				portal,
				basecolormap,
				Fake3DOpaque::Normal,
				0);

			ceilingplane->AddLights(Thread, sub->section->lighthead);
		}

		int adjusted_floorlightlevel = floorlightlevel;
		if (cameraLight->FixedLightLevel() < 0 && frontsector->e && frontsector->e->XFloor.lightlist.Size())
		{
			lightlist_t *light = P_GetPlaneLight(frontsector, &frontsector->floorplane, false);
			basecolormap = GetColorTable(light->extra_colormap, frontsector->SpecialColors[sector_t::floor]);
			// If this is the real floor, don't discard plane lighting R_FakeFlat()
			// accounted for.
			if (light->p_lightlevel != &frontsector->lightlevel)
			{
				adjusted_floorlightlevel = *light->p_lightlevel;
			}
		}
		else
		{
			basecolormap = (r_fullbrightignoresectorcolor && cameraLight->FixedLightLevel() >= 0) ? &FullNormalLight : GetColorTable(frontsector->Colormap, frontsector->SpecialColors[sector_t::floor]);
		}
		#ifdef __3DS__
		basecolormap = Apply3DSMap01DistanceFog(frontsector, basecolormap);
		#endif

		portal = frontsector->ValidatePortal(sector_t::floor);

		VisiblePlane *floorplane = nullptr;
		if (frontsector->floorplane.PointOnSide(Thread->Viewport->viewpoint.Pos) > 0 ||
			frontsector->GetTexture(sector_t::floor) == skyflatnum ||
			portal ||
			(frontsector->GetHeightSec() && frontsector->heightsec->GetTexture(sector_t::ceiling) == skyflatnum))
		{
			floorplane = Thread->PlaneList->FindPlane(frontsector->floorplane,
				frontsector->GetTexture(sector_t::floor),
				adjusted_floorlightlevel, foggy,
				frontsector->GetAlpha(sector_t::floor),
				!!(frontsector->GetFlags(sector_t::floor) & PLANEF_ADDITIVE),
				frontsector->planes[sector_t::floor].xform,
				frontsector->sky,
				portal,
				basecolormap,
				Fake3DOpaque::Normal,
				0);

			floorplane->AddLights(Thread, sub->section->lighthead);
		}

		Add3DFloorPlanes(sub, frontsector, basecolormap, foggy, adjusted_ceilinglightlevel, adjusted_floorlightlevel);

		// killough 9/18/98: Fix underwater slowdown, by passing real sector 
		// instead of fake one. Improve sprite lighting by basing sprite
		// lightlevels on floor & ceiling lightlevels in the surrounding area.
		// [RH] Handle sprite lighting like Duke 3D: If the ceiling is a sky, sprites are lit by
		// it, otherwise they are lit by the floor.
#ifdef __3DS__
		const uint64_t walkPlanes = svcGetSystemTick();
		WalkPlaneTicks += walkPlanes - walkStart;
#endif
		auto nc = !!(frontsector->Level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING);
		auto spriteColormap = GetSpriteColorTable(frontsector->Colormap,
			frontsector->SpecialColors[sector_t::sprites], nc);
		#ifdef __3DS__
		spriteColormap = Apply3DSMap01DistanceFog(frontsector, spriteColormap);
		#endif
		AddSprites(sub->sector, frontsector->GetTexture(sector_t::ceiling) == skyflatnum ?
			ceilinglightlevel : floorlightlevel, FakeSide, foggy, spriteColormap);

		// [RH] Add particles
		if ((unsigned int)(sub->Index()) < Level->subsectors.Size())
		{ // Only do it for the main BSP.
			int lightlevel = (floorlightlevel + ceilinglightlevel) / 2;
			for (int i = frontsector->Level->ParticlesInSubsec[sub->Index()]; i != NO_PARTICLE; i = frontsector->Level->Particles[i].snext)
			{
				RenderParticle::Project(Thread, &frontsector->Level->Particles[i], sub->sector, lightlevel, FakeSide, foggy);
			}
		}

		DVector2 viewpointPos = Thread->Viewport->viewpoint.Pos.XY();
#ifdef __3DS__
		const uint64_t walkSprites = svcGetSystemTick();
		WalkSpriteTicks += walkSprites - walkPlanes;
#endif

		seg_t *line = sub->firstline;
		int count = sub->numlines;
		while (count--)
		{
			// Keep every subsector in the normal BSP traversal so its planes and
			// occluders remain topologically complete. FarClipLine preserves the
			// projected clip/visplane contribution of a distant segment without
			// paying for its textured wall draw.
			if (Map01RootDistanceCullActive(Thread) &&
				Map01ExteriorDistanceCullCandidate(frontsector) &&
				LineIsOutsideDistance(viewpointPos, line->v1->fPos(),
				line->v2->fPos(), Map01CullDistanceSquared()))
			{
				#ifdef __3DS__
				I_3DSRecordDrawDistanceLineCull();
				#endif
				FarClipLine farclip(Thread);
				farclip.Render(line, InSubsector, floorplane, ceilingplane, Fake3DOpaque::Normal);
			}
			else if (!outersubsector || line->sidedef == nullptr || !(line->sidedef->Flags & WALLF_POLYOBJ))
			{
				Add3DFloorLine(line, frontsector);
				renderline.Render(line, InSubsector, frontsector, nullptr, floorplane, ceilingplane, Fake3DOpaque::Normal); // now real
			}
			line++;
		}
#ifdef __3DS__
		WalkLineTicks += svcGetSystemTick() - walkSprites;
#endif
		if (outersubsector)
		{
			InSubsector = nullptr;
		}
	}

	void RenderOpaquePass::Add3DFloorLine(seg_t *line, sector_t *frontsector)
	{
		// kg3D - fake planes bounding calculation
		if (r_3dfloors && line->backsector && frontsector->e && line->backsector->e->XFloor.ffloors.Size())
		{
			Clip3DFloors *clip3d = Thread->Clip3D.get();
			for (unsigned int i = 0; i < line->backsector->e->XFloor.ffloors.Size(); i++)
			{
				clip3d->SetFakeFloor(line->backsector->e->XFloor.ffloors[i]);
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_EXISTS)) continue;
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_RENDERPLANES)) continue;
				if (!clip3d->fakeFloor->fakeFloor->model) continue;
				Fake3DOpaque opaque3dfloor = Fake3DOpaque::FakeBack;
				sector_t tempsec = *clip3d->fakeFloor->fakeFloor->model;
				tempsec.floorplane = *clip3d->fakeFloor->fakeFloor->top.plane;
				tempsec.ceilingplane = *clip3d->fakeFloor->fakeFloor->bottom.plane;
				if (clip3d->fakeFloor->validcount != validcount)
				{
					clip3d->fakeFloor->validcount = validcount;
					clip3d->NewClip();
				}
				renderline.Render(line, InSubsector, frontsector, &tempsec, nullptr, nullptr, opaque3dfloor); // fake
			}
			clip3d->fakeFloor = nullptr;
		}
	}

	void RenderOpaquePass::Add3DFloorPlanes(subsector_t *sub, sector_t *frontsector, FDynamicColormap *basecolormap, bool foggy, int ceilinglightlevel, int floorlightlevel)
	{
		// kg3D - fake planes rendering
		if (r_3dfloors && frontsector->e && frontsector->e->XFloor.ffloors.Size())
		{
			CameraLight *cameraLight = CameraLight::Instance();
			Clip3DFloors *clip3d = Thread->Clip3D.get();

			// first check all floors
			for (int i = 0; i < (int)frontsector->e->XFloor.ffloors.Size(); i++)
			{
				clip3d->SetFakeFloor(frontsector->e->XFloor.ffloors[i]);
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_EXISTS)) continue;
				if (!clip3d->fakeFloor->fakeFloor->model) continue;
				if (clip3d->fakeFloor->fakeFloor->bottom.plane->isSlope()) continue;
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_NOSHADE) || (clip3d->fakeFloor->fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
				{
					clip3d->AddHeight(clip3d->fakeFloor->fakeFloor->top.plane, frontsector);
				}
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_RENDERPLANES)) continue;
				if (clip3d->fakeFloor->fakeFloor->alpha == 0) continue;
				if (clip3d->fakeFloor->fakeFloor->flags & FF_THISINSIDE && clip3d->fakeFloor->fakeFloor->flags & FF_INVERTSECTOR) continue;
				fixed_t fakeAlpha = MIN<fixed_t>(Scale(clip3d->fakeFloor->fakeFloor->alpha, OPAQUE, 255), OPAQUE);
				if (clip3d->fakeFloor->validcount != validcount)
				{
					clip3d->fakeFloor->validcount = validcount;
					clip3d->NewClip();
				}
				double fakeHeight = clip3d->fakeFloor->fakeFloor->top.plane->ZatPoint(frontsector->centerspot);
				if (fakeHeight < Thread->Viewport->viewpoint.Pos.Z &&
					fakeHeight > frontsector->floorplane.ZatPoint(frontsector->centerspot))
				{
					sector_t tempsec = *clip3d->fakeFloor->fakeFloor->model;
					tempsec.floorplane = *clip3d->fakeFloor->fakeFloor->top.plane;
					tempsec.ceilingplane = *clip3d->fakeFloor->fakeFloor->bottom.plane;

					int	position;
					if (!(clip3d->fakeFloor->fakeFloor->flags & FF_THISINSIDE) && !(clip3d->fakeFloor->fakeFloor->flags & FF_INVERTSECTOR))
					{
						tempsec.SetTexture(sector_t::floor, tempsec.GetTexture(sector_t::ceiling));
						position = sector_t::ceiling;
					}
					else
					{
						position = sector_t::floor;
					}

					if (cameraLight->FixedLightLevel() < 0 && sub->sector->e->XFloor.lightlist.Size())
					{
						lightlist_t *light = P_GetPlaneLight(sub->sector, &tempsec.floorplane, false);
						basecolormap = GetColorTable(light->extra_colormap);
						floorlightlevel = *light->p_lightlevel;
					}

					VisiblePlane *floorplane3d = Thread->PlaneList->FindPlane(
						tempsec.floorplane,
						tempsec.GetTexture(sector_t::floor),
						floorlightlevel, foggy,
						tempsec.GetAlpha(sector_t::floor),
						!!(clip3d->fakeFloor->fakeFloor->flags & FF_ADDITIVETRANS),
						tempsec.planes[position].xform,
						tempsec.sky,
						nullptr,
						basecolormap,
						Fake3DOpaque::FakeFloor,
						fakeAlpha);

					floorplane3d->AddLights(Thread, sub->section->lighthead);

					FakeDrawLoop(sub, &tempsec, floorplane3d, nullptr, Fake3DOpaque::FakeFloor);
				}
			}
			// and now ceilings
			for (unsigned int i = 0; i < frontsector->e->XFloor.ffloors.Size(); i++)
			{
				clip3d->SetFakeFloor(frontsector->e->XFloor.ffloors[i]);
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_EXISTS)) continue;
				if (!clip3d->fakeFloor->fakeFloor->model) continue;
				if (clip3d->fakeFloor->fakeFloor->top.plane->isSlope()) continue;
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_NOSHADE) || (clip3d->fakeFloor->fakeFloor->flags & (FF_RENDERPLANES | FF_RENDERSIDES)))
				{
					clip3d->AddHeight(clip3d->fakeFloor->fakeFloor->bottom.plane, frontsector);
				}
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_RENDERPLANES)) continue;
				if (clip3d->fakeFloor->fakeFloor->alpha == 0) continue;
				if (!(clip3d->fakeFloor->fakeFloor->flags & FF_THISINSIDE) && (clip3d->fakeFloor->fakeFloor->flags & (FF_SWIMMABLE | FF_INVERTSECTOR)) == (FF_SWIMMABLE | FF_INVERTSECTOR)) continue;
				fixed_t fakeAlpha = MIN<fixed_t>(Scale(clip3d->fakeFloor->fakeFloor->alpha, OPAQUE, 255), OPAQUE);

				if (clip3d->fakeFloor->validcount != validcount)
				{
					clip3d->fakeFloor->validcount = validcount;
					clip3d->NewClip();
				}
				double fakeHeight = clip3d->fakeFloor->fakeFloor->bottom.plane->ZatPoint(frontsector->centerspot);
				if (fakeHeight > Thread->Viewport->viewpoint.Pos.Z &&
					fakeHeight < frontsector->ceilingplane.ZatPoint(frontsector->centerspot))
				{
					sector_t tempsec = *clip3d->fakeFloor->fakeFloor->model;
					tempsec.floorplane = *clip3d->fakeFloor->fakeFloor->top.plane;
					tempsec.ceilingplane = *clip3d->fakeFloor->fakeFloor->bottom.plane;

					int	position;
					if ((!(clip3d->fakeFloor->fakeFloor->flags & FF_THISINSIDE) && !(clip3d->fakeFloor->fakeFloor->flags & FF_INVERTSECTOR)) ||
						(clip3d->fakeFloor->fakeFloor->flags & FF_THISINSIDE && clip3d->fakeFloor->fakeFloor->flags & FF_INVERTSECTOR))
					{
						tempsec.SetTexture(sector_t::ceiling, tempsec.GetTexture(sector_t::floor));
						position = sector_t::floor;
					}
					else
					{
						position = sector_t::ceiling;
					}

					tempsec.ceilingplane.ChangeHeight(-1 / 65536.);
					if (cameraLight->FixedLightLevel() < 0 && sub->sector->e->XFloor.lightlist.Size())
					{
						lightlist_t *light = P_GetPlaneLight(sub->sector, &tempsec.ceilingplane, false);
						basecolormap = GetColorTable(light->extra_colormap);
						ceilinglightlevel = *light->p_lightlevel;
					}
					tempsec.ceilingplane.ChangeHeight(1 / 65536.);

					VisiblePlane *ceilingplane3d = Thread->PlaneList->FindPlane(
						tempsec.ceilingplane,
						tempsec.GetTexture(sector_t::ceiling),
						ceilinglightlevel, foggy,
						tempsec.GetAlpha(sector_t::ceiling),
						!!(clip3d->fakeFloor->fakeFloor->flags & FF_ADDITIVETRANS),
						tempsec.planes[position].xform,
						tempsec.sky,
						nullptr,
						basecolormap,
						Fake3DOpaque::FakeCeiling,
						fakeAlpha);

					ceilingplane3d->AddLights(Thread, sub->section->lighthead);

					FakeDrawLoop(sub, &tempsec, nullptr, ceilingplane3d, Fake3DOpaque::FakeCeiling);
				}
			}
			clip3d->fakeFloor = nullptr;
		}
	}

	void RenderOpaquePass::RenderScene(FLevelLocals *Level)
	{
		if (Thread->MainThread)
			WallCycles.Clock();

		for (uint32_t sub : PvsSubsectors)
			SubsectorDepths[sub] = 0xffffffff;
		SubsectorDepths.resize(Level->subsectors.Size(), 0xffffffff);

		PvsSubsectors.clear();
		SeenSpriteSectors.clear();
		SeenActors.clear();

		InSubsector = nullptr;
#ifdef __3DS__
		if (Thread->MainThread) LastMainOpaquePass3DS = this;
		WalkPlaneTicks = WalkSpriteTicks = WalkLineTicks = WalkSubsectors = 0;
		const uint64_t walkStart = svcGetSystemTick();
#endif
		RenderBSPNode(Level->HeadNode());	// The head node is the last node output.
#ifdef __3DS__
		if (Thread->MainThread) TraceSolid3DS = false;
		const uint64_t walkTotal = svcGetSystemTick() - walkStart;
		I_3DSSectionAdd("walk: planes", WalkPlaneTicks);
		I_3DSSectionAdd("walk: sprites, particles", WalkSpriteTicks);
		I_3DSSectionAdd("walk: lines", WalkLineTicks);
		I_3DSSectionAdd("walk: bsp, rest",
			walkTotal - WalkPlaneTicks - WalkSpriteTicks - WalkLineTicks);
		I_3DSSectionAdd("walk: subsectors (x1000 = count)", WalkSubsectors * SYSCLOCK_ARM11 / 1000000);
#endif

		if (Thread->MainThread)
			WallCycles.Unclock();
	}

	//
	// RenderBSPNode
	// Renders all subsectors below a given node, traversing subtree recursively.
	// Just call with BSP root and -1.
	// killough 5/2/98: reformatted, removed tail recursion

#ifdef __3DS__
	// lod3ds_tracesub SUBSECTOR (debug): the next walk prints why it leaves out the subtree that
	// holds it: the box CheckBBox rejected and the solid ranges at that moment. -1 stops.
	static int TraceSubsector3DS = -1;
	CCMD(lod3ds_tracesub)
	{
		TraceSubsector3DS = argv.argc() > 1 ? atoi(argv[1]) : -1;
	}

	static bool SubtreeHolds3DS(void *node, int wanted)
	{
		if ((size_t)node & 1)
			return int(((subsector_t *)((uint8_t *)node - 1))->Index()) == wanted;
		node_t *bsp = (node_t *)node;
		return SubtreeHolds3DS(bsp->children[0], wanted) || SubtreeHolds3DS(bsp->children[1], wanted);
	}
#endif

	void RenderOpaquePass::RenderBSPNode(void *node)
	{
		if (Thread->Viewport->Level()->nodes.Size() == 0)
		{
			RenderSubsector(&Thread->Viewport->Level()->subsectors[0]);
			return;
		}
		while (!((size_t)node & 1))  // Keep going until found a subsector
		{
			node_t *bsp = (node_t *)node;

			// Decide which side the view point is on.
			int side = R_PointOnSide(Thread->Viewport->viewpoint.Pos, bsp);

			// Recursively divide front space (toward the viewer). A distance-only
			// subtree rejection is not safe here: a child that looks remote in map
			// space can still own the visplane or occluder covering near pixels.
			RenderBSPNode(bsp->children[side]);

			// Possibly divide back space (away from the viewer).
			side ^= 1;
			if (!CheckBBox(bsp->bbox[side]))
			{
#ifdef __3DS__
				if (TraceSubsector3DS >= 0 && Thread->MainThread &&
					SubtreeHolds3DS(bsp->children[side], TraceSubsector3DS))
				{
					char solid[512];
					Thread->ClipSegments->Describe3DS(solid, sizeof(solid));
					const float *box = bsp->bbox[side];
					Printf("tracesub %d: box l%.0f r%.0f b%.0f t%.0f rejected; viewwidth %d; solid %s\n",
						TraceSubsector3DS, box[BOXLEFT], box[BOXRIGHT], box[BOXBOTTOM], box[BOXTOP],
						viewwidth, solid);
					TraceSubsector3DS = -1;
				}
#endif
				return;
			}

			node = bsp->children[side];
		}
		RenderSubsector((subsector_t *)((uint8_t *)node - 1));
	}

	void RenderOpaquePass::ClearClip()
	{
		fillshort(floorclip, viewwidth, viewheight);
		fillshort(ceilingclip, viewwidth, 0);
	}

#ifdef __3DS__
	static size_t SeenSlot(const void *key, size_t mask)
	{
		uintptr_t h = reinterpret_cast<uintptr_t>(key) >> 3;
		h ^= h >> 15;
		h *= 0x2c1b3c6dU;
		h ^= h >> 12;
		return h & mask;
	}

	bool RenderOpaquePass::FSeenSet::Insert(const void *key)
	{
		if ((Count + 1) * 2 > Keys.size()) Grow();
		const size_t mask = Keys.size() - 1;
		for (size_t slot = SeenSlot(key, mask);; slot = (slot + 1) & mask)
		{
			if (Generations[slot] != Generation)
			{
				Keys[slot] = key;
				Generations[slot] = Generation;
				++Count;
				return true;
			}
			if (Keys[slot] == key) return false;
		}
	}

	bool RenderOpaquePass::FSeenSet::Contains(const void *key) const
	{
		if (Keys.empty()) return false;
		const size_t mask = Keys.size() - 1;
		for (size_t slot = SeenSlot(key, mask);; slot = (slot + 1) & mask)
		{
			if (Generations[slot] != Generation) return false;
			if (Keys[slot] == key) return true;
		}
	}

	void RenderOpaquePass::FSeenSet::clear()
	{
		Count = 0;
		if (++Generation == 0)
		{
			std::fill(Generations.begin(), Generations.end(), 0);
			Generation = 1;
		}
	}

	void RenderOpaquePass::FSeenSet::Grow()
	{
		std::vector<const void *> oldKeys;
		std::vector<uint32_t> oldGenerations;
		oldKeys.swap(Keys);
		oldGenerations.swap(Generations);
		const size_t size = oldKeys.empty() ? 256 : oldKeys.size() * 2;
		Keys.assign(size, nullptr);
		Generations.assign(size, 0);
		Count = 0;
		for (size_t i = 0; i < oldKeys.size(); ++i)
			if (oldGenerations[i] == Generation) Insert(oldKeys[i]);
	}
#endif

	void RenderOpaquePass::AddSprites(sector_t *sec, int lightlevel, WaterFakeSide fakeside, bool foggy, FDynamicColormap *basecolormap)
	{
		// BSP is traversed by subsector.
		// A sector might have been split into several
		//	subsectors during BSP building.
		// Thus we check whether it was already added.
#ifdef __3DS__
		if (sec->touching_renderthings == nullptr || !SeenSpriteSectors.Insert(sec))
			return;
#else
		if (sec->touching_renderthings == nullptr || SeenSpriteSectors.find(sec) != SeenSpriteSectors.end()/*|| sec->validcount == validcount*/)
			return;

		// Well, now it will be done.
		//sec->validcount = validcount;
		SeenSpriteSectors.insert(sec);
#endif

		// Handle all things in sector.
		for (auto p = sec->touching_renderthings; p != nullptr; p = p->m_snext)
		{
			auto thing = p->m_thing;
#ifdef __3DS__
			if (!SeenActors.Insert(thing)) continue;
			// Two cheap rejections first, before the sprite's frame is worked out: past MAP01's
			// fog (IsPotentiallyVisible's own test, which comes after its other checks), and well
			// behind the camera, where not even the edge of a sprite can show.
			{
				const DVector3 &eye = Thread->Viewport->viewpoint.Pos;
				const double dx = thing->X() - eye.X, dy = thing->Y() - eye.Y;
				if (Map01RootDistanceCullActive(Thread) &&
					Map01ExteriorDistanceCullCandidate(thing->Sector) &&
					dx * dx + dy * dy + (thing->Z() - eye.Z) * (thing->Z() - eye.Z) > Map01CullDistanceSquared())
				{
					I_3DSRecordDrawDistanceSpriteCull();
					continue;
				}
				const double forward = dx * Thread->Viewport->viewpoint.Cos + dy * Thread->Viewport->viewpoint.Sin;
				if (forward < -256.0) continue;
			}
#else
			if (SeenActors.find(thing) != SeenActors.end()) continue;
			SeenActors.insert(thing);
#endif
			//if (thing->validcount == validcount) continue;
			//thing->validcount = validcount;

			FIntCVar *cvar = thing->GetInfo()->distancecheck;
			if (cvar != nullptr && *cvar >= 0)
			{
				double dist = (thing->Pos() - Thread->Viewport->viewpoint.Pos).LengthSquared();
				double check = (double)**cvar;
				if (dist >= check * check)
				{
					continue;
				}
			}

			// find fake level
			F3DFloor *fakeceiling = nullptr;
			F3DFloor *fakefloor = nullptr;
			for (auto rover : thing->Sector->e->XFloor.ffloors)
			{
				if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_RENDERPLANES)) continue;
				if (!(rover->flags & FF_SOLID) || rover->alpha != 255) continue;
				if (!fakefloor)
				{
					if (!rover->top.plane->isSlope())
					{
						if (rover->top.plane->ZatPoint(0., 0.) <= thing->Z()) fakefloor = rover;
					}
				}
				if (!rover->bottom.plane->isSlope())
				{
					if (rover->bottom.plane->ZatPoint(0., 0.) >= thing->Top()) fakeceiling = rover;
				}
			}

			if (IsPotentiallyVisible(thing))
			{
				ThingSprite sprite;
				int spritenum = thing->sprite;
				bool isPicnumOverride = thing->picnum.isValid();
				FSpriteModelFrame *modelframe = isPicnumOverride ? nullptr : FindModelFrame(thing->GetClass(), spritenum, thing->frame, !!(thing->flags & MF_DROPPED));
				if (r_modelscene && modelframe && (thing->Pos() - Thread->Viewport->viewpoint.Pos).LengthSquared() < model_distance_cull)
				{
					DVector3 pos = thing->InterpolatedPosition(Thread->Viewport->viewpoint.TicFrac);
					RenderModel::Project(Thread, (float)pos.X, (float)pos.Y, (float)pos.Z, modelframe, thing);
				}
				else if (GetThingSprite(thing, sprite))
				{
					FDynamicColormap *thingColormap = basecolormap;
					int thinglightlevel = lightlevel;
					if (sec->sectornum != thing->Sector->sectornum)	// compare sectornums to account for R_FakeFlat copies.
					{
						thinglightlevel = thing->Sector->GetTexture(sector_t::ceiling) == skyflatnum ? thing->Sector->GetCeilingLight() : thing->Sector->GetFloorLight();
						auto nc = !!(thing->Level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING);
						thingColormap = GetSpriteColorTable(thing->Sector->Colormap, thing->Sector->SpecialColors[sector_t::sprites], nc);					
						#ifdef __3DS__
						thingColormap = Apply3DSMap01DistanceFog(thing->Sector,
							thingColormap);
						#endif
					}

					if ((sprite.renderflags & RF_SPRITETYPEMASK) == RF_WALLSPRITE)
					{
						RenderWallSprite::Project(Thread, thing, sprite.pos, sprite.tex, sprite.spriteScale, sprite.renderflags, thinglightlevel, foggy, thingColormap);
					}
					else if (sprite.voxel)
					{
						RenderVoxel::Project(Thread, thing, sprite.pos, sprite.voxel, sprite.spriteScale, sprite.renderflags, fakeside, fakefloor, fakeceiling, sec, thinglightlevel, foggy, thingColormap);
					}
					else
					{
						RenderSprite::Project(Thread, thing, sprite.pos, sprite.tex, sprite.spriteScale, sprite.renderflags, fakeside, fakefloor, fakeceiling, sec, thinglightlevel, foggy, thingColormap);

						// [Nash] draw sprite shadow
						if (R_ShouldDrawSpriteShadow(thing))
						{
							double dist = (thing->Pos() - Thread->Viewport->viewpoint.Pos).LengthSquared();
							double distCheck = r_actorspriteshadowdist;
							if (dist <= distCheck * distCheck)
							{
								// squash Y scale
								DVector2 shadowScale = sprite.spriteScale;
								shadowScale.Y *= 0.15;

								// snap to floor Z
								DVector3 shadowPos = sprite.pos;
								shadowPos.Z = thing->floorz;

								RenderSprite::Project(Thread, thing, shadowPos, sprite.tex, shadowScale, sprite.renderflags, fakeside, fakefloor, fakeceiling, sec, thinglightlevel, foggy, thingColormap, true);
							}
						}
					}
				}
			}
		}
	}

	bool RenderOpaquePass::IsPotentiallyVisible(AActor *thing)
	{
		// Don't waste time projecting sprites that are definitely not visible.
		if (thing == nullptr ||
			(thing->renderflags & RF_INVISIBLE) ||
			!thing->RenderStyle.IsVisible(thing->Alpha) ||
			!thing->IsVisibleToPlayer() ||
			!thing->IsInsideVisibleAngles())
		{
			return false;
		}

		// check renderrequired vs ~r_rendercaps, if anything matches we don't support that feature,
		// check renderhidden vs r_rendercaps, if anything matches we do support that feature and should hide it.
		if ((!r_debug_disable_vis_filter && !!(thing->RenderRequired & ~r_renderercaps)) ||
			(!!(thing->RenderHidden & r_renderercaps)))
			return false;

		// [ZZ] Or less definitely not visible (hue)
		// [ZZ] 10.01.2016: don't try to clip stuff inside a skybox against the current portal.
		RenderPortal *renderportal = Thread->Portal.get();
		if (!renderportal->CurrentPortalInSkybox && renderportal->CurrentPortal && !!P_PointOnLineSidePrecise(thing->Pos(), renderportal->CurrentPortal->dst))
			return false;

		double distanceSquared = (thing->Pos() - Thread->Viewport->viewpoint.Pos).LengthSquared();
		if (Map01RootDistanceCullActive(Thread) &&
			Map01ExteriorDistanceCullCandidate(thing->Sector) &&
			distanceSquared > Map01CullDistanceSquared())
		{
			#ifdef __3DS__
			I_3DSRecordDrawDistanceSpriteCull();
			#endif
			return false;
		}

		return true;
	}

	bool RenderOpaquePass::GetThingSprite(AActor *thing, ThingSprite &sprite)
	{
		// The X offsetting (SpriteOffset.X) is performed in r_sprite.cpp, in RenderSprite::Project().
		sprite.pos = thing->InterpolatedPosition(Thread->Viewport->viewpoint.TicFrac);
		sprite.pos.Z += thing->GetBobOffset(Thread->Viewport->viewpoint.TicFrac) - thing->SpriteOffset.Y;
		sprite.spritenum = thing->sprite;
		sprite.tex = nullptr;
		sprite.voxel = nullptr;
		sprite.spriteScale = thing->Scale;
		sprite.renderflags = thing->renderflags;

		if (thing->player != nullptr)
		{
			P_CheckPlayerSprite(thing, sprite.spritenum, sprite.spriteScale);
		}

		if (thing->picnum.isValid())
		{
			sprite.picnum = thing->picnum;

			sprite.tex = GetPalettedSWTexture(sprite.picnum, true);
			if (!sprite.tex) return false;

			if (sprite.tex->GetRotations() != 0xFFFF)
			{
				// choose a different rotation based on player view
				spriteframe_t *sprframe = &SpriteFrames[sprite.tex->GetRotations()];
				DAngle ang = (sprite.pos - Thread->Viewport->viewpoint.Pos).Angle();
				angle_t rot;
				if (sprframe->Texture[0] == sprframe->Texture[1])
				{
					if (thing->flags7 & MF7_SPRITEANGLE)
						rot = (thing->SpriteAngle + 45.0 / 2 * 9).BAMs() >> 28;
					else
						rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + 45.0 / 2 * 9).BAMs() >> 28;
				}
				else
				{
					if (thing->flags7 & MF7_SPRITEANGLE)
						rot = (thing->SpriteAngle + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
					else
						rot = (ang - (thing->Angles.Yaw + thing->SpriteRotation) + (45.0 / 2 * 9 - 180.0 / 16)).BAMs() >> 28;
				}
				sprite.picnum = sprframe->Texture[rot];
				if (sprframe->Flip & (1 << rot))
				{
					sprite.renderflags ^= RF_XFLIP;
				}
				sprite.tex = GetPalettedSWTexture(sprite.picnum, false);	// Do not animate the rotation
				if (!sprite.tex) return false;
			}
		}
		else
		{
			// decide which texture to use for the sprite
			if ((unsigned)sprite.spritenum >= sprites.Size())
			{
				DPrintf(DMSG_ERROR, "R_ProjectSprite: invalid sprite number %u\n", sprite.spritenum);
				return false;
			}
			spritedef_t *sprdef = &sprites[sprite.spritenum];
			if (thing->frame >= sprdef->numframes)
			{
				// If there are no frames at all for this sprite, don't draw it.
				return false;
			}
			else
			{
				auto &viewpoint = Thread->Viewport->viewpoint;
				DAngle sprangle = thing->GetSpriteAngle((sprite.pos - viewpoint.Pos).Angle(), viewpoint.TicFrac);
				bool flipX;

				FTextureID tex = sprdef->GetSpriteFrame(thing->frame, -1, sprangle, &flipX, !!(thing->renderflags & RF_SPRITEFLIP));
				if (tex.isValid())
				{
					if (flipX)
					{
						sprite.renderflags ^= RF_XFLIP;
					}
					sprite.tex = GetPalettedSWTexture(tex, false);	// Do not animate the rotation
				}

				if (r_drawvoxels)
				{
					sprite.voxel = SpriteFrames[sprdef->spriteframes + thing->frame].Voxel;
				}

				if (sprite.voxel == nullptr && !tex.isValid())
					return false;
			}

			if (sprite.voxel == nullptr && sprite.tex == nullptr)
			{
				return false;
			}

			if (sprite.spriteScale.Y < 0)
			{
				sprite.spriteScale.Y = -sprite.spriteScale.Y;
				sprite.renderflags ^= RF_YFLIP;
			}
			if (sprite.spriteScale.X < 0)
			{
				sprite.spriteScale.X = -sprite.spriteScale.X;
				sprite.renderflags ^= RF_XFLIP;
			}
		}

		return true;
	}
}
