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

// PlayerAssist.cpp
// The core of the player assists: the match setting, the state kept per unit, the message entry point and the
// per frame update.  The assists themselves are in the other PlayerAssist*.cpp / Assist*.cpp files.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "Common/Recorder.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

PlayerAssist *ThePlayerAssist = nullptr;

//-------------------------------------------------------------------------------------------------
PlayerAssist::PlayerAssist()
{
	m_allowed = FALSE;
	m_aimValid = FALSE;
	m_aimAngle = 0.0f;
	m_aimWidth = 0.0f;
	m_pruneFrame = 0;
}

//-------------------------------------------------------------------------------------------------
PlayerAssist::~PlayerAssist()
{
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::init()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::reset()
{
	m_allowed = FALSE;
	m_units.clear();
	m_protect.reset();
	m_aimValid = FALSE;
	m_pruneFrame = 0;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::startMatch( Bool allowed )
{
	reset();
	m_allowed = allowed;
	ASSIST_DEBUG(( "ASSIST match allowed=%d replay=%d", allowed ? 1 : 0, TheRecorder && TheRecorder->isPlaybackMode() ? 1 : 0 ));
}

//-------------------------------------------------------------------------------------------------
/// Forget units that are gone.  Cheap, and only every few seconds.
void PlayerAssist::pruneDead()
{
	for (UnitMap::iterator it = m_units.begin(); it != m_units.end(); )
	{
		Object *obj = TheGameLogic->findObjectByID( it->first );
		if (obj == nullptr || obj->isEffectivelyDead())
			m_units.erase( it++ );
		else
			++it;
	}
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::logicUpdate()
{
	runTestSpec();

	// the tests follow the logic frames from this line (starter_flow.mjs, lastFrame)
	if (TheAssistOptions.m_debug && TheGameLogic->getFrame() % 100 == 0)
		printf( "ASSIST frame %u\n", TheGameLogic->getFrame() );

	if (!m_allowed)
		return;

	const UnsignedInt now = TheGameLogic->getFrame();
	m_protect.update( now );
	if (now >= m_pruneFrame)
	{
		m_pruneFrame = now + 2 * LOGICFRAMES_PER_SECOND;
		pruneDead();
	}
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::onMessage( GameMessage *msg, Player *player, AIGroup *group )
{
	if (!m_allowed || player == nullptr)
		return FALSE;

	switch (msg->getType())
	{
		case GameMessage::MSG_ASSIST_FORMATION:
		{
			if (group)
				setFormation( group, msg->getArgument( 0 )->integer );
			return TRUE;
		}

		case GameMessage::MSG_ASSIST_FORMATION_MOVE:
		{
			if (group)
				formationMove( group, msg->getArgument( 0 )->integer, msg->getArgument( 1 )->location,
					msg->getArgument( 2 )->location, msg->getArgument( 3 )->boolean );
			return TRUE;
		}

		case GameMessage::MSG_ASSIST_PROTECT:
		{
			// (hotkey group or -1, number of protectors, protectors ..., protected ...)
			const Int squad = msg->getArgument( 0 )->integer;
			const Int count = msg->getArgument( 1 )->integer;
			const Int total = (Int)msg->getArgumentCount();
			std::vector<ObjectID> protectors, targets;
			for (Int i = 0; i < count && 2 + i < total; ++i)
				protectors.push_back( msg->getArgument( 2 + i )->objectID );
			for (Int i = 2 + count; i < total; ++i)
			{
				const Object *o = TheGameLogic->findObjectByID( msg->getArgument( i )->objectID );
				if (o && !o->isEffectivelyDead() && player->getRelationship( o->getTeam() ) != ENEMIES)
					targets.push_back( o->getID() );
			}
			for (size_t i = 0; i < protectors.size(); ++i)
			{
				Object *o = TheGameLogic->findObjectByID( protectors[i] );
				if (o == nullptr || o->isEffectivelyDead() || o->getControllingPlayer() != player)
					continue;
				if (m_protect.link( o, targets, squad, nullptr ))
					ASSIST_DEBUG(( "ASSIST protect link %d protects %d objects and hotkey group %d", (int)o->getID(), (int)targets.size(), squad ));
			}
			return TRUE;
		}

		case GameMessage::MSG_ASSIST_UNPROTECT:
		{
			if (group)
			{
				const VecObjectID ids = group->getAllIDs();
				for (size_t i = 0; i < ids.size(); ++i)
				{
					if (m_protect.unlink( ids[i] ))
						ASSIST_DEBUG(( "ASSIST protect link %d removed", (int)ids[i] ));
				}
			}
			return TRUE;
		}

		default:
			break;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
/// The player moved these units by hand: protectors among them have their home where they were sent.
void PlayerAssist::noteGroupMove( AIGroup *group, const Coord3D *dest )
{
	if (!m_allowed || group == nullptr || m_protect.count() == 0)
		return;
	Coord3D center;
	if (!group->getCenter( &center ))
		return;
	const VecObjectID ids = group->getAllIDs();
	for (size_t i = 0; i < ids.size(); ++i)
	{
		if (!m_protect.isProtector( ids[i] ))
			continue;
		const Object *o = TheGameLogic->findObjectByID( ids[i] );
		if (o == nullptr)
			continue;
		// every unit keeps its place in the group, up to six times its size (as the game's own group move does)
		Real ox = o->getPosition()->x - center.x;
		Real oy = o->getPosition()->y - center.y;
		const Real len = sqrtf( ox * ox + oy * oy );
		const Real limit = 6.0f * o->getGeometryInfo().getBoundingCircleRadius();
		if (len > limit && len > 0.0f)
		{
			ox *= limit / len;
			oy *= limit / len;
		}
		Coord3D home = *dest;
		home.x += ox;
		home.y += oy;
		m_protect.noteManualMove( ids[i], home );
	}
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::crc( Xfer *x )
{
	// the same data as the save: whatever the player assists remember is part of the simulation
	this->xfer( x );
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	const Bool loading = xfer->getXferMode() == XFER_LOAD;
	xfer->xferBool( &m_allowed );
	if (version >= 2)
		m_protect.xfer( xfer );

	UnsignedInt count = (UnsignedInt)m_units.size();
	xfer->xferUnsignedInt( &count );

	if (loading)
	{
		m_units.clear();
		for (UnsignedInt i = 0; i < count; ++i)
		{
			ObjectID id = INVALID_ID;
			UnitState st;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedByte( &st.m_formation );
			m_units[id] = st;
		}
	}
	else
	{
		for (UnitMap::iterator it = m_units.begin(); it != m_units.end(); ++it)
		{
			ObjectID id = it->first;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedByte( &it->second.m_formation );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::loadPostProcess()
{
}
