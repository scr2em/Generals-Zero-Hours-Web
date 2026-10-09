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

// AIGarrison.cpp
// Garrisons for the Expert computer player.
//
//  * Defence: when armed enemies are seen in the base, the infantry at home enter the garrisonable structures of
//    the base (any structure whose contain module says it can be garrisoned) that face the attack, shoot from
//    inside, and leave again when the attackers are gone.
//  * Attack: a structure of the enemy that is garrisoned (an enemy contact in sight whose contain module is a
//    garrison with occupants) does not shoot back at units that ignore it, so the units with the best damage against
//    structures (AICombatModel) are told to bring it down.
//
// Whether a unit may enter a structure is asked of the action manager.  Templates and what is in sight only.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static inline Real healthShare(Object *obj)
{
	BodyModuleInterface *body = obj->getBodyModule();
	if (body == nullptr || body->getMaxHealth() <= 0.0f)
		return 1.0f;
	const Real h = body->getHealth() / body->getMaxHealth();
	return h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
}

enum { GAR_ENTERING = 0, GAR_INSIDE = 1, GAR_LEAVING = 2 };

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::garrisonOn() const
{
	return (skill().m_useGarrison || m_ai->isFeatureForced(AIPlayer::AIF_GARRISON)) && !m_ai->isFeatureOff(AIPlayer::AIF_GARRISON);
}

Bool AIStrategy::isGarrisoned( ObjectID id ) const
{
	for (Int i = 0; i < m_numGarrisoned; ++i)
		if (m_garrisoned[i].m_unit == id)
			return TRUE;
	for (Int i = 0; i < m_numCleaners; ++i)
		if (m_cleaners[i] == id)
			return TRUE;
	return FALSE;
}

/// Enemy armed ground units seen in the base in the last seconds: their value and the place of the biggest.
Bool AIStrategy::baseUnderAttack( Coord3D *where, Real *value, Real *range ) const
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real zone = m_ai->m_baseRadius + 300.0f;
	Real threat = 0.0f, best = 0.0f, reach = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 3 * LOGICFRAMES_PER_SECOND)
			continue;
		if (dist2D(c.m_pos, base) > zone)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || !f->m_canHitGround)
			continue;
		threat += f->m_cost;
		if (f->m_range > reach)
			reach = f->m_range;
		if (f->m_cost > best)
		{
			best = f->m_cost;
			*where = c.m_pos;
		}
	}
	*value = threat;
	*range = reach;
	return threat >= skill().m_garrisonThreatValue;
}

void AIStrategy::dropGarrisoned( Int index )
{
	m_garrisoned[index] = m_garrisoned[--m_numGarrisoned];
}

