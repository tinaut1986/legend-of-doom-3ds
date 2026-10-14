//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
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
//		Ticker.
//
//-----------------------------------------------------------------------------

#include "p_local.h"
#include "p_effect.h"
#include "c_console.h"
#include "b_bot.h"
#include "doomstat.h"
#include "sbar.h"
#include "r_data/r_interpolate.h"
#include "d_player.h"
#include "r_utility.h"
#include "p_spec.h"
#ifdef __3DS__
#include <3ds.h>
#include <algorithm>
#include <cstdlib>
#include "c_dispatch.h"
#endif
#include "g_levellocals.h"
#include "events.h"
#include "actorinlines.h"
#include "g_game.h"
#ifdef __3DS__
#include "am_map.h"
#endif

extern gamestate_t wipegamestate;
extern uint8_t globalfreeze, globalchangefreeze;

//==========================================================================
//
// P_CheckTickerPaused
//
// Returns true if the ticker should be paused. In that case, it also
// pauses sound effects and possibly music. If the ticker should not be
// paused, then it returns false but does not unpause anything.
//
//==========================================================================

bool P_CheckTickerPaused ()
{
	// pause if in menu or console and at least one tic has been run
	if ( !netgame
		 && gamestate != GS_TITLELEVEL
		 && ((menuactive != MENU_Off && menuactive != MENU_OnNoPause) ||
			 ConsoleState == c_down || ConsoleState == c_falling
			#ifdef __3DS__
			|| automapactive
			#endif
			)
		 && !demoplayback
		 && !demorecording
		 && players[consoleplayer].viewz != NO_VALUE
		 && wipegamestate == gamestate)
	{
		// Only the current UI level's settings are relevant for sound.
		S_PauseSound (!(primaryLevel->flags2 & LEVEL2_PAUSE_MUSIC_IN_MENUS), false);
		return true;
	}
	return false;
}

//
// P_Ticker
//
#ifdef __3DS__
// lod3ds_tickprofile N (P2.4): the average time of each phase of P_Ticker over the next N tics,
// printed to the console and its log.
namespace
{
enum ETickPhase { TP_Interpolations, TP_ClearInterpolation, TP_Particles, TP_Players,
	TP_WorldTick, TP_LevelTick, TP_Thinkers, TP_Specials, TP_StatusBar, TP_Count };
const char *const TickPhaseNames[TP_Count] = { "interpolations", "clear interpolation",
	"particles", "players", "WorldTick", "level tick", "thinkers", "specials", "status bar" };
uint64_t TickPhaseTicks[TP_Count];
int TickProfileLeft, TickProfileTotal;
// Thinkers ticked and skipped (P_3DSStaticThinker) while profiling, the ticked ones by class.
unsigned TickProfileSkipped, TickProfileTicked;
TMap<FName, unsigned> TickProfileClasses;
uint64_t TickPhaseStart;
inline void TickPhaseBegin() { if (TickProfileLeft > 0) TickPhaseStart = svcGetSystemTick(); }
inline void TickPhaseEnd(ETickPhase phase)
{
	if (TickProfileLeft > 0) TickPhaseTicks[phase] += svcGetSystemTick() - TickPhaseStart;
}
void TickProfileTicDone()
{
	if (TickProfileLeft <= 0 || --TickProfileLeft > 0) return;
	Printf("Tick profile, %d tics, ms a tic:\n", TickProfileTotal);
	for (int phase = 0; phase < TP_Count; ++phase)
		Printf("  %-20s %.3f\n", TickPhaseNames[phase],
			TickPhaseTicks[phase] * 1000.0 / SYSCLOCK_ARM11 / TickProfileTotal);
	Printf("  thinkers a tic: %u ticked, %u skipped as static; ticked by class:\n",
		TickProfileTicked / TickProfileTotal, TickProfileSkipped / TickProfileTotal);
	TMap<FName, unsigned>::Iterator it(TickProfileClasses);
	TMap<FName, unsigned>::Pair *pair;
	while (it.NextPair(pair))
		if (pair->Value >= unsigned(TickProfileTotal))
			Printf("    %5u %s\n", pair->Value / TickProfileTotal, pair->Key.GetChars());
}
}

void P_3DSTickProfileThinker(DThinker *thinker, bool skipped)
{
	if (TickProfileLeft <= 0) return;
	if (skipped)
	{
		++TickProfileSkipped;
		return;
	}
	++TickProfileTicked;
	const FName name = thinker->GetClass()->TypeName;
	if (unsigned *count = TickProfileClasses.CheckKey(name)) ++*count;
	else TickProfileClasses.Insert(name, 1);
}

