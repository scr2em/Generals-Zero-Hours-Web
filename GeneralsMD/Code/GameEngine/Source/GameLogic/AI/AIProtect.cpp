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

// AIProtect.cpp
// The protect relation (see AIProtect.h): protectors answer an attack on the objects they protect, then go home.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIProtect.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"


#define PROTECT_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] PROTECT ", m_playerIndex, TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static inline UnsignedInt framesOf(Real seconds)
{
	const Int f = (Int)(seconds * LOGICFRAMES_PER_SECOND + 0.5f);
	return f < 1 ? 1 : (UnsignedInt)f;
}

/// Can the object be seen by the owner of 'viewer' right now?
static Bool isSeenBy(const Object *viewer, const Object *o)
{
	if (o->isEffectivelyDead() || o->isOffMap())
		return FALSE;
	if (o->getShroudedStatus(viewer->getControllingPlayer()->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
		return FALSE;
	if (o->testStatus(OBJECT_STATUS_STEALTHED) && !o->testStatus(OBJECT_STATUS_DETECTED))
		return FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
AIProtect::AIProtect()
{
	reset();
}

void AIProtect::reset()
{
	memset(m_protectors, 0, sizeof(m_protectors));
	memset(m_relations, 0, sizeof(m_relations));
	m_numProtectors = 0;
	m_numRelations = 0;
	m_nextUpdate = 0;
	m_alarms = 0;
	m_responses = 0;
	m_returns = 0;
	m_trace = FALSE;
	m_playerIndex = 0;
}

//-------------------------------------------------------------------------------------------------
AIProtect::Relation *AIProtect::findRelation( Int key )
{
	for (Int i = 0; i < m_numRelations; ++i)
	{
		if (m_relations[i].m_key == key)
			return &m_relations[i];
	}
	return nullptr;
}

AIProtect::Protector *AIProtect::findProtector( ObjectID unit )
{
	for (Int i = 0; i < m_numProtectors; ++i)
	{
		if (m_protectors[i].m_unit == unit)
			return &m_protectors[i];
	}
	return nullptr;
}

void AIProtect::dropProtector( Protector *p )
{
	*p = m_protectors[--m_numProtectors];
}

Bool AIProtect::isAway( ObjectID unit ) const
{
	for (Int i = 0; i < m_numProtectors; ++i)
	{
		if (m_protectors[i].m_unit == unit)
			return m_protectors[i].m_state != STATE_HOME;
	}
	return FALSE;
}

Bool AIProtect::isProtector( ObjectID unit ) const
{
	for (Int i = 0; i < m_numProtectors; ++i)
	{
		if (m_protectors[i].m_unit == unit)
			return TRUE;
	}
	return FALSE;
}

Bool AIProtect::isProtected( ObjectID object ) const
{
	for (Int r = 0; r < m_numRelations; ++r)
	{
		for (Int i = 0; i < m_relations[r].m_numProtected; ++i)
		{
			if (m_relations[r].m_protected[i] == object)
				return TRUE;
		}
	}
	return FALSE;
}

Int AIProtect::numAway() const
{
	Int n = 0;
	for (Int i = 0; i < m_numProtectors; ++i)
		if (m_protectors[i].m_state != STATE_HOME)
			++n;
	return n;
}

//-------------------------------------------------------------------------------------------------
Bool AIProtect::assign( Int key, const ObjectID *protectors, Int numProtectors, const ObjectID *protectedSet, Int numProtected,
	const AIProtectParams &params, const Coord3D *home )
{
	Relation *rel = findRelation(key);
	if (rel == nullptr)
	{
		if (m_numRelations >= MAX_GROUPS)
			return FALSE;
		rel = &m_relations[m_numRelations++];
		memset(rel, 0, sizeof(*rel));
		rel->m_key = key;
	}
	rel->m_params = params;
	rel->m_numProtected = numProtected > MAX_PROTECTED ? MAX_PROTECTED : numProtected;
	for (Int i = 0; i < rel->m_numProtected; ++i)
		rel->m_protected[i] = protectedSet[i];

	// The protectors of this relation that are not in the new list are let go (those that are away come home first).
	for (Int i = 0; i < m_numProtectors; ++i)
		m_protectors[i].m_keep = m_protectors[i].m_key != key;

	for (Int i = 0; i < numProtectors; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(protectors[i]);
		if (o == nullptr || o->isEffectivelyDead())
			continue;
		Protector *p = findProtector(protectors[i]);
		if (p == nullptr)
		{
			if (m_numProtectors >= MAX_PROTECTORS)
				continue;
			p = &m_protectors[m_numProtectors++];
			memset(p, 0, sizeof(*p));
			p->m_unit = protectors[i];
			p->m_state = STATE_HOME;
			p->m_home = *o->getPosition();
		}
		p->m_key = key;
		p->m_keep = TRUE;
		if (p->m_state == STATE_HOME)
			p->m_home = home ? *home : *o->getPosition();
	}
	for (Int i = 0; i < m_numProtectors; )
	{
		Protector &p = m_protectors[i];
		if (p.m_keep)
		{
			++i;
			continue;
		}
		if (p.m_state == STATE_HOME)
			dropProtector(&p);
		else
			++i;		// away: it finishes its trip and is dropped when it is home (see update)
	}
	return TRUE;
}

void AIProtect::release( Int key )
{
	for (Int i = 0; i < m_numRelations; ++i)
	{
		if (m_relations[i].m_key == key)
		{
			m_relations[i] = m_relations[--m_numRelations];
			break;
		}
	}
	for (Int i = 0; i < m_numProtectors; )
	{
		Protector &p = m_protectors[i];
		if (p.m_key == key)
		{
			if (p.m_state == STATE_HOME)
			{
				dropProtector(&p);
				continue;
			}
			p.m_key = -1;		// away: comes home, then goes
		}
		++i;
	}
}

void AIProtect::releaseProtector( ObjectID protector )
{
	Protector *p = findProtector(protector);
	if (p == nullptr)
		return;
	if (p->m_state != STATE_HOME)
		sendHome(p);
	p->m_key = -1;
}

//-------------------------------------------------------------------------------------------------
void AIProtect::onDamaged( const Object *victim, ObjectID attacker, Real amount )
{
	if (amount <= 0.0f || m_numRelations == 0)
		return;
	for (Int r = 0; r < m_numRelations; ++r)
	{
		Relation &rel = m_relations[r];
		for (Int i = 0; i < rel.m_numProtected; ++i)
		{
			if (rel.m_protected[i] != victim->getID())
				continue;
			if (!rel.m_alarm)
				++m_alarms;
			rel.m_alarm = TRUE;
			if (attacker != INVALID_ID)
				rel.m_attacker = attacker;
			rel.m_victim = victim->getID();
			rel.m_victimPos = *victim->getPosition();
			rel.m_lastHit = TheGameLogic->getFrame();
			break;
		}
	}
}

/// The enemy that is nearest to 'around' and in sight of 'unit' (armed things first), or null.
Object *AIProtect::nearestEnemy( const Object *unit, const Coord3D &around, Real radius ) const
{
	PartitionFilterAlive alive;
	PartitionFilterRelationship foes(unit, PartitionFilterRelationship::ALLOW_ENEMIES);
	PartitionFilter *filters[] = { &foes, &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&around, radius, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR);
	MemoryPoolObjectHolder hold(iter);
	Object *fallback = nullptr;
	for (Object *o = iter->first(); o; o = iter->next())
	{
		if (!isSeenBy(unit, o) || o->isKindOf(KINDOF_STRUCTURE) || o->isKindOf(KINDOF_UNATTACKABLE) || o->isKindOf(KINDOF_PROJECTILE) || o->isKindOf(KINDOF_MINE))
			continue;
		const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
		if (f && f->m_armed)
			return o;
		if (fallback == nullptr)
			fallback = o;
	}
	return fallback;
}

//-------------------------------------------------------------------------------------------------
void AIProtect::sendHome( Protector *p )
{
	Object *o = TheGameLogic->findObjectByID(p->m_unit);
	if (o && o->getAI() && !o->isEffectivelyDead())
		o->getAI()->aiMoveToPosition(&p->m_home, CMD_FROM_AI);
	p->m_state = STATE_RETURNING;
	p->m_since = TheGameLogic->getFrame();
	p->m_target = INVALID_ID;
	++m_returns;
}

/**
 * Damage on a protected object: the nearest suitable protectors go for the attacker (or, when it is not in
 * sight, to the place where the object stands), enough of them for what is seen around the object.
 */
void AIProtect::answerAlarm( Relation *rel )
{
	rel->m_alarm = FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *victim = TheGameLogic->findObjectByID(rel->m_victim);
	if (victim == nullptr || victim->isEffectivelyDead())
		return;
	Object *attacker = rel->m_attacker != INVALID_ID ? TheGameLogic->findObjectByID(rel->m_attacker) : nullptr;
	if (attacker && (attacker->isEffectivelyDead() || victim->getRelationship(attacker) != ENEMIES))
		attacker = nullptr;
	const Coord3D spot = rel->m_victimPos;

	// Candidates: protectors of this relation at their posts, near enough, not too far from the place they would be leashed to.
	Int order[MAX_PROTECTORS];
	Real distance[MAX_PROTECTORS];
	Int numCand = 0;
	for (Int i = 0; i < m_numProtectors; ++i)
	{
		Protector &p = m_protectors[i];
		if (p.m_key != rel->m_key || p.m_state != STATE_HOME)
			continue;
		Object *o = TheGameLogic->findObjectByID(p.m_unit);
		if (o == nullptr || o->isEffectivelyDead() || o->isContained() || o->isDisabled() || o->getAI() == nullptr)
			continue;
		const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
		if (f == nullptr || !f->m_armed || f->m_structure || f->m_speed <= 0.0f)
			continue;
		if (attacker && isSeenBy(o, attacker))
		{
			// Suitable: it can hurt the attacker and does not lose the exchange badly.
			const AICombatFigures *af = AICombatModel::figures(attacker->getTemplate());
			if (AICombatModel::damagePerSecond(f, af) <= 0.0f || AICombatModel::matchup(f, af) < -0.5f)
				continue;
		}
		else if (f->m_airborne)
		{
			continue;
		}
		const Real d = dist2D(*o->getPosition(), spot);
		if (d > rel->m_params.m_responseRadius || dist2D(p.m_home, spot) > rel->m_params.m_leashRadius)
			continue;
		Int k = numCand++;
		while (k > 0 && distance[k - 1] > d)
		{
			distance[k] = distance[k - 1];
			order[k] = order[k - 1];
			--k;
		}
		distance[k] = d;
		order[k] = i;
	}
	if (numCand == 0)
		return;

	// How much is needed: the armed value seen around the damaged object (or the attacker alone).
	Real threat = 0.0f;
	{
		PartitionFilterAlive alive;
		PartitionFilterRelationship foes(victim, PartitionFilterRelationship::ALLOW_ENEMIES);
		PartitionFilter *filters[] = { &foes, &alive, nullptr };
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&spot, 260.0f, FROM_CENTER_2D, filters);
		MemoryPoolObjectHolder hold(iter);
		for (Object *o = iter->first(); o; o = iter->next())
		{
			if (o->isKindOf(KINDOF_STRUCTURE) || !isSeenBy(victim, o))
				continue;
			const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
			if (f && f->m_armed)
				threat += f->m_cost;
		}
	}

	Real sent = 0.0f;
	Int count = 0;
	for (Int c = 0; c < numCand && count < rel->m_params.m_maxResponders; ++c)
	{
		Protector &p = m_protectors[order[c]];
		Object *o = TheGameLogic->findObjectByID(p.m_unit);
		if (count > 0 && sent >= 1.3f * threat)
			break;
		AIUpdateInterface *ai = o->getAI();
		if (attacker && isSeenBy(o, attacker))
			ai->aiAttackObject(attacker, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		else
			ai->aiAttackMoveToPosition(&spot, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		p.m_state = STATE_RESPONDING;
		p.m_since = now;
		p.m_target = attacker ? attacker->getID() : INVALID_ID;
		p.m_aim = spot;
		sent += AICombatModel::figures(o->getTemplate())->m_cost;
		++count;
		++m_responses;
	}
	if (count > 0)
		PROTECT_TRACE("alarm at (%.0f,%.0f): %s hit by %s, %d protectors answer (their value %.0f against %.0f seen)", spot.x, spot.y,
			victim->getTemplate()->getName().str(), attacker ? attacker->getTemplate()->getName().str() : "an unseen enemy", count, sent, threat);
}

/// A protector on a response: stay on the fight, take the next enemy, or go home.
void AIProtect::keepResponding( Protector *p, Relation *rel )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *o = TheGameLogic->findObjectByID(p->m_unit);
	AIUpdateInterface *ai = o->getAI();
	const AIProtectParams &par = rel ? rel->m_params : AIProtectParams();

	if (dist2D(*o->getPosition(), p->m_home) > par.m_leashRadius + 60.0f)
	{
		PROTECT_TRACE("%u is beyond its leash: back", p->m_unit);
		sendHome(p);
		return;
	}
	if (now - p->m_since > framesOf(par.m_maxSeconds))
	{
		PROTECT_TRACE("%u has been away long enough: back", p->m_unit);
		sendHome(p);
		return;
	}

	Object *target = p->m_target != INVALID_ID ? TheGameLogic->findObjectByID(p->m_target) : nullptr;
	if (target && (target->isEffectivelyDead() || !isSeenBy(o, target) || o->getRelationship(target) != ENEMIES))
		target = nullptr;
	if (target)
	{
		if (!(ai->isAttacking() && ai->getCurrentVictim() == target))
			ai->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		return;
	}
	// The attacker is dead or gone: anything hostile near the protected place or near the unit?
	Object *next = nearestEnemy(o, p->m_aim, 260.0f);
	if (next == nullptr)
		next = nearestEnemy(o, *o->getPosition(), 200.0f);
	if (next && dist2D(*next->getPosition(), p->m_home) <= par.m_leashRadius + 60.0f)
	{
		p->m_target = next->getID();
		ai->aiAttackObject(next, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		return;
	}
	p->m_target = INVALID_ID;
	const UnsignedInt lastHit = rel ? rel->m_lastHit : 0;
	if (rel == nullptr || now - lastHit > framesOf(par.m_calmSeconds))
		sendHome(p);
	else if (ai->isIdle())
		ai->aiAttackMoveToPosition(&p->m_aim, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
}

//-------------------------------------------------------------------------------------------------
void AIProtect::update()
{
	if (m_numProtectors == 0 && m_numRelations == 0)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();

	for (Int r = 0; r < m_numRelations; ++r)
	{
		if (m_relations[r].m_alarm)
			answerAlarm(&m_relations[r]);
	}

	if (now < m_nextUpdate)
		return;
	m_nextUpdate = now + 10;
	for (Int i = 0; i < m_numProtectors; )
	{
		Protector &p = m_protectors[i];
		Object *o = TheGameLogic->findObjectByID(p.m_unit);
		if (o == nullptr || o->isEffectivelyDead() || o->getAI() == nullptr)
		{
			dropProtector(&p);
			continue;
		}
		if (p.m_state == STATE_RESPONDING)
		{
			keepResponding(&p, findRelation(p.m_key));
		}
		else if (p.m_state == STATE_RETURNING)
		{
			if (dist2D(*o->getPosition(), p.m_home) < 60.0f || now - p.m_since > 20 * LOGICFRAMES_PER_SECOND)
			{
				p.m_state = STATE_HOME;
				if (p.m_key < 0)
				{
					dropProtector(&p);
					continue;
				}
			}
		}
		else if (p.m_key < 0)
		{
			dropProtector(&p);
			continue;
		}
		++i;
	}
}

//-------------------------------------------------------------------------------------------------
void AIProtect::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferInt(&m_numProtectors);
	xfer->xferUser(m_protectors, sizeof(m_protectors));
	xfer->xferInt(&m_numRelations);
	xfer->xferUser(m_relations, sizeof(m_relations));
	xfer->xferUnsignedInt(&m_nextUpdate);
	xfer->xferInt(&m_alarms);
	xfer->xferInt(&m_responses);
	xfer->xferInt(&m_returns);
}

//-------------------------------------------------------------------------------------------------
void AIProtectNotifyDamage( Object *victim, ObjectID attacker, Real amount )
{
	// Damage without a source (fire, falling, an expiring lifetime ...) or from a friend is no attack.
	if (attacker == INVALID_ID)
		return;
	Player *owner = victim->getControllingPlayer();
	if (owner == nullptr)
		return;
	const Object *source = TheGameLogic->findObjectByID(attacker);
	if (source == nullptr || victim->getRelationship(source) != ENEMIES)
		return;
	owner->aiObjectDamaged(victim, attacker, amount);

	// The Expert allies of the owner hear about it too (the game's "our ally is under attack"): allied base support
	// (AIStrategy::onObjectDamaged tells their own objects from an ally's).  The players in the order of the list.
	const Int count = ThePlayerList->getPlayerCount();
	for (Int i = 0; i < count; ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (p == nullptr || p == owner || !p->isExpertAIPlayer())
			continue;
		if (p->getRelationship(victim->getTeam()) != ALLIES || p->getRelationship(source->getTeam()) != ENEMIES)
			continue;
		p->aiObjectDamaged(victim, attacker, amount);
	}
}