/// Garrison defence: send the infantry at home into the structures that face the attackers; take them out when it is over.
void AIStrategy::updateGarrisonDefence()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();

	Coord3D where;
	where.zero();
	Real threat = 0.0f, attackerRange = 0.0f;
	const Bool attacked = baseUnderAttack(&where, &threat, &attackerRange);
	if (attacked)
		m_garrisonLastThreat = now;
	const Bool holding = m_garrisonLastThreat != 0 && now - m_garrisonLastThreat <= secondsToFrames(sk.m_garrisonHoldSeconds);

	// The units that are in (or on their way to) a structure.
	for (Int i = 0; i < m_numGarrisoned; )
	{
		AIGarrisonRecord &g = m_garrisoned[i];
		Object *unit = TheGameLogic->findObjectByID(g.m_unit);
		Object *building = TheGameLogic->findObjectByID(g.m_building);
		if (unit == nullptr || unit->isEffectivelyDead() || unit->getAI() == nullptr)
		{
			dropGarrisoned(i);
			continue;
		}
		const Bool inside = unit->isContained() && unit->getContainedBy() == building;
		if (g.m_phase == GAR_ENTERING)
		{
			if (inside)
			{
				g.m_phase = GAR_INSIDE;
				++m_garrisonEntered;
			}
			else if (building == nullptr || building->isEffectivelyDead() || now - g.m_since > 20 * LOGICFRAMES_PER_SECOND || !holding)
			{
				// Could not get in (or not needed any more).
				unit->getAI()->aiIdle(CMD_FROM_AI);
				dropGarrisoned(i);
				continue;
			}
		}
		else if (g.m_phase == GAR_INSIDE)
		{
			if (!inside)
			{
				dropGarrisoned(i);		// thrown out, or the building is gone
				continue;
			}
			++m_garrisonSeconds;
			if (unit->getLastShotFiredFrame() + 3 * LOGICFRAMES_PER_SECOND >= now)
				++m_garrisonFiring;
			if (!holding)
			{
				unit->getAI()->aiExit(building, CMD_FROM_AI);
				g.m_phase = GAR_LEAVING;
				g.m_since = now;
			}
		}
		else
		{
			if (!unit->isContained() || now - g.m_since > 6 * LOGICFRAMES_PER_SECOND)
			{
				sendPatientBack(unit);
				dropGarrisoned(i);
				continue;
			}
		}
		++i;
	}

	if (!attacked || m_numGarrisoned >= MAX_GARRISONED)
		return;

	// Structures of ours that can be garrisoned, nearest to the attackers first.
	Coord3D base;
	m_ai->getBaseCenter(&base);
	struct Site
	{
		Object *obj;
		Int free;
		Real d;
	};
	Site sites[8];
	Int numSites = 0;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_STRUCTURE))
					continue;
				if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD) || obj->isDisabled())
					continue;
				ContainModuleInterface *contain = obj->getContain();
				if (contain == nullptr || !contain->isGarrisonable() || healthShare(obj) < 0.25f)
					continue;
				const Int max = contain->getContainMax();
				Int free = (max > 0 ? max : 0) - (Int)contain->getContainCount();
				for (Int g = 0; g < m_numGarrisoned; ++g)
					if (m_garrisoned[g].m_building == obj->getID() && m_garrisoned[g].m_phase == GAR_ENTERING)
						--free;
				if (free <= 0)
					continue;
				const Real d = dist2D(*obj->getPosition(), where);
				if (d > 450.0f)
					continue;		// does not face the attackers
				// Insert by distance.
				Int pos = numSites;
				while (pos > 0 && sites[pos - 1].d > d)
					--pos;
				if (pos >= 8)
					continue;
				const Int last = numSites < 8 ? numSites : 7;
				for (Int k = last; k > pos; --k)
					sites[k] = sites[k - 1];
				sites[pos].obj = obj;
				sites[pos].free = free;
				sites[pos].d = d;
				if (numSites < 8)
					++numSites;
			}
		}
	}
	if (numSites == 0)
		return;

	// The infantry at home, near the base, that are not on a task: the nearest to each structure goes in.
	Int sent = 0;
	for (Int s = 0; s < numSites && m_numGarrisoned < MAX_GARRISONED; ++s)
	{
		for (Int slot = 0; slot < sites[s].free && m_numGarrisoned < MAX_GARRISONED; ++slot)
		{
			Object *pick = nullptr;
			Real pickD = 0.0f;
			for (Int i = 0; i < m_numTeams; ++i)
			{
				const AITeamRecord &rec = m_teams[i];
				if (rec.m_mode == AITEAM_ATTACKING || rec.m_mode == AITEAM_RETREATING)
					continue;
				Team *team = TheTeamFactory->findTeamByID(rec.m_team);
				if (team == nullptr)
					continue;
				for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
				{
					Object *obj = it.cur();
					if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr || isDetached(obj->getID()))
						continue;
					if (!obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_NO_GARRISON))
						continue;
					const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
					if (f == nullptr || !f->m_armed || !f->m_canHitGround || healthShare(obj) < 0.3f)
						continue;
					// A garrison is for troops that can answer: attackers that out-range the infantry by far would only shoot the
					// structure from where the garrison cannot reach them.
					if (f->m_range * 1.3f < attackerRange)
						continue;
					if (dist2D(*obj->getPosition(), base) > m_ai->m_baseRadius + 450.0f)
						continue;
					if (!TheActionManager->canEnterObject(obj, sites[s].obj, CMD_FROM_AI, CHECK_CAPACITY))
						continue;
					const Real d = dist2D(*obj->getPosition(), *sites[s].obj->getPosition());
					if (pick == nullptr || d < pickD)
					{
						pick = obj;
						pickD = d;
					}
				}
			}
			if (pick == nullptr)
				break;
			AIGarrisonRecord &g = m_garrisoned[m_numGarrisoned++];
			g.m_unit = pick->getID();
			g.m_building = sites[s].obj->getID();
			g.m_since = now;
			g.m_phase = GAR_ENTERING;
			pick->getAI()->aiEnter(sites[s].obj, CMD_FROM_AI);
			++sent;
		}
	}
	if (sent > 0)
		AI_TRACE("GARRISON: base under attack (%.0f at (%.0f,%.0f)): %d infantry enter %d structure(s)", threat, where.x, where.y, sent, numSites);
}

