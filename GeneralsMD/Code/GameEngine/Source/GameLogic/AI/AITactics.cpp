/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// AITactics.cpp
// Unit level tactics of the Expert computer player, on top of the strategic layer (AIStrategy.cpp):
// split fire, kiting, spreading out against area weapons.  Like the rest of the Expert AI this only reads
// synchronised game state, never uses the wall clock, and keeps its memory in plain arrays.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"


//-------------------------------------------------------------------------------------------------
// split fire
//
// A unit that picks a target tells the ledger how much damage it is about to deal to it.  Units that
// pick a target a moment later see what is already on its way and take the next target once the
// first ones are enough to kill it.  An entry expires after the split window of the skill settings
// (the time the shots take to land), so a target that survives becomes a candidate again.
//-------------------------------------------------------------------------------------------------
Real AIStrategy::assignedDamage( ObjectID target ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = 0; i < LEDGER_SIZE; ++i)
	{
		const AILedgerEntry &e = m_ledger[i];
		if (e.m_target == target && e.m_expire > now)
			return e.m_damage;
	}
	return 0.0f;
}

void AIStrategy::assignDamage( ObjectID target, Real damage, Bool switched )
{
	++m_splitPicks;
	if (switched)
		++m_splitSwitches;
	const UnsignedInt now = TheGameLogic->getFrame();
	const UnsignedInt expire = now + secondsToFrames(skill().m_splitWindowSeconds);

	Int free = -1;
	for (Int i = 0; i < LEDGER_SIZE; ++i)
	{
		AILedgerEntry &e = m_ledger[i];
		if (e.m_target == target && e.m_expire > now)
		{
			e.m_damage += damage;
			e.m_expire = expire;
			return;
		}
		// The first free (expired) slot, else the entry that expires first.
		if (e.m_expire <= now)
		{
			if (free < 0 || m_ledger[free].m_expire > now)
				free = i;
		}
		else if (free < 0 || (m_ledger[free].m_expire > now && e.m_expire < m_ledger[free].m_expire))
		{
			free = i;
		}
	}
	AILedgerEntry &e = m_ledger[free];
	e.m_target = target;
	e.m_damage = damage;
	e.m_expire = expire;
}