CCMD(lod3ds_tickprofile)
{
	TickProfileTotal = argv.argc() > 1 ? std::max(1, atoi(argv[1])) : 70;
	TickProfileLeft = TickProfileTotal;
	for (auto &ticks : TickPhaseTicks) ticks = 0;
	TickProfileSkipped = TickProfileTicked = 0;
	TickProfileClasses.Clear();
}
#define TICK_PHASE_BEGIN() TickPhaseBegin()
#define TICK_PHASE_END(phase) TickPhaseEnd(phase)
#else
#define TICK_PHASE_BEGIN()
#define TICK_PHASE_END(phase)
#endif

void P_Ticker (void)
{
	int i;

	TICK_PHASE_BEGIN();
	for (auto Level : AllLevels())
	{
		Level->interpolator.UpdateInterpolations();
	}
	TICK_PHASE_END(TP_Interpolations);
	r_NoInterpolate = true;

	if (!demoplayback)
	{
		// This is a separate slot from the wipe in D_Display(), because this
		// is delayed slightly due to latency. (Even on a singleplayer game!)
//		GSnd->SetSfxPaused(!!playerswiping, 2);
	}

	// run the tic
	if (paused || P_CheckTickerPaused())
	{
		// This must run even when the game is paused to catch changes from netevents before the frame is rendered.
		for (auto Level : AllLevels())
		{
			auto it = Level->GetThinkerIterator<AActor>();
			AActor* ac;

			while ((ac = it.Next()))
			{
				if (ac->flags8 & MF8_RECREATELIGHTS)
				{
					ac->flags8 &= ~MF8_RECREATELIGHTS;
					ac->SetDynamicLights();
				}
			}
		}
		return;
	}

	DPSprite::NewTick();

	// [RH] Frozen mode is only changed every 4 tics, to make it work with A_Tracer().
	// This may not be perfect but it is not really relevant for sublevels that tracer homing behavior is preserved.
	if ((primaryLevel->maptime & 3) == 0)
	{
		if (globalchangefreeze)
		{
			globalfreeze ^= 1;
			globalchangefreeze = 0;
			for (auto Level : AllLevels())
			{
				Level->frozenstate = (Level->frozenstate & ~2) | (2 * globalfreeze);
			}
		}
	}

	// [BC] Do a quick check to see if anyone has the freeze time power. If they do,
	// then don't resume the sound, since one of the effects of that power is to shut
	// off the music.
	for (i = 0; i < MAXPLAYERS; i++ )
	{
		if (playeringame[i] && players[i].timefreezer != 0)
			break;
	}

	if ( i == MAXPLAYERS )
		S_ResumeSound (false);

	P_ResetSightCounters (false);
	R_ClearInterpolationPath();

	// Since things will be moving, it's okay to interpolate them in the renderer.
	r_NoInterpolate = false;

	// Reset all actor interpolations on all levels before the current thinking turn so that indirect actor movement gets properly interpolated.
	for (auto Level : AllLevels())
	{
		// todo: set up a sandbox for secondary levels here.
		TICK_PHASE_BEGIN();
		auto it = Level->GetThinkerIterator<AActor>();
		AActor *ac;

		while ((ac = it.Next()))
		{
			ac->ClearInterpolation();
		}

		TICK_PHASE_END(TP_ClearInterpolation);
		TICK_PHASE_BEGIN();
		P_ThinkParticles(Level);	// [RH] make the particles think
		TICK_PHASE_END(TP_Particles);
		TICK_PHASE_BEGIN();

		for (i = 0; i < MAXPLAYERS; i++)
			if (Level->PlayerInGame(i))
				P_PlayerThink(Level->Players[i]);
		TICK_PHASE_END(TP_Players);
		TICK_PHASE_BEGIN();

		// [ZZ] call the WorldTick hook
		Level->localEventManager->WorldTick();
		TICK_PHASE_END(TP_WorldTick);
		TICK_PHASE_BEGIN();
		Level->Tick();			// [RH] let the level tick
		TICK_PHASE_END(TP_LevelTick);
		TICK_PHASE_BEGIN();
		Level->Thinkers.RunThinkers(Level);
		TICK_PHASE_END(TP_Thinkers);
		TICK_PHASE_BEGIN();

		//if added by MC: Freeze mode.
		if (!Level->isFrozen())
		{
			P_UpdateSpecials(Level);
		}
		TICK_PHASE_END(TP_Specials);

		// for par times
		Level->time++;
		Level->maptime++;
		Level->totaltime++;
	}
	if (players[consoleplayer].mo != NULL) {
		if (players[consoleplayer].mo->Vel.Length() > primaryLevel->max_velocity) { primaryLevel->max_velocity = players[consoleplayer].mo->Vel.Length(); }
		primaryLevel->avg_velocity += (players[consoleplayer].mo->Vel.Length() - primaryLevel->avg_velocity) / primaryLevel->maptime;
	}
	TICK_PHASE_BEGIN();
	StatusBar->CallTick();		// Status bar should tick AFTER the thinkers to properly reflect the level's state at this time.
	TICK_PHASE_END(TP_StatusBar);
#ifdef __3DS__
	TickProfileTicDone();
#endif
}
