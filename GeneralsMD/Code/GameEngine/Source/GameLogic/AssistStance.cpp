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

// AssistStance.cpp
// Unit stances of the player assists: kite, retreat when damaged, spread out, split fire.  The player switches a stance
// on for the selected units (MSG_ASSIST_STANCE); from then on the simulation looks at those units a few times a second
// and orders what the Expert computer player would order for its own units.  The decisions come from the functions
// of AITacticsCore (shared with the computer player): when a step back is worth it, where to step, which shots go
// to which target.  Orders the stances give carry the command source of the AI; an order of the player always wins
// (the unit's last command source is the player again), and ends whatever the stance was doing with the unit.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/ActionManager.h"
#include "Common/AssistOptions.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/AITacticsCore.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

namespace
{
	const UnsignedInt LOOK_EVERY = 4;													// frames between two looks at a unit
	const UnsignedInt RETREAT_FRAMES = 180 * LOGICFRAMES_PER_SECOND;	// longest trip to a place of repair, the wait and the repair included
	const UnsignedInt RALLY_FRAMES = 40 * LOGICFRAMES_PER_SECOND;			// longest trip to the rally point
	const UnsignedInt STUCK_FRAMES = 3 * LOGICFRAMES_PER_SECOND;			// idle this long on a trip: the order was not carried out
	const UnsignedInt RESCAN_FRAMES = 5 * LOGICFRAMES_PER_SECOND;			// waiting at the rally point: look for a place of repair again
	const UnsignedByte MAX_TRIES = 3;																	// orders to the same place of repair before the rally point
	const UnsignedInt BLOCK_FRAMES = 15 * LOGICFRAMES_PER_SECOND;			// after an order of the player a unit does not retreat for this long

	Real dist2D( const Coord3D &a, const Coord3D &b )
	{
		return sqrtf( (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) );
	}

	Real healthOf( Object *obj )
	{
		BodyModuleInterface *body = obj->getBodyModule();
		return body && body->getMaxHealth() > 0.0f ? body->getHealth() / body->getMaxHealth() : 1.0f;
	}

	/// A unit that a stance makes sense for: armed, mobile on the ground.
	Bool eligible( Object *obj )
	{
		if (obj->isEffectivelyDead() || obj->isKindOf( KINDOF_STRUCTURE ) || obj->getAI() == nullptr || !obj->isMobile())
			return FALSE;
		const AICombatFigures *f = AICombatModel::figures( obj->getTemplate() );
		return f && f->m_armed && !f->m_structure && !f->m_airborne && f->m_speed > 0.0f;
	}

	/// The enemies a unit can see around it, as the tactics see them.
	class VisibleEnemies : public AITactics::ContactList
	{
	public:
		VisibleEnemies( Object *unit, Real radius )
		{
			PartitionFilterRelationship enemies( unit, PartitionFilterRelationship::ALLOW_ENEMIES );
			PartitionFilterAlive alive;
			PartitionFilterFreeOfFog fog( unit->getControllingPlayer()->getPlayerIndex() );
			PartitionFilterStealthedAndUndetected stealth( unit, false );
			PartitionFilter *filters[] = { &enemies, &alive, &fog, &stealth, nullptr };
			SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( unit->getPosition(), radius, FROM_CENTER_2D, filters );
			MemoryPoolObjectHolder hold( iter );
			for (Object *o = iter->first(); o; o = iter->next())
			{
				if (o->isOffMap())
					continue;
				AITactics::Contact c;
				c.m_id = o->getID();
				c.m_pos = *o->getPosition();
				c.m_fig = AICombatModel::figures( o->getTemplate() );
				c.m_age = 0;
				if (c.m_fig)
					m_list.push_back( c );
			}
		}
		virtual Int count() const override { return (Int)m_list.size(); }
		virtual Bool get( Int index, AITactics::Contact *out ) const override { *out = m_list[index]; return TRUE; }
		std::vector<AITactics::Contact> m_list;
	};

	/// A structure the unit can be repaired or healed at: the nearest one.
	struct FacilitySearch
	{
		Object	*m_unit;
		Object	*m_best;
		Real		m_bestDist;
		Bool		m_heal;
	};

