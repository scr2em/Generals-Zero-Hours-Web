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

// Protect.cpp
// See Protect.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/Protect.h"
#include "GameLogic/Squad.h"
#include "GameLogic/Weapon.h"

//-------------------------------------------------------------------------------------------------
const ProtectManager::Params &ProtectManager::params()
{
	static Params p;
	static Bool init = FALSE;
	if (!init)
	{
		init = TRUE;
		p.m_responseRadius = 500.0f;
		p.m_fightRadius = 320.0f;
		p.m_leashDistance = 800.0f;
		p.m_leashFrames = 45 * LOGICFRAMES_PER_SECOND;
		p.m_quietFrames = 5 * LOGICFRAMES_PER_SECOND;
		p.m_homeTolerance = 45.0f;
		p.m_followDistance = 50.0f;
	}
	return p;
}

//-------------------------------------------------------------------------------------------------
ProtectManager::ProtectManager()
{
	m_nextScan = 0;
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::reset()
{
	m_links.clear();
	m_damageSeen.clear();
	m_nextScan = 0;
}

//-------------------------------------------------------------------------------------------------
const ProtectLink *ProtectManager::find( ObjectID protector ) const
{
	LinkMap::const_iterator it = m_links.find( protector );
	return it == m_links.end() ? nullptr : &it->second;
}

//-------------------------------------------------------------------------------------------------
Bool ProtectManager::unlink( ObjectID protector )
{
	return m_links.erase( protector ) > 0;
}

//-------------------------------------------------------------------------------------------------
static Real dist2D( const Coord3D &a, const Coord3D &b )
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return sqrtf( dx * dx + dy * dy );
}

