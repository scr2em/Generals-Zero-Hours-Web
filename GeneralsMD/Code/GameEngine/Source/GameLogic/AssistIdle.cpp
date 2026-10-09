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

// AssistIdle.cpp
// Which of a player's units stand idle: the army units and the workers.  These are questions only, for the idle hotkeys
// and the idle counter (GameClient/AssistUIIdle.cpp) and for the test bench; nothing here gives an order or changes the
// game.  The definition of an army unit is the one of the "base under attack" response (AssistBaseAlert.cpp).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"

namespace
{
	struct Collect
	{
		Int											m_kind;
		std::vector<Object *>		*m_out;
	};

	void collectIdle( Object *obj, void *userData )
	{
		Collect *c = (Collect *)userData;
		if (PlayerAssist::isIdleUnit( obj, c->m_kind ))
			c->m_out->push_back( obj );
	}

	Bool idLess( const Object *a, const Object *b )
	{
		return a->getID() < b->getID();
	}
}

//-------------------------------------------------------------------------------------------------
/// A unit that can shoot (from the combat figures of its template: no names), is not a worker, harvester or support unit and
/// is not a structure, and that the player can order about (not inside a transport or a building, not held).
Bool PlayerAssist::isArmyUnit( const Object *obj )
{
	if (obj == nullptr || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->isKindOf( KINDOF_IMMOBILE ) ||
			obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || obj->isContained() || obj->isDisabledByType( DISABLED_HELD ))
		return FALSE;
	if (obj->getAIUpdateInterface() == nullptr)
		return FALSE;
	const AICombatFigures *f = AICombatModel::figures( obj->getTemplate() );
	return f && f->m_armed && f->m_supportLevel == 0 && (f->m_canHitGround || f->m_canHitAir);
}

//-------------------------------------------------------------------------------------------------
/// A builder (dozer, worker) or a supply gatherer.
Bool PlayerAssist::isWorker( const Object *obj )
{
	if (obj == nullptr || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->getAIUpdateInterface() == nullptr)
		return FALSE;
	const AIUpdateInterface *ai = obj->getAIUpdateInterface();
	return obj->isKindOf( KINDOF_DOZER ) || obj->isKindOf( KINDOF_HARVESTER ) || ai->getDozerAIInterface() != nullptr ||
		ai->getSupplyTruckAIInterface() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::isIdleUnit( Object *obj, Int kind )
{
	if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabledByType( DISABLED_HELD ) ||
			obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ) || obj->testStatus( OBJECT_STATUS_SOLD ))
		return FALSE;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai == nullptr || !ai->isIdle())
		return FALSE;

	if (kind == IDLE_ARMY)
		return isArmyUnit( obj );

	if (!isWorker( obj ))
		return FALSE;
	// a builder with a job to do, or a gatherer on its round, is not idle even when its AI is between two orders
	DozerAIInterface *dozer = ai->getDozerAIInterface();
	if (dozer && (dozer->getCurrentTask() != DOZER_TASK_INVALID || dozer->isAnyTaskPending()))
		return FALSE;
	const SupplyTruckAIInterface *truck = ai->getSupplyTruckAIInterface();
	if (truck && truck->isCurrentlyFerryingSupplies())
		return FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::idleUnits( const Player *player, Int kind, std::vector<Object *> &out )
{
	out.clear();
	if (player == nullptr)
		return;
	Collect c;
	c.m_kind = kind;
	c.m_out = &out;
	player->iterateObjects( collectIdle, &c );
	std::sort( out.begin(), out.end(), idLess );
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::pickIdle( const Player *player, Int pick, ObjectID after, std::vector<ObjectID> &out )
{
	out.clear();
	const Int kind = pick == IDLE_PICK_WORKER_NEXT ? IDLE_WORKER : IDLE_ARMY;
	std::vector<Object *> list;
	idleUnits( player, kind, list );
	const char *what = kind == IDLE_WORKER ? "workers" : "army";
	if (list.empty())
	{
		ASSIST_DEBUG(( "ASSIST idle %s: none idle", what ));
		return;
	}
	if (pick == IDLE_PICK_ARMY_ALL)
	{
		for (size_t i = 0; i < list.size(); ++i)
			out.push_back( list[i]->getID() );
		ASSIST_DEBUG(( "ASSIST idle army: selects all %d", (int)out.size() ));
		return;
	}
	Object *next = list[0];
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i]->getID() > after)
		{
			next = list[i];
			break;
		}
	}
	out.push_back( next->getID() );
	ASSIST_DEBUG(( "ASSIST idle %s: %d idle, selects unit %d %s", what, (int)list.size(), (int)next->getID(), next->getTemplate()->getName().str() ));
}