	void findFacility( Object *obj, void *userData )
	{
		FacilitySearch *s = (FacilitySearch *)userData;
		if (!obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead())
			return;
		const Bool repair = TheActionManager->canGetRepairedAt( s->m_unit, obj, CMD_FROM_AI );
		const Bool heal = !repair && TheActionManager->canGetHealedAt( s->m_unit, obj, CMD_FROM_AI );
		if (!repair && !heal)
			return;
		const Real d = dist2D( *s->m_unit->getPosition(), *obj->getPosition() );
		if (s->m_best == nullptr || d < s->m_bestDist || (d == s->m_bestDist && obj->getID() < s->m_best->getID()))
		{
			s->m_best = obj;
			s->m_bestDist = d;
			s->m_heal = heal;
		}
	}

	/// The structures of a player: where the base is.
	struct BaseSearch
	{
		Real	m_sumX, m_sumY;
		Int		m_count;
		Coord3D	m_first;
		Real	m_maxDist;
	};

	void addStructure( Object *obj, void *userData )
	{
		BaseSearch *b = (BaseSearch *)userData;
		if (!obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_DEFENSIVE_WALL ) || obj->isOffMap() ||
				obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
			return;
		b->m_sumX += obj->getPosition()->x;
		b->m_sumY += obj->getPosition()->y;
		++b->m_count;
	}

	void maxStructureDist( Object *obj, void *userData )
	{
		BaseSearch *b = (BaseSearch *)userData;
		if (!obj->isKindOf( KINDOF_STRUCTURE ) || obj->isEffectivelyDead() || obj->isKindOf( KINDOF_DEFENSIVE_WALL ) || obj->isOffMap() ||
				obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION ))
			return;
		const Real d = dist2D( *obj->getPosition(), b->m_first );
		if (d > b->m_maxDist)
			b->m_maxDist = d;
	}

	/// The place in front of the base on the side of the enemy (the rally point of the computer player), or false when the player has no base.
	Bool rallyPointOf( Player *player, Coord3D *out )
	{
		BaseSearch b;
		b.m_sumX = b.m_sumY = 0.0f;
		b.m_count = 0;
		b.m_maxDist = 0.0f;
		b.m_first.x = b.m_first.y = b.m_first.z = 0.0f;
		player->iterateObjects( addStructure, &b );
		if (b.m_count == 0)
			return FALSE;
		Coord3D base;
		base.x = b.m_sumX / (Real)b.m_count;
		base.y = b.m_sumY / (Real)b.m_count;
		base.z = TheTerrainLogic->getGroundHeight( base.x, base.y );
		b.m_first = base;
		player->iterateObjects( maxStructureDist, &b );

		Coord3D toward;
		toward.x = toward.y = toward.z = 0.0f;
		Bool haveTarget = FALSE;
		Player *enemy = player->getCurrentEnemy();
		if (enemy == nullptr)
		{
			for (Int i = 0; i < ThePlayerList->getPlayerCount() && enemy == nullptr; ++i)
			{
				Player *other = ThePlayerList->getNthPlayer( i );
				if (other && other != player && player->getRelationship( other->getDefaultTeam() ) == ENEMIES && !other->isPlayerObserver())
					enemy = other;
			}
		}
		if (enemy)
			haveTarget = AITactics::playerStart( enemy, &toward );
		if (!haveTarget)
		{
			Region3D extent;
			TheTerrainLogic->getExtent( &extent );
			toward.x = (extent.lo.x + extent.hi.x) * 0.5f;
			toward.y = (extent.lo.y + extent.hi.y) * 0.5f;
		}
		AITactics::rallyAhead( base, b.m_maxDist, toward, out );
		return TRUE;
	}
}

//-------------------------------------------------------------------------------------------------
Int PlayerAssist::stanceOf( ObjectID id ) const
{
	StanceMap::const_iterator it = m_stances.find( id );
	return it == m_stances.end() ? 0 : (Int)it->second.m_flags;
}

//-------------------------------------------------------------------------------------------------
Int PlayerAssist::retreatPercentOf( ObjectID id ) const
{
	StanceMap::const_iterator it = m_stances.find( id );
	return it == m_stances.end() ? 0 : (Int)it->second.m_retreatPercent;
}

//-------------------------------------------------------------------------------------------------
/// The stance bits that every eligible unit of the list has; with 'retreatPercent' the retreat setting they share (0 when they differ).
Int PlayerAssist::sharedStance( const std::vector<ObjectID> &ids, Int *retreatPercent ) const
{
	Int flags = 0xF;
	Int percent = -1;
	Int seen = 0;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || !eligible( obj ))
			continue;
		++seen;
		flags &= stanceOf( ids[i] );
		const Int p = retreatPercentOf( ids[i] );
		if (percent == -1)
			percent = p;
		else if (percent != p)
			percent = 0;
	}
	if (retreatPercent)
		*retreatPercent = percent < 0 ? 0 : percent;
	return seen > 0 ? flags : 0;
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::stanceEligible( const Object *obj )
{
	return obj != nullptr && eligible( const_cast<Object *>( obj ) );
}