//-------------------------------------------------------------------------------------------------
Bool ProtectManager::link( Object *protector, const std::vector<ObjectID> &targetsIn, Int squad, const Coord3D *home )
{
	if (protector == nullptr || protector->getAI() == nullptr || protector->isKindOf( KINDOF_IMMOBILE ))
		return FALSE;
	Player *owner = protector->getControllingPlayer();
	if (owner == nullptr)
		return FALSE;

	ProtectLink link;
	link.m_protector = protector->getID();
	link.m_player = (Short)owner->getPlayerIndex();
	link.m_squad = (Short)(squad >= 0 && squad < NUM_HOTKEY_SQUADS ? squad : -1);
	for (size_t i = 0; i < targetsIn.size(); ++i)
	{
		if (targetsIn[i] != protector->getID())
			link.m_targets.push_back( targetsIn[i] );
	}
	std::sort( link.m_targets.begin(), link.m_targets.end() );
	link.m_targets.erase( std::unique( link.m_targets.begin(), link.m_targets.end() ), link.m_targets.end() );
	if (link.m_targets.empty() && link.m_squad < 0)
		return FALSE;

	link.m_home = home ? *home : *protector->getPosition();
	link.m_state = PROTECT_AT_HOME;
	link.m_stateFrame = TheGameLogic->getFrame();
	link.m_lastFightFrame = 0;

	std::vector<ObjectID> now;
	resolveTargets( link, now );
	Coord3D centre;
	if (centreOf( now, &centre ))
	{
		link.m_offset.x = link.m_home.x - centre.x;
		link.m_offset.y = link.m_home.y - centre.y;
		link.m_lastCentre = centre;
		link.m_hasOffset = TRUE;
	}
	m_links[link.m_protector] = link;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::resolveTargets( const ProtectLink &link, std::vector<ObjectID> &out ) const
{
	out.clear();
	for (size_t i = 0; i < link.m_targets.size(); ++i)
	{
		const Object *o = TheGameLogic->findObjectByID( link.m_targets[i] );
		if (o && !o->isEffectivelyDead() && o->getID() != link.m_protector)
			out.push_back( o->getID() );
	}
	if (link.m_squad >= 0)
	{
		Player *player = ThePlayerList->getNthPlayer( link.m_player );
		Squad *squad = player ? player->getHotkeySquad( link.m_squad ) : nullptr;
		if (squad)
		{
			const VecObjectPtr &live = squad->getLiveObjects();
			for (size_t i = 0; i < live.size(); ++i)
			{
				if (live[i]->getID() != link.m_protector)
					out.push_back( live[i]->getID() );
			}
		}
	}
	std::sort( out.begin(), out.end() );
	out.erase( std::unique( out.begin(), out.end() ), out.end() );
}

//-------------------------------------------------------------------------------------------------
Bool ProtectManager::centreOf( const std::vector<ObjectID> &targets, Coord3D *centre ) const
{
	Real x = 0.0f, y = 0.0f;
	Int n = 0;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		const Object *o = TheGameLogic->findObjectByID( targets[i] );
		if (o == nullptr)
			continue;
		x += o->getPosition()->x;
		y += o->getPosition()->y;
		++n;
	}
	if (n == 0)
		return FALSE;
	centre->x = x / (Real)n;
	centre->y = y / (Real)n;
	centre->z = 0.0f;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::noteManualMove( ObjectID protector, const Coord3D &dest )
{
	LinkMap::iterator it = m_links.find( protector );
	if (it == m_links.end())
		return;
	ProtectLink &link = it->second;
	link.m_home = dest;
	link.m_state = PROTECT_AT_HOME;
	link.m_stateFrame = TheGameLogic->getFrame();
	std::vector<ObjectID> now;
	resolveTargets( link, now );
	Coord3D centre;
	if (centreOf( now, &centre ))
	{
		link.m_offset.x = dest.x - centre.x;
		link.m_offset.y = dest.y - centre.y;
		link.m_lastCentre = centre;
		link.m_hasOffset = TRUE;
	}
	ASSIST_DEBUG(( "ASSIST protect home of %d moved by hand to %.0f,%.0f", (int)protector, dest.x, dest.y ));
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::goHome( ProtectLink &link, Object *protector, UnsignedInt frame )
{
	AIUpdateInterface *ai = protector->getAIUpdateInterface();
	if (ai == nullptr)
		return;
	ai->aiMoveToPosition( &link.m_home, CMD_FROM_AI );
	link.m_lastOrderFrame = frame;
}

//-------------------------------------------------------------------------------------------------
/// Look at the protected objects for fresh damage from an enemy; the protectors of the object that was hit within reach answer.
void ProtectManager::scanAlarms( UnsignedInt frame )
{
	// the protected objects of all links, once each, in id order
	std::vector<ObjectID> watched;
	for (LinkMap::const_iterator it = m_links.begin(); it != m_links.end(); ++it)
	{
		std::vector<ObjectID> t;
		resolveTargets( it->second, t );
		watched.insert( watched.end(), t.begin(), t.end() );
	}
	std::sort( watched.begin(), watched.end() );
	watched.erase( std::unique( watched.begin(), watched.end() ), watched.end() );

	for (size_t w = 0; w < watched.size(); ++w)
	{
		Object *target = TheGameLogic->findObjectByID( watched[w] );
		if (target == nullptr || target->getBodyModule() == nullptr)
			continue;
		BodyModuleInterface *body = target->getBodyModule();
		const UnsignedInt *stamp = body->getLastDamageTimestamp();
		if (stamp == nullptr || *stamp == 0 || frame - *stamp > 2 * LOGICFRAMES_PER_SECOND)
			continue;
		std::map<ObjectID, UnsignedInt>::iterator seen = m_damageSeen.find( watched[w] );
		if (seen != m_damageSeen.end() && seen->second == *stamp)
			continue;
		m_damageSeen[watched[w]] = *stamp;

		const DamageInfo *info = body->getLastDamageInfo();
		if (info == nullptr || info->out.m_actualDamageDealt <= 0.0f)
			continue;
		const Object *source = TheGameLogic->findObjectByID( info->in.m_sourceID );
		if (source == nullptr || target->getRelationship( source ) != ENEMIES)
			continue;

		// the alarm: where the attacker is, or where the hit object stands
		Coord3D alarm = *source->getPosition();
		if (dist2D( alarm, *target->getPosition() ) > 600.0f)
			alarm = *target->getPosition();

		for (LinkMap::iterator it = m_links.begin(); it != m_links.end(); ++it)
		{
			ProtectLink &link = it->second;
			Object *protector = TheGameLogic->findObjectByID( link.m_protector );
			if (protector == nullptr || protector->isEffectivelyDead() || protector->getAI() == nullptr)
				continue;
			// does this link protect the object that was hit?
			std::vector<ObjectID> t;
			resolveTargets( link, t );
			if (!std::binary_search( t.begin(), t.end(), watched[w] ))
				continue;
			if (dist2D( link.m_home, alarm ) > params().m_responseRadius && dist2D( link.m_home, *target->getPosition() ) > params().m_responseRadius)
				continue;
			// the player's own order has priority
			AIUpdateInterface *ai = protector->getAIUpdateInterface();
			if (ai->getLastCommandSource() == CMD_FROM_PLAYER && !ai->isIdle())
				continue;

			const Coord3D previous = link.m_alert;
			link.m_alert = alarm;
			link.m_lastFightFrame = frame;
			// the same fight again: the order stands, no need to give it every time a shot lands
			if (link.m_state == PROTECT_RESPONDING && dist2D( previous, alarm ) < 200.0f && frame - link.m_lastOrderFrame < 3 * LOGICFRAMES_PER_SECOND)
				continue;
			if (link.m_state != PROTECT_RESPONDING)
			{
				link.m_state = PROTECT_RESPONDING;
				link.m_stateFrame = frame;
			}
			ai->aiAttackMoveToPosition( &alarm, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
			link.m_lastOrderFrame = frame;
			ASSIST_DEBUG(( "ASSIST protect %d answers the alarm for %d at %.0f,%.0f", (int)link.m_protector, (int)watched[w], alarm.x, alarm.y ));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::updateLink( ProtectLink &link, Object *protector, UnsignedInt frame )
{
	AIUpdateInterface *ai = protector->getAIUpdateInterface();
	const Coord3D *pos = protector->getPosition();
	const Params &p = params();

	const Bool playerBusy = ai->getLastCommandSource() == CMD_FROM_PLAYER && !ai->isIdle();
	if (playerBusy)
	{
		// the player's order has priority; the link waits
		if (link.m_state != PROTECT_AT_HOME)
		{
			link.m_state = PROTECT_AT_HOME;
			link.m_stateFrame = frame;
		}
		return;
	}

	// escort: the home follows a protected group that moves
	std::vector<ObjectID> targets;
	resolveTargets( link, targets );
	Coord3D centre;
	if (centreOf( targets, &centre ))
	{
		if (!link.m_hasOffset)
		{
			link.m_offset.x = link.m_home.x - centre.x;
			link.m_offset.y = link.m_home.y - centre.y;
			link.m_lastCentre = centre;
			link.m_hasOffset = TRUE;
		}
		else if (dist2D( centre, link.m_lastCentre ) > p.m_followDistance)
		{
			link.m_home.x = centre.x + link.m_offset.x;
			link.m_home.y = centre.y + link.m_offset.y;
			link.m_home.z = TheTerrainLogic->getGroundHeight( link.m_home.x, link.m_home.y );
			link.m_lastCentre = centre;
			if (link.m_state != PROTECT_RESPONDING)
				goHome( link, protector, frame );
		}
	}

	switch (link.m_state)
	{
		case PROTECT_RESPONDING:
		{
			// enemies near the protector keep the fight going
			Object *enemy = TheAI->findClosestEnemy( protector, p.m_fightRadius, AI::CAN_SEE | AI::CAN_ATTACK );
			if (enemy)
			{
				link.m_lastFightFrame = frame;
				if (ai->isIdle())
				{
					Coord3D at = *enemy->getPosition();
					ai->aiAttackMoveToPosition( &at, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
					link.m_lastOrderFrame = frame;
				}
			}
			const Bool tooLong = frame - link.m_stateFrame > p.m_leashFrames;
			const Bool tooFar = dist2D( *pos, link.m_home ) > p.m_leashDistance;
			const Bool quiet = frame - link.m_lastFightFrame > p.m_quietFrames;
			if (tooLong || tooFar || quiet)
			{
				ASSIST_DEBUG(( "ASSIST protect %d goes home (%s)", (int)link.m_protector, tooLong ? "too long" : tooFar ? "too far" : "quiet" ));
				link.m_state = PROTECT_RETURNING;
				link.m_stateFrame = frame;
				goHome( link, protector, frame );
			}
			break;
		}

		case PROTECT_RETURNING:
		{
			if (dist2D( *pos, link.m_home ) <= p.m_homeTolerance)
			{
				ASSIST_DEBUG(( "ASSIST protect %d is home", (int)link.m_protector ));
				link.m_state = PROTECT_AT_HOME;
				link.m_stateFrame = frame;
			}
			else if (ai->isIdle() && frame - link.m_lastOrderFrame > LOGICFRAMES_PER_SECOND)
				goHome( link, protector, frame );
			break;
		}

		case PROTECT_AT_HOME:
		default:
		{
			// stray from the post (pushed, or chased something): go back when idle
			if (ai->isIdle() && dist2D( *pos, link.m_home ) > p.m_homeTolerance && frame - link.m_lastOrderFrame > 2 * LOGICFRAMES_PER_SECOND)
				goHome( link, protector, frame );
			break;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::update( UnsignedInt frame )
{
	if (m_links.empty())
		return;

	// forget links whose protector is gone, or whose protected are all gone
	if (frame % 30 == 7)
	{
		for (LinkMap::iterator it = m_links.begin(); it != m_links.end(); )
		{
			Object *o = TheGameLogic->findObjectByID( it->first );
			std::vector<ObjectID> t;
			resolveTargets( it->second, t );
			const Bool noTargets = t.empty() && it->second.m_squad < 0;
			if (o == nullptr || o->isEffectivelyDead() || o->getControllingPlayer() == nullptr ||
					o->getControllingPlayer()->getPlayerIndex() != it->second.m_player || noTargets)
				m_links.erase( it++ );
			else
				++it;
		}
		for (std::map<ObjectID, UnsignedInt>::iterator s = m_damageSeen.begin(); s != m_damageSeen.end(); )
		{
			Object *o = TheGameLogic->findObjectByID( s->first );
			if (o == nullptr || o->isEffectivelyDead())
				m_damageSeen.erase( s++ );
			else
				++s;
		}
	}

	// the alarms: every third frame
	if (frame % 3 == 0)
		scanAlarms( frame );

	// each link is looked at every ten frames, spread by the id of the protector
	for (LinkMap::iterator it = m_links.begin(); it != m_links.end(); ++it)
	{
		if (((UnsignedInt)it->first + frame) % 10 != 0)
			continue;
		Object *protector = TheGameLogic->findObjectByID( it->first );
		if (protector == nullptr || protector->isEffectivelyDead() || protector->getAIUpdateInterface() == nullptr)
			continue;
		updateLink( it->second, protector, frame );
	}
}

//-------------------------------------------------------------------------------------------------
void ProtectManager::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	const Bool loading = xfer->getXferMode() == XFER_LOAD;
	UnsignedInt count = (UnsignedInt)m_links.size();
	xfer->xferUnsignedInt( &count );
	xfer->xferUnsignedInt( &m_nextScan );

	if (loading)
		m_links.clear();

	LinkMap::iterator it = m_links.begin();
	for (UnsignedInt i = 0; i < count; ++i)
	{
		ProtectLink tmp;
		ProtectLink *l = &tmp;
		if (!loading)
			l = &it->second;

		xfer->xferObjectID( &l->m_protector );
		xfer->xferShort( &l->m_player );
		xfer->xferShort( &l->m_squad );
		UnsignedInt n = (UnsignedInt)l->m_targets.size();
		xfer->xferUnsignedInt( &n );
		if (loading)
			l->m_targets.resize( n );
		for (UnsignedInt t = 0; t < n; ++t)
			xfer->xferObjectID( &l->m_targets[t] );
		xfer->xferCoord3D( &l->m_home );
		xfer->xferCoord2D( &l->m_offset );
		xfer->xferCoord3D( &l->m_lastCentre );
		xfer->xferBool( &l->m_hasOffset );
		xfer->xferUnsignedByte( &l->m_state );
		xfer->xferUnsignedInt( &l->m_stateFrame );
		xfer->xferUnsignedInt( &l->m_lastOrderFrame );
		xfer->xferUnsignedInt( &l->m_lastFightFrame );
		xfer->xferCoord3D( &l->m_alert );

		if (loading)
			m_links[l->m_protector] = *l;
		else
			++it;
	}

	// the damage that was handled
	UnsignedInt seenCount = (UnsignedInt)m_damageSeen.size();
	xfer->xferUnsignedInt( &seenCount );
	if (loading)
	{
		m_damageSeen.clear();
		for (UnsignedInt i = 0; i < seenCount; ++i)
		{
			ObjectID id = INVALID_ID;
			UnsignedInt stamp = 0;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedInt( &stamp );
			m_damageSeen[id] = stamp;
		}
	}
	else
	{
		for (std::map<ObjectID, UnsignedInt>::iterator s = m_damageSeen.begin(); s != m_damageSeen.end(); ++s)
		{
			ObjectID id = s->first;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedInt( &s->second );
		}
	}
}
