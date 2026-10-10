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

// AssistRepeat.cpp
// The simulation side of repeat production.  The player switches it on for a production building (MSG_ASSIST_REPEAT_PRODUCTION);
// from then on, whenever the building's queue runs empty because its last unit came out, the building queues that unit
// again: through the building's own production queue (ProductionUpdate::queueCreateUnit, as the computer players do it), so
// the cost is paid and the unit is built exactly as if the player had clicked.  It builds again only while the player's
// money stays at or above the reserve the player set (money - cost >= reserve) and while the unit can be built at all
// (BuildAssistant::canMakeUnit: prerequisites, a working building, the unit limits); otherwise it waits and tries again
// every frame.  When the player empties the queue by cancelling, it does not build again until the player queues a unit.
//
// Only units repeat (not upgrades).  The state is per building, ordered by object id, saved with the game and part of
// the CRC (PlayerAssist::xfer).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AssistOptions.h"
#include "Common/BuildAssistant.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

namespace
{
	enum { WAIT_NONE = 0, WAIT_MONEY = 1, WAIT_CANNOT = 2 };
	const Int MAX_RESERVE = 1000000;

	/// The unit of the last unit entry of the queue (null if there is none).
	const ThingTemplate *lastQueuedUnit( ProductionUpdateInterface *pu )
	{
		const ThingTemplate *last = nullptr;
		for (const ProductionEntry *e = pu->firstProduction(); e; e = pu->nextProduction( e ))
		{
			if (e->getProductionType() == PRODUCTION_UNIT && e->getProductionObject())
				last = e->getProductionObject();
		}
		return last;
	}

