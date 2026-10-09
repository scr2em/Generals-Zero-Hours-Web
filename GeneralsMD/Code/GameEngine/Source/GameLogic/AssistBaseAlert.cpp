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

// AssistBaseAlert.cpp
// The simulation side of the "base under attack" response.  The player clicks the button the alert offers (or presses its
// hotkey): the army units that stand idle near the attacked place attack-move there, and the response remembers where each
// of them stood.  A second click sends the ones that are still around back to those places.
//
// An army unit is a unit that can shoot (from the combat figures of its template: no names), is not a worker, harvester or
// support unit and is not a structure (PlayerAssist::isArmyUnit, AssistIdle.cpp; the idle hotkeys use it as well).  "Near" is a radius around the attack; "idle" is the AI's idle state, so units that
// the player is busy with are left alone.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

namespace
{
	const Real NEARBY_RADIUS = 1600.0f;			// army units further from the attack than this are not sent
	const Real BACK_RADIUS = 700.0f;				// at the return, units that are still this near the attack (or idle) are sent back

	Real dist2D( const Coord3D &a, const Coord3D &b )
	{
		return sqrtf( (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) );
	}

	struct Found
	{
		Coord3D					m_where;
		std::vector<Object *>	m_units;
	};

	void findIdle( Object *obj, void *userData )
	{
		Found *found = (Found *)userData;
		if (!PlayerAssist::isArmyUnit( obj ) || !obj->getAIUpdateInterface()->isIdle())
			return;
		if (dist2D( *obj->getPosition(), found->m_where ) > NEARBY_RADIUS)
			return;
		found->m_units.push_back( obj );
	}

	Bool idLess( const Object *a, const Object *b )
	{
		return a->getID() < b->getID();
	}
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::defendActive( Int playerIndex ) const
{
	return playerIndex >= 0 && playerIndex < MAX_DEFEND && m_defend[playerIndex].m_active;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::baseDefend( Player *player, const Coord3D &where )
{
	const Int index = player->getPlayerIndex();
	if (index < 0 || index >= MAX_DEFEND)
		return;
	DefendState &state = m_defend[index];

	Found found;
	found.m_where = where;
	player->iterateObjects( findIdle, &found );
	std::sort( found.m_units.begin(), found.m_units.end(), idLess );

	if (found.m_units.empty())
	{
		ASSIST_DEBUG(( "ASSIST base defend: no idle army units near %.0f,%.0f", where.x, where.y ));
		return;
	}

	state.m_active = TRUE;
	state.m_location = where;
	state.m_frame = TheGameLogic->getFrame();

	// the units spread over a small disc around the place, in a fixed pattern (the golden angle)
	for (size_t i = 0; i < found.m_units.size(); ++i)
	{
		Object *unit = found.m_units[i];
		const Real angle = 2.39996323f * (Real)i;
		const Real radius = 25.0f + 14.0f * sqrtf( (Real)i );
		Coord3D dest;
		dest.x = where.x + radius * cosf( angle );
		dest.y = where.y + radius * sinf( angle );
		dest.z = TheTerrainLogic->getGroundHeight( dest.x, dest.y );

		// a unit that is already on the list keeps its first place to return to
		Bool known = FALSE;
		for (size_t k = 0; k < state.m_units.size(); ++k)
			known = known || state.m_units[k] == unit->getID();
		if (!known)
		{
			state.m_units.push_back( unit->getID() );
			state.m_origins.push_back( *unit->getPosition() );
		}
		unit->getAIUpdateInterface()->aiAttackMoveToPosition( &dest, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
	}
	ASSIST_DEBUG(( "ASSIST base defend: %d units sent to %.0f,%.0f", (int)found.m_units.size(), where.x, where.y ));
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::baseReturn( Player *player )
{
	const Int index = player->getPlayerIndex();
	if (index < 0 || index >= MAX_DEFEND || !m_defend[index].m_active)
		return;
	DefendState &state = m_defend[index];

	Int sent = 0;
	for (size_t i = 0; i < state.m_units.size(); ++i)
	{
		Object *unit = TheGameLogic->findObjectByID( state.m_units[i] );
		if (unit == nullptr || unit->isEffectivelyDead() || unit->isContained() || unit->getAIUpdateInterface() == nullptr)
			continue;
		if (unit->getControllingPlayer() != player)
			continue;
		// units that went elsewhere on the player's orders stay there
		if (!unit->getAIUpdateInterface()->isIdle() && dist2D( *unit->getPosition(), state.m_location ) > BACK_RADIUS)
			continue;
		Coord3D origin = state.m_origins[i];
		unit->getAIUpdateInterface()->aiMoveToPosition( &origin, CMD_FROM_PLAYER );
		++sent;
	}
	ASSIST_DEBUG(( "ASSIST base defend: %d units sent back", sent ));
	state = DefendState();
}