/// Garrison attack: bring down a visible enemy structure with occupants.
void AIStrategy::updateGarrisonClearing()
{
	const UnsignedInt now = TheGameLogic->getFrame();

	// A job that is running.
	if (m_clearTarget != INVALID_ID)
	{
		Object *target = TheGameLogic->findObjectByID(m_clearTarget);
		Int alive = 0;
		for (Int i = 0; i < m_numCleaners; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_cleaners[i]);
			if (o && !o->isEffectivelyDead())
				m_cleaners[alive++] = m_cleaners[i];
		}
		m_numCleaners = alive;
		const char *why = nullptr;
		if (target == nullptr || target->isEffectivelyDead())
			why = "the structure is down";
		else if (enemyCanSee(target) && (target->getContain() == nullptr || target->getContain()->getContainCount() == 0))
			why = "it is empty";
		else if (m_numCleaners == 0)
			why = "the cleaners are lost";
		else if (now - m_clearStart > 45 * LOGICFRAMES_PER_SECOND)
			why = "taking too long";
		if (why)
		{
			AI_TRACE("CLEAR over: %s", why);
			if (target == nullptr || target->isEffectivelyDead())
				++m_garrisonClears;
			for (Int i = 0; i < m_numCleaners; ++i)
			{
				Object *o = TheGameLogic->findObjectByID(m_cleaners[i]);
				const AITeamRecord *rec = (o && o->getTeam()) ? findRecord(o->getTeam()->getID(), FALSE) : nullptr;
				if (o && o->getAI() && rec && (rec->m_mode == AITEAM_ATTACKING || rec->m_mode == AITEAM_DEFENDING))
				{
					const Coord3D t = rec->m_target;
					o->getAI()->aiAttackMoveToPosition(&t, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
				}
			}
			m_numCleaners = 0;
			m_clearTarget = INVALID_ID;
			m_nextClearSearch = now + 10 * LOGICFRAMES_PER_SECOND;
			return;
		}
		for (Int i = 0; i < m_numCleaners; ++i)
		{
			Object *o = TheGameLogic->findObjectByID(m_cleaners[i]);
			if (o && o->getAI() && !(o->getAI()->isAttacking() && o->getAI()->getCurrentVictim() == target))
				o->getAI()->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}
		return;
	}

	if (now < m_nextClearSearch)
		return;
	m_nextClearSearch = now + 2 * LOGICFRAMES_PER_SECOND;

	// An occupied structure of the enemy that we can see now, near one of our field teams.
	Object *target = nullptr;
	Real targetD = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role < AIROLE_DEFENCE || now - c.m_lastSeen > 2 * LOGICFRAMES_PER_SECOND)
			continue;
		Object *o = TheGameLogic->findObjectByID(c.m_id);
		if (o == nullptr || !enemyCanSee(o))
			continue;
		ContainModuleInterface *contain = o->getContain();
		if (contain == nullptr || !contain->isGarrisonable() || contain->getContainCount() == 0)
			continue;
		for (Int t = 0; t < m_numTeams; ++t)
		{
			if (m_teams[t].m_mode == AITEAM_RETREATING)
				continue;
			Team *team = TheTeamFactory->findTeamByID(m_teams[t].m_team);
			const Coord3D *p = team ? team->getEstimateTeamPosition() : nullptr;
			if (p == nullptr)
				continue;
			const Real d = dist2D(*p, c.m_pos);
			if (d < 500.0f && (target == nullptr || d < targetD))
			{
				target = o;
				targetD = d;
			}
		}
	}
	if (target == nullptr)
		return;
	// Not while the troops around it are stronger than ours: units sent against the building alone would only be lost.
	{
		Real ours = 0.0f, theirs = 0.0f;
		const Real advantage = fightAdvantage(target->getPosition(), 350.0f, &ours, &theirs);
		if (theirs > 0.0f && advantage < 1.0f)
			return;
	}

	// The cleaners: the units that hurt this structure most (damage against its armour), nearest first among equals.
	const AICombatFigures *tf = AICombatModel::figures(target->getTemplate());
	Object *best[MAX_CLEANERS];
	Real score[MAX_CLEANERS];
	Int n = 0;
	for (Int t = 0; t < m_numTeams; ++t)
	{
		if (m_teams[t].m_mode == AITEAM_RETREATING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(m_teams[t].m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->getAI() == nullptr || obj->isDisabled() || isDetached(obj->getID()))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || f->m_structure || f->m_airborne || f->m_speed <= 0.0f)
				continue;
			if (dist2D(*obj->getPosition(), *target->getPosition()) > 600.0f)
				continue;
			const Real dps = AICombatModel::damagePerSecond(f, tf);
			if (dps <= 0.0f)
				continue;
			Int pos = n;
			while (pos > 0 && score[pos - 1] < dps)
				--pos;
			if (pos >= MAX_CLEANERS)
				continue;
			const Int last = n < MAX_CLEANERS ? n : MAX_CLEANERS - 1;
			for (Int k = last; k > pos; --k)
			{
				best[k] = best[k - 1];
				score[k] = score[k - 1];
			}
			best[pos] = obj;
			score[pos] = dps;
			if (n < MAX_CLEANERS)
				++n;
		}
	}
	if (n == 0)
		return;
	m_clearTarget = target->getID();
	m_clearStart = now;
	m_numCleaners = n;
	for (Int i = 0; i < n; ++i)
		m_cleaners[i] = best[i]->getID();
	++m_garrisonClearJobs;
	AI_TRACE("CLEAR: %s %u holds %u occupants; %d unit(s) with the best damage against it (%.0f/s the best) go for it", target->getTemplate()->getName().str(), target->getID(),
		target->getContain()->getContainCount(), n, score[0]);
	for (Int i = 0; i < n; ++i)
		best[i]->getAI()->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::updateGarrison()
{
	if (!garrisonOn())
	{
		m_numGarrisoned = 0;
		m_numCleaners = 0;
		m_clearTarget = INVALID_ID;
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextGarrison)
		return;
	m_nextGarrison = now + LOGICFRAMES_PER_SECOND;
	updateGarrisonDefence();
	if (skill().m_garrisonClear && !m_ai->isFeatureOff(AIPlayer::AIF_CLEAR))
		updateGarrisonClearing();
}