	const char *nameOf( const ThingTemplate *t )
	{
		return t ? t->getName().str() : "nothing yet";
	}
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::isRepeatFactory( const Object *obj )
{
	if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf( KINDOF_STRUCTURE ))
		return FALSE;
	return const_cast<Object *>( obj )->getProductionUpdateInterface() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::repeatOn( ObjectID factory ) const
{
	return m_repeat.find( factory ) != m_repeat.end();
}

//-------------------------------------------------------------------------------------------------
const ThingTemplate *PlayerAssist::repeatUnit( ObjectID factory ) const
{
	RepeatMap::const_iterator it = m_repeat.find( factory );
	return it == m_repeat.end() ? nullptr : it->second.m_unit;
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::repeatWaiting( ObjectID factory ) const
{
	RepeatMap::const_iterator it = m_repeat.find( factory );
	return it != m_repeat.end() && it->second.m_waiting && it->second.m_logged != WAIT_NONE;
}

//-------------------------------------------------------------------------------------------------
Int PlayerAssist::repeatReserve( Int playerIndex ) const
{
	return playerIndex >= 0 && playerIndex < MAX_DEFEND ? m_repeatReserve[playerIndex] : 0;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::repeatList( Int playerIndex, std::vector<ObjectID> &out ) const
{
	out.clear();
	for (RepeatMap::const_iterator it = m_repeat.begin(); it != m_repeat.end(); ++it)
	{
		if (it->second.m_owner == playerIndex)
			out.push_back( it->first );
	}
}

//-------------------------------------------------------------------------------------------------
/// (Int on: 0 off, 1 on, 2 only the reserve; Int reserve; factory ids ...)
void PlayerAssist::setRepeat( Player *player, const GameMessage *msg )
{
	const Int count = (Int)msg->getArgumentCount();
	if (count < 2)
		return;
	const Int on = msg->getArgument( 0 )->integer;
	Int reserve = msg->getArgument( 1 )->integer;
	if (reserve < 0)
		reserve = 0;
	if (reserve > MAX_RESERVE)
		reserve = MAX_RESERVE;
	const Int index = player->getPlayerIndex();
	if (index < 0 || index >= MAX_DEFEND)
		return;
	m_repeatReserve[index] = reserve;
	if (on == 2)
	{
		ASSIST_DEBUG(( "ASSIST repeat production: reserve %d for player %d", reserve, index ));
		return;
	}

	for (Int i = 2; i < count; ++i)
	{
		Object *factory = TheGameLogic->findObjectByID( msg->getArgument( i )->objectID );
		if (factory == nullptr || factory->getControllingPlayer() != player || !isRepeatFactory( factory ))
			continue;
		const ObjectID id = factory->getID();
		if (on == 1)
		{
			if (m_repeat.find( id ) == m_repeat.end())
			{
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				RepeatState st;
				st.m_unit = lastQueuedUnit( pu );
				st.m_queueCount = pu->getProductionCount();
				st.m_owner = index;
				m_repeat[id] = st;
			}
			ASSIST_DEBUG(( "ASSIST repeat production: factory %d on (%s), reserve %d", (int)id, nameOf( m_repeat[id].m_unit ), reserve ));
		}
		else if (m_repeat.erase( id ) > 0)
			ASSIST_DEBUG(( "ASSIST repeat production: factory %d off", (int)id ));
	}
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::onUnitProduced( Object *factory, Object *unit )
{
	if (!m_allowed || factory == nullptr || unit == nullptr)
		return;
	RepeatMap::iterator it = m_repeat.find( factory->getID() );
	if (it == m_repeat.end())
		return;
	// (what it builds again is taken from its queue: a unit that comes out was queued first)
	it->second.m_producedFrame = TheGameLogic->getFrame();
	ASSIST_DEBUG(( "ASSIST repeat production: factory %d built %s", (int)factory->getID(), unit->getTemplate()->getName().str() ));
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::updateRepeat( UnsignedInt now )
{
	for (RepeatMap::iterator it = m_repeat.begin(); it != m_repeat.end(); )
	{
		const ObjectID id = it->first;
		RepeatState &st = it->second;
		Object *factory = TheGameLogic->findObjectByID( id );
		Player *player = factory ? factory->getControllingPlayer() : nullptr;
		ProductionUpdateInterface *pu = factory && !factory->isEffectivelyDead() ? factory->getProductionUpdateInterface() : nullptr;
		if (pu == nullptr || player == nullptr || player->getPlayerIndex() != st.m_owner)
		{
			ASSIST_DEBUG(( "ASSIST repeat production: factory %d is gone or changed hands, repeat off", (int)id ));
			m_repeat.erase( it++ );
			continue;
		}
		++it;

		// what the queue holds now: the unit the player queued last is the one to build again
		const UnsignedInt count = pu->getProductionCount();
		if (count > 0)
		{
			const ThingTemplate *last = lastQueuedUnit( pu );
			if (last)
				st.m_unit = last;
			st.m_waiting = FALSE;
			st.m_logged = WAIT_NONE;
		}
		else if (st.m_queueCount > 0)
		{
			// the queue ran empty this frame: by a unit that came out, or by the player cancelling
			if (st.m_producedFrame == now)
				st.m_waiting = TRUE;
			else
			{
				st.m_waiting = FALSE;
				ASSIST_DEBUG(( "ASSIST repeat production: factory %d queue emptied by the player, waits for a new order", (int)id ));
			}
		}
		st.m_queueCount = count;

		if (!st.m_waiting || count > 0 || st.m_unit == nullptr)
			continue;

		// build it again, if the money and the rules allow
		const Int cost = st.m_unit->calcCostToBuild( player );
		const Int money = (Int)player->getMoney()->countMoney();
		const Int reserve = repeatReserve( st.m_owner );
		if (money - cost < reserve)
		{
			if (st.m_logged != WAIT_MONEY)
				ASSIST_DEBUG(( "ASSIST repeat production: factory %d waits: money %d, cost %d, reserve %d", (int)id, money, cost, reserve ));
			st.m_logged = WAIT_MONEY;
			continue;
		}
		const CanMakeType can = TheBuildAssistant->canMakeUnit( factory, st.m_unit );
		if (can != CANMAKE_OK)
		{
			if (st.m_logged != WAIT_CANNOT)
				ASSIST_DEBUG(( "ASSIST repeat production: factory %d cannot build %s now (reason %d), waits", (int)id, nameOf( st.m_unit ), (int)can ));
			st.m_logged = WAIT_CANNOT;
			continue;
		}
		if (pu->queueCreateUnit( st.m_unit, pu->requestUniqueUnitID() ))
		{
			ASSIST_DEBUG(( "ASSIST repeat production: factory %d queues %s again (money %d, cost %d, reserve %d)", (int)id, nameOf( st.m_unit ), money, cost, reserve ));
			st.m_waiting = FALSE;
			st.m_logged = WAIT_NONE;
			st.m_queueCount = pu->getProductionCount();
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::xferRepeat( Xfer *xfer )
{
	for (Int i = 0; i < MAX_DEFEND; ++i)
		xfer->xferInt( &m_repeatReserve[i] );

	UnsignedInt count = (UnsignedInt)m_repeat.size();
	xfer->xferUnsignedInt( &count );
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_repeat.clear();
		for (UnsignedInt i = 0; i < count; ++i)
		{
			ObjectID id = INVALID_ID;
			AsciiString unit;
			RepeatState st;
			xfer->xferObjectID( &id );
			xfer->xferAsciiString( &unit );
			xfer->xferUnsignedInt( &st.m_producedFrame );
			xfer->xferUnsignedInt( &st.m_queueCount );
			xfer->xferBool( &st.m_waiting );
			xfer->xferInt( &st.m_owner );
			xfer->xferUnsignedByte( &st.m_logged );
			st.m_unit = unit.isEmpty() ? nullptr : TheThingFactory->findTemplate( unit );
			m_repeat[id] = st;
		}
	}
	else
	{
		for (RepeatMap::iterator it = m_repeat.begin(); it != m_repeat.end(); ++it)
		{
			ObjectID id = it->first;
			RepeatState &st = it->second;
			AsciiString unit = st.m_unit ? st.m_unit->getName() : AsciiString::TheEmptyString;
			xfer->xferObjectID( &id );
			xfer->xferAsciiString( &unit );
			xfer->xferUnsignedInt( &st.m_producedFrame );
			xfer->xferUnsignedInt( &st.m_queueCount );
			xfer->xferBool( &st.m_waiting );
			xfer->xferInt( &st.m_owner );
			xfer->xferUnsignedByte( &st.m_logged );
		}
	}
}
