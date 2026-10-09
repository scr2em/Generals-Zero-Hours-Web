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

// AssistTest.cpp
// Test aid for the automated tests of the player assists: with "-assistTest spec" on the command line a match
// starts with extra units, so that a test does not have to play for minutes to get an army.  Without the
// argument nothing here runs.
//
//   spec = entry[+entry...]      entry = player:template:count[:ref:dx:dy[:frame]]
//
// "player" is the slot index of the owner (0: the first, human player), the units appear around the command center of player "ref" (default the
// owner), shifted by dx, dy world units (default -150, -130: below the base on the screen), in rows.  They appear in logic
// frame "frame" (default 3), so that a test can set things up before a fight starts.  Every client of a network match must
// pass the same argument.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <stdio.h>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/TerrainLogic.h"

//-------------------------------------------------------------------------------------------------
static Bool findBase( Player *player, Coord3D *pos )
{
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getControllingPlayer() == player && obj->isKindOf( KINDOF_COMMANDCENTER ))
		{
			*pos = *obj->getPosition();
			return TRUE;
		}
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::runTestSpec()
{
	if (TheAssistOptions.m_testSpec.isEmpty())
		return;

	if (!(TheGameLogic->isInSkirmishGame() || TheGameLogic->isInMultiplayerGame() || TheGameLogic->isInReplayGame()))
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < 3)
		return;

	char spec[512];
	strlcpy( spec, TheAssistOptions.m_testSpec.str(), sizeof(spec) );
	char *save = nullptr;
	for (char *entry = strtok_r( spec, "+", &save ); entry; entry = strtok_r( nullptr, "+", &save ))
	{
		int owner = 0, count = 0, ref = -1, dx = -150, dy = -130, frame = 3;
		char name[128];
		name[0] = 0;
		const int n = sscanf( entry, "%d:%127[^:]:%d:%d:%d:%d:%d", &owner, name, &count, &ref, &dx, &dy, &frame );
		if (n < 3 || (UnsignedInt)frame != now)
			continue;
		if (ref < 0)
			ref = owner;

		Player *player = ThePlayerList->getPlayerFromSlotIndex( owner );
		Player *refPlayer = ThePlayerList->getPlayerFromSlotIndex( ref );
		const ThingTemplate *tt = TheThingFactory->findTemplate( AsciiString( name ) );
		Coord3D base;
		if (player == nullptr || refPlayer == nullptr || tt == nullptr || !findBase( refPlayer, &base ))
		{
			printf( "ASSISTTEST cannot create %s for player %d\n", name, owner );
			continue;
		}

		Team *team = player->getDefaultTeam();
		const Int perRow = 8;
		for (Int i = 0; i < count; ++i)
		{
			Object *obj = TheThingFactory->newObject( tt, team );
			if (obj == nullptr)
				continue;
			Coord3D pos;
			pos.x = base.x + (Real)dx + (Real)(i % perRow) * 30.0f;
			pos.y = base.y + (Real)dy - (Real)(i / perRow) * 30.0f;
			if (dy > 0)
				pos.y = base.y + (Real)dy + (Real)(i / perRow) * 30.0f;
			pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			obj->setPosition( &pos );
			obj->setOrientation( 0.0f );
		}
		printf( "ASSISTTEST created %d x %s for player %d\n", count, name, owner );
	}
}