//-------------------------------------------------------------------------------------------------
/// Switch stances on or off for the units of the group: 'mask' are the bits to change, 'value' what they become.
void PlayerAssist::setStance( Player *player, AIGroup *group, Int mask, Int value, Int percent )
{
	if (group == nullptr)
		return;
	const VecObjectID ids = group->getAllIDs();
	Int changed = 0;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || !eligible( obj ) || obj->getControllingPlayer() != player)
			continue;
		StanceState &st = m_stances[ids[i]];
		st.m_flags = (UnsignedByte)((st.m_flags & ~mask) | (value & mask));
		if (mask & STANCE_RETREAT)
		{
			st.m_retreatPercent = (UnsignedByte)(percent < 0 ? 0 : (percent > 90 ? 90 : percent));
			if (st.m_retreatPercent == 0)
				st.m_flags = (UnsignedByte)(st.m_flags & ~STANCE_RETREAT);
		}
		if ((st.m_flags & STANCE_RETREAT) == 0)
			st.m_retreatPercent = 0;
		if (st.m_flags == 0)
			m_stances.erase( ids[i] );
		++changed;
	}
	ASSIST_DEBUG(( "ASSIST stance set mask=%d value=%d percent=%d for %d units", mask, value, percent, changed ));
}

//-------------------------------------------------------------------------------------------------
/// Does the unit's weapon still have a few frames to wait?  (A step is worth it only then.)
static Bool weaponReady( Object *unit, UnsignedInt now )
{
	Weapon *weapon = unit->getCurrentWeapon();
	return weapon == nullptr || weapon->getPossibleNextShotFrame() <= now + 8;
}

//-------------------------------------------------------------------------------------------------
/// Is this enemy still there for the player to shoot at?
static Bool visibleTo( const Object *victim, Int playerIndex )
{
	if (victim->isEffectivelyDead() || victim->isOffMap())
		return FALSE;
	if (victim->getShroudedStatus( playerIndex ) != OBJECTSHROUD_CLEAR)
		return FALSE;
	if (victim->testStatus( OBJECT_STATUS_STEALTHED ) && !victim->testStatus( OBJECT_STATUS_DETECTED ))
		return FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/// Send the unit to the nearest place of repair or healing, or else to the rally point in front of the base.  Called
/// when the unit drops below its threshold (it remembers where it stood, to go back there), and again when the place
/// it was going to is gone or a place turns up while it waits at the rally point (where it stood is kept).
void PlayerAssist::startRetreat( Object *unit, StanceState &st, UnsignedInt now )
{
	AIUpdateInterface *ai = unit->getAI();
	Player *owner = unit->getControllingPlayer();

	if (st.m_phase != PHASE_RETREAT && st.m_phase != PHASE_PARKED)
		st.m_from = *unit->getPosition();
	st.m_victim = INVALID_ID;
	st.m_tries = 0;

	FacilitySearch fs;
	fs.m_unit = unit;
	fs.m_best = nullptr;
	fs.m_bestDist = 0.0f;
	fs.m_heal = FALSE;
	owner->iterateObjects( findFacility, &fs );

	if (fs.m_best)
	{
		// enter a heal building (HealContain) or dock at a repair building (RepairDockUpdate): what a right click on it does
		if (fs.m_heal)
			ai->aiGetHealed( fs.m_best, CMD_FROM_AI );
		else
			ai->aiGetRepaired( fs.m_best, CMD_FROM_AI );
		// An order the unit does not take changes nothing, not even the source of its last order (often the player's,
		// which would then look like an order of the player): there is a trip only when the unit is on its way.
		const AIStateType state = ai->getAIStateType();
		if (state == AI_ENTER || state == AI_DOCK)
		{
			st.m_facility = fs.m_best->getID();
			st.m_phase = PHASE_RETREAT;
			st.m_until = now + RETREAT_FRAMES;
			st.m_orderFrame = now;
			st.m_tries = 1;
			ASSIST_DEBUG(( "ASSIST stance retreat: unit %d at %d%% goes to %s %d", (int)unit->getID(), (int)(healthOf( unit ) * 100.0f), fs.m_heal ? "heal facility" : "repair facility", (int)fs.m_best->getID() ));
			return;
		}
		ASSIST_DEBUG(( "ASSIST stance retreat: unit %d cannot use facility %d", (int)unit->getID(), (int)fs.m_best->getID() ));
	}
	goToRally( unit, st, now );
}

//-------------------------------------------------------------------------------------------------
/// No place of repair: out of the fight to the rally point, and wait there.
void PlayerAssist::goToRally( Object *unit, StanceState &st, UnsignedInt now )
{
	Coord3D rally;
	st.m_facility = INVALID_ID;
	st.m_tries = 0;
	if (!rallyPointOf( unit->getControllingPlayer(), &rally ))
	{
		// no base: stay out of trouble where it is (a unit that was not on a trip yet is looked at again later)
		if (st.m_phase == PHASE_RETREAT || st.m_phase == PHASE_PARKED)
		{
			st.m_phase = PHASE_PARKED;
			st.m_to = *unit->getPosition();
			st.m_until = now + RESCAN_FRAMES;
		}
		return;
	}
	// A move of the AI given to a busy unit (fighting, or on an order of the player) is only a detour of 20 seconds, after
	// which it takes up what it was doing, and it keeps the source of that order: stop it first, so that the move is
	// its order.
	AIUpdateInterface *ai = unit->getAI();
	if (!ai->isIdle())
		ai->aiIdle( CMD_FROM_AI );
	ai->aiMoveToPosition( &rally, CMD_FROM_AI );
	st.m_phase = PHASE_RETREAT;
	st.m_until = now + RALLY_FRAMES;
	st.m_orderFrame = now;
	st.m_to = rally;
	ASSIST_DEBUG(( "ASSIST stance retreat: unit %d at %d%% goes to the rally point %.0f,%.0f", (int)unit->getID(), (int)(healthOf( unit ) * 100.0f), rally.x, rally.y ));
}

//-------------------------------------------------------------------------------------------------
/// A unit on its way to be repaired or healed, being mended, or waiting at the rally point.
void PlayerAssist::updateRetreat( Object *unit, StanceState &st, UnsignedInt now, Real health )
{
	AIUpdateInterface *ai = unit->getAI();

	if (st.m_facility != INVALID_ID)
	{
		// mended: the dock and the heal building let it go at full health (it is not looked at while it is inside)
		if (health >= 0.999f || (health >= 0.97f && ai->isIdle()))
		{
			endRetreat( unit, st, health );
			return;
		}
		Object *fac = TheGameLogic->findObjectByID( st.m_facility );
		const Bool heal = unit->isKindOf( KINDOF_INFANTRY );
		if (fac == nullptr || fac->isEffectivelyDead() ||
				!(heal ? TheActionManager->canGetHealedAt( unit, fac, CMD_FROM_AI ) : TheActionManager->canGetRepairedAt( unit, fac, CMD_FROM_AI )))
		{
			ASSIST_DEBUG(( "ASSIST stance retreat: unit %d lost facility %d", (int)unit->getID(), (int)st.m_facility ));
			startRetreat( unit, st, now );
			return;
		}
		if (now >= st.m_until)
		{
			ASSIST_DEBUG(( "ASSIST stance retreat: unit %d gives up on facility %d (too long)", (int)unit->getID(), (int)st.m_facility ));
			goToRally( unit, st, now );
			return;
		}
		// Standing about without being mended: the building did not take it (full, or the way is blocked).  Again a few
		// times, then the rally point.
		if (ai->isIdle() && now >= st.m_orderFrame + STUCK_FRAMES)
		{
			if (st.m_tries < MAX_TRIES)
			{
				if (heal)
					ai->aiGetHealed( fac, CMD_FROM_AI );
				else
					ai->aiGetRepaired( fac, CMD_FROM_AI );
				++st.m_tries;
				st.m_orderFrame = now;
				ASSIST_DEBUG(( "ASSIST stance retreat: unit %d tries facility %d again", (int)unit->getID(), (int)st.m_facility ));
			}
			else
			{
				ASSIST_DEBUG(( "ASSIST stance retreat: unit %d cannot use facility %d", (int)unit->getID(), (int)st.m_facility ));
				goToRally( unit, st, now );
			}
		}
		return;
	}

	// at the rally point, or on the way: back to the fight once it healed by itself (an upgrade, a medic, a veteran's self-repair)
	const Real enough = std::min( 0.97f, ((Real)st.m_retreatPercent + 35.0f) * 0.01f );
	if (health >= enough)
	{
		endRetreat( unit, st, health );
		return;
	}
	if (st.m_phase == PHASE_RETREAT)
	{
		const Bool arrived = dist2D( *unit->getPosition(), st.m_to ) < 80.0f;
		if (arrived || now >= st.m_until || (ai->isIdle() && now >= st.m_orderFrame + STUCK_FRAMES))
		{
			// there: stop, rather than push about among the others for the very spot
			if (arrived && !ai->isIdle())
				ai->aiIdle( CMD_FROM_AI );
			st.m_phase = PHASE_PARKED;
			st.m_until = now + RESCAN_FRAMES;
			ASSIST_DEBUG(( "ASSIST stance retreat: unit %d parks at the rally point", (int)unit->getID() ));
		}
		return;
	}
	// parked: now and then, look for a place of repair again (one may have been built)
	if (now >= st.m_until)
	{
		st.m_until = now + RESCAN_FRAMES;
		FacilitySearch fs;
		fs.m_unit = unit;
		fs.m_best = nullptr;
		fs.m_bestDist = 0.0f;
		fs.m_heal = FALSE;
		unit->getControllingPlayer()->iterateObjects( findFacility, &fs );
		if (fs.m_best)
			startRetreat( unit, st, now );
	}
}

//-------------------------------------------------------------------------------------------------
/// The unit is healthy again: back to where it was.
void PlayerAssist::endRetreat( Object *unit, StanceState &st, Real health )
{
	AIUpdateInterface *ai = unit->getAI();
	if (st.m_from.x != 0.0f || st.m_from.y != 0.0f)
	{
		const Coord3D back = st.m_from;
		ai->aiAttackMoveToPosition( &back, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
		ASSIST_DEBUG(( "ASSIST stance retreat: unit %d goes back to %.0f,%.0f at %d%%", (int)unit->getID(), back.x, back.y, (int)(health * 100.0f + 0.5f) ));
	}
	st.m_phase = PHASE_NONE;
	st.m_facility = INVALID_ID;
	st.m_tries = 0;
}

//-------------------------------------------------------------------------------------------------
/// The one unit: carry on what it is doing, or look for something to do.
void PlayerAssist::stanceUnit( Object *unit, StanceState &st, UnsignedInt now )
{
	AIUpdateInterface *ai = unit->getAI();
	Player *owner = unit->getControllingPlayer();
	const Int playerIndex = owner->getPlayerIndex();
	const AITactics::Params params = AITactics::paramsOf( TheAI->getAiData()->m_expertSkill );

	// An order of the player ends what the stance was doing with the unit.
	if (st.m_phase != PHASE_NONE && ai->getLastCommandSource() == CMD_FROM_PLAYER)
	{
		if (st.m_phase == PHASE_RETREAT || st.m_phase == PHASE_PARKED)
		{
			st.m_noRetreatUntil = now + BLOCK_FRAMES;
			ASSIST_DEBUG(( "ASSIST stance retreat: unit %d takes the order of the player (no retreat for %d s)", (int)unit->getID(), (int)(BLOCK_FRAMES / LOGICFRAMES_PER_SECOND) ));
		}
		st.m_phase = PHASE_NONE;
		st.m_facility = INVALID_ID;
		st.m_tries = 0;
	}

	const Real health = healthOf( unit );

	// ---- the unit is on its way to be repaired, being mended, or waits at the rally point
	if (st.m_phase == PHASE_RETREAT || st.m_phase == PHASE_PARKED)
	{
		updateRetreat( unit, st, now, health );
		return;
	}

	// ---- retreat when damaged: before anything else the stances do (a unit on a kite step pulls out too)
	if ((st.m_flags & STANCE_RETREAT) != 0 && health * 100.0f < (Real)st.m_retreatPercent && now >= st.m_noRetreatUntil &&
			((now + unit->getID()) % LOOK_EVERY) == 0)
	{
		startRetreat( unit, st, now );
		if (st.m_phase == PHASE_RETREAT || st.m_phase == PHASE_PARKED)
			return;
	}

	// ---- the unit is on a step (kite or spread)
	if (st.m_phase == PHASE_STEP || st.m_phase == PHASE_ATTACK)
	{
		if ((now & 1) != 0)
			return;
		Object *victim = TheGameLogic->findObjectByID( st.m_victim );
		const Bool victimOk = victim != nullptr && visibleTo( victim, playerIndex );
		if (st.m_phase == PHASE_STEP)
		{
			if (now >= st.m_until || weaponReady( unit, now ) || ai->isIdle())
			{
				if (victimOk)
				{
					ai->aiAttackObject( victim, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
					st.m_phase = PHASE_ATTACK;
					st.m_until = now + 12 * LOGICFRAMES_PER_SECOND;
				}
				else
				{
					st.m_phase = PHASE_NONE;
				}
			}
			return;
		}
		// attacking again: fired?  then another step if it is worth it; the target gone: done
		const UnsignedInt phaseStart = st.m_until - 12 * LOGICFRAMES_PER_SECOND;
		if (!victimOk || (now >= phaseStart + 10 && ai->getCurrentVictim() != victim) || now >= st.m_until)
		{
			st.m_phase = PHASE_NONE;
			return;
		}
		Coord3D to;
		UnsignedInt until;
		if ((st.m_flags & STANCE_KITE) != 0)
		{
			VisibleEnemies contacts( unit, unit->getCurrentWeapon() ? unit->getCurrentWeapon()->getAttackRange( unit ) + 200.0f : 300.0f );
			if (AITactics::planKite( params, unit, victim, nullptr, contacts, &to, &until ) == AITactics::KITE_YES)
			{
				st.m_phase = PHASE_STEP;
				st.m_until = until;
				ai->aiMoveToPosition( &to, CMD_FROM_AI );
				ASSIST_DEBUG(( "ASSIST stance kite: unit %d steps back from %d", (int)unit->getID(), (int)victim->getID() ));
				return;
			}
		}
		if ((st.m_flags & STANCE_SPREAD) != 0 && st.m_spacing > 0.0f && AITactics::planSpread( params, st.m_spacing, unit, victim, nullptr, &to, &until ))
		{
			st.m_phase = PHASE_STEP;
			st.m_until = until;
			ai->aiMoveToPosition( &to, CMD_FROM_AI );
			ASSIST_DEBUG(( "ASSIST stance spread: unit %d steps aside", (int)unit->getID() ));
		}
		return;
	}

	// ---- nothing going on: look at the unit now and then
	if (((now + unit->getID()) % LOOK_EVERY) != 0)
		return;

	Object *victim = ai->isAttacking() ? ai->getCurrentVictim() : nullptr;
	const Real range = unit->getCurrentWeapon() ? unit->getCurrentWeapon()->getAttackRange( unit ) : 0.0f;

	// split fire: damage that other units have assigned to the target already counts against it
	if ((st.m_flags & STANCE_SPLIT) != 0 && victim != nullptr && range > 0.0f && visibleTo( victim, playerIndex ))
	{
		AITactics::SplitLedger &ledger = m_ledgers[playerIndex < MAX_LEDGERS ? playerIndex : 0];
		const UnsignedInt window = (UnsignedInt)(params.m_splitWindowSeconds * LOGICFRAMES_PER_SECOND + 0.5f);
		const AICombatFigures *mine = AICombatModel::figures( unit->getTemplate() );
		const AICombatFigures *vf = AICombatModel::figures( victim->getTemplate() );
		BodyModuleInterface *vbody = victim->getBodyModule();
		if (mine && vf && vbody)
		{
			Real assigned = ledger.assigned( victim->getID(), now );
			if (st.m_splitTarget == victim->getID() && now < st.m_splitUntil)
				assigned -= st.m_splitDamage;		// what this unit itself has put on it
			if (assigned < 0.0f)
				assigned = 0.0f;

			if (AITactics::splitPenalty( assigned, vbody->getHealth() ) < 100.0f)
			{
				// not overkilled: keep this target and keep it on the ledger
				if (st.m_splitTarget != victim->getID() || now >= st.m_splitUntil)
				{
					st.m_splitTarget = victim->getID();
					st.m_splitDamage = AICombatModel::damagePerSecond( mine, vf ) * params.m_splitWindowSeconds;
					st.m_splitUntil = now + window;
					ledger.assign( victim->getID(), st.m_splitDamage, now, window );
				}
			}
			else
			{
				// enough is on its way: the best other target in reach
				Object *best = pickOtherTarget( unit, victim, range, ledger, now, params );
				if (best)
				{
					st.m_splitTarget = best->getID();
					st.m_splitDamage = AICombatModel::damagePerSecond( mine, AICombatModel::figures( best->getTemplate() ) ) * params.m_splitWindowSeconds;
					st.m_splitUntil = now + window;
					ledger.assign( best->getID(), st.m_splitDamage, now, window );
					ai->aiAttackObject( best, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
					ASSIST_DEBUG(( "ASSIST stance split: unit %d leaves %d (enough on its way) for %d", (int)unit->getID(), (int)victim->getID(), (int)best->getID() ));
					return;
				}
			}
		}
	}

	// the enemy's blasts: units keep apart while it has area weapons
	if ((st.m_flags & STANCE_SPREAD) != 0)
	{
		if (st.m_spacingUntil == 0 || now >= st.m_spacingUntil || st.m_spacing <= 0.0f)
		{
			VisibleEnemies seen( unit, 700.0f );
			Real blast = 0.0f;
			for (Int i = 0; i < seen.count(); ++i)
			{
				const AICombatFigures *f = seen.m_list[i].m_fig;
				if (f->m_armed && f->m_maxSplash >= params.m_splashRadiusThreshold && f->m_maxSplash > blast)
					blast = f->m_maxSplash;
			}
			if (blast > 0.0f)
			{
				st.m_spacing = AITactics::spacingForBlast( params, blast );
				st.m_spacingUntil = now + 45 * LOGICFRAMES_PER_SECOND;
			}
			else if (st.m_spacingUntil != 0 && now >= st.m_spacingUntil)
			{
				st.m_spacing = 0.0f;
				st.m_spacingUntil = 0;
			}
		}
	}

	Coord3D to;
	UnsignedInt until;
	if (victim == nullptr)
	{
		// standing around: do not stand in a clump while the enemy has area weapons
		if ((st.m_flags & STANCE_SPREAD) != 0 && st.m_spacing > 0.0f && ai->isIdle() && AITactics::planSpread( params, st.m_spacing, unit, nullptr, nullptr, &to, &until ))
		{
			ai->aiMoveToPosition( &to, CMD_FROM_AI );
			ASSIST_DEBUG(( "ASSIST stance spread: idle unit %d moves apart", (int)unit->getID() ));
		}
		return;
	}

	Bool kite = FALSE;
	if ((st.m_flags & STANCE_KITE) != 0)
	{
		VisibleEnemies contacts( unit, range + 200.0f );
		kite = AITactics::planKite( params, unit, victim, nullptr, contacts, &to, &until ) == AITactics::KITE_YES;
	}
	if (!kite)
	{
		if ((st.m_flags & STANCE_SPREAD) == 0 || st.m_spacing <= 0.0f || !AITactics::planSpread( params, st.m_spacing, unit, victim, nullptr, &to, &until ))
			return;
	}
	st.m_phase = PHASE_STEP;
	st.m_victim = victim->getID();
	st.m_until = until;
	ai->aiMoveToPosition( &to, CMD_FROM_AI );
	if (kite)
		ASSIST_DEBUG(( "ASSIST stance kite: unit %d steps back from %d", (int)unit->getID(), (int)victim->getID() ));
	else
		ASSIST_DEBUG(( "ASSIST stance spread: unit %d steps aside", (int)unit->getID() ));
}

//-------------------------------------------------------------------------------------------------
/// The enemy in reach that is the best target once 'current' has enough fire on it.
Object *PlayerAssist::pickOtherTarget( Object *unit, Object *current, Real range, AITactics::SplitLedger &ledger, UnsignedInt now, const AITactics::Params &params )
{
	const AICombatFigures *mine = AICombatModel::figures( unit->getTemplate() );
	if (mine == nullptr)
		return nullptr;

	PartitionFilterRelationship enemies( unit, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive alive;
	PartitionFilterRejectBuildings noBuildings( unit );
	PartitionFilterPossibleToAttack canAttack( ATTACK_NEW_TARGET, unit, CMD_FROM_AI );
	PartitionFilterFreeOfFog fog( unit->getControllingPlayer()->getPlayerIndex() );
	PartitionFilterStealthedAndUndetected stealth( unit, false );
	PartitionFilter *filters[] = { &enemies, &alive, &noBuildings, &canAttack, &fog, &stealth, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( unit, range, FROM_BOUNDINGSPHERE_2D, filters, ITER_SORTED_NEAR_TO_FAR );
	MemoryPoolObjectHolder hold( iter );

	// our units around: what a target can do to them is what makes it dangerous
	AITactics::Mates mates;
	{
		PartitionFilterAlive mateAlive;
		PartitionFilterRelationship friends( unit, PartitionFilterRelationship::ALLOW_ALLIES );
		PartitionFilter *mateFilters[] = { &friends, &mateAlive, nullptr };
		SimpleObjectIterator *mateIter = ThePartitionManager->iterateObjectsInRange( unit->getPosition(), 150.0f, FROM_CENTER_2D, mateFilters );
		MemoryPoolObjectHolder mateHold( mateIter );
		for (Object *m = mateIter->first(); m && mates.m_num < AITactics::Mates::MAX; m = mateIter->next())
		{
			const AICombatFigures *mf = AICombatModel::figures( m->getTemplate() );
			if (mf && mf->m_armed && !mf->m_structure)
				mates.add( mf );
		}
	}

	Object *best = nullptr;
	Real bestScore = 0.0f;
	Int examined = 0;
	for (Object *e = iter->first(); e && examined < 8; e = iter->next(), ++examined)
	{
		if (e == current)
			continue;
		const AICombatFigures *f = AICombatModel::figures( e->getTemplate() );
		if (f == nullptr)
			continue;
		BodyModuleInterface *body = e->getBodyModule();
		const Real dist = sqrtf( ThePartitionManager->getDistanceSquared( unit, e, FROM_BOUNDINGSPHERE_2D ) );
		const Real health = body && body->getMaxHealth() > 0.0f ? body->getHealth() / body->getMaxHealth() : 1.0f;
		AITactics::TargetScore ts;
		AITactics::scoreTarget( mine, f, health, dist, range, TRUE, mates, &ts );
		Real score = ts.m_threat;
		if (body)
			score -= AITactics::splitPenalty( ledger.assigned( e->getID(), now ), body->getHealth() );
		if (best == nullptr || score > bestScore)
		{
			best = e;
			bestScore = score;
		}
	}
	(void)params;
	return best;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::updateStances( UnsignedInt now )
{
	if (m_stances.empty())
		return;
	std::vector<ObjectID> gone;
	for (StanceMap::iterator it = m_stances.begin(); it != m_stances.end(); ++it)
	{
		Object *unit = TheGameLogic->findObjectByID( it->first );
		if (unit == nullptr || unit->isEffectivelyDead() || unit->getControllingPlayer() == nullptr)
		{
			gone.push_back( it->first );
			continue;
		}
		if (TheAssistOptions.m_debug && it->second.m_phase >= PHASE_RETREAT && now % 150 == 0)
			ASSIST_DEBUG(( "ASSIST stance state: unit %d phase %d health %d%% contained %d idle %d", (int)it->first, (int)it->second.m_phase, (int)(healthOf( unit ) * 100.0f),
				unit->isContained() ? 1 : 0, unit->getAI() && unit->getAI()->isIdle() ? 1 : 0 ));
		if (unit->isContained() || unit->isDisabled() || unit->getAI() == nullptr)
			continue;
		stanceUnit( unit, it->second, now );
	}
	for (size_t i = 0; i < gone.size(); ++i)
		m_stances.erase( gone[i] );
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::xferStances( Xfer *xfer, UnsignedByte version )
{
	const Bool loading = xfer->getXferMode() == XFER_LOAD;
	UnsignedInt count = (UnsignedInt)m_stances.size();
	xfer->xferUnsignedInt( &count );
	if (loading)
		m_stances.clear();
	StanceMap::iterator it = m_stances.begin();
	for (UnsignedInt i = 0; i < count; ++i)
	{
		ObjectID id = INVALID_ID;
		StanceState loaded;
		StanceState *st = &loaded;
		if (!loading)
		{
			id = it->first;
			st = &it->second;
		}
		xfer->xferObjectID( &id );
		xfer->xferUnsignedByte( &st->m_flags );
		xfer->xferUnsignedByte( &st->m_retreatPercent );
		xfer->xferUnsignedByte( &st->m_phase );
		xfer->xferObjectID( &st->m_victim );
		xfer->xferObjectID( &st->m_facility );
		xfer->xferUnsignedInt( &st->m_until );
		xfer->xferCoord3D( &st->m_from );
		xfer->xferCoord3D( &st->m_to );
		xfer->xferReal( &st->m_spacing );
		xfer->xferUnsignedInt( &st->m_spacingUntil );
		xfer->xferUnsignedInt( &st->m_noRetreatUntil );
		xfer->xferObjectID( &st->m_splitTarget );
		xfer->xferReal( &st->m_splitDamage );
		xfer->xferUnsignedInt( &st->m_splitUntil );
		if (version >= 6)
		{
			xfer->xferUnsignedInt( &st->m_orderFrame );
			xfer->xferUnsignedByte( &st->m_tries );
		}
		if (loading)
			m_stances[id] = loaded;
		else
			++it;
	}
	for (Int p = 0; p < MAX_LEDGERS; ++p)
	{
		for (Int e = 0; e < AITactics::SplitLedger::SIZE; ++e)
		{
			AITactics::SplitLedger::Entry &entry = m_ledgers[p].entry( e );
			xfer->xferObjectID( &entry.m_target );
			xfer->xferReal( &entry.m_damage );
			xfer->xferUnsignedInt( &entry.m_expire );
		}
	}
}
