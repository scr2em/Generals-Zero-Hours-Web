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

// AIRaid.cpp
// Economic raids of the Expert computer player: a small party of the fastest units of the army, armed against
// gatherers, goes after enemy supply trucks and workers that the enemy model has seen away from its defences,
// kills what it finds, and comes home when defenders turn up (the same Lanchester weighing as the fight check),
// when it is hurt, or when its time is up.
//
// Everything is derived from the templates (speed, weapons against the target's armour, KindOf of the target)
// and from what the player has seen (AIEnemyModel); no template names.  The party is "detached" from its teams
// while it is out: the team logic leaves it alone (AIStrategy::isDetached).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

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
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

/// Distance from point p to the segment a-b.
static Real distToSegment(const Coord3D &p, const Coord3D &a, const Coord3D &b)
{
	const Real dx = b.x - a.x, dy = b.y - a.y;
	const Real len2 = dx * dx + dy * dy;
	Real t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	const Real cx = a.x + t * dx - p.x, cy = a.y + t * dy - p.y;
	return sqrtf(cx * cx + cy * cy);
}

static inline Real healthShare(Object *obj)
{
	BodyModuleInterface *body = obj->getBodyModule();
	if (body == nullptr || body->getMaxHealth() <= 0.0f)
		return 1.0f;
	const Real h = body->getHealth() / body->getMaxHealth();
	return h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
}

/// A gatherer in the sense of the raid: supply truck, harvester, worker (not a structure).
static inline Bool isGathererFigures(const AICombatFigures *f)
{
	return f != nullptr && !f->m_structure && f->m_role == AIROLE_ECONOMY;
}

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::raidOn() const
{
	return (skill().m_useRaids || m_ai->isFeatureForced(AIPlayer::AIF_RAID)) && !m_ai->isFeatureOff(AIPlayer::AIF_RAID);
}

/// The party that is out is not part of its teams.
Bool AIStrategy::isDetached( ObjectID id ) const
{
	if (id == INVALID_ID)
		return FALSE;
	for (Int i = 0; i < m_numRaiders; ++i)
	{
		if (m_raiders[i] == id)
			return TRUE;
	}
	return FALSE;
}

void AIStrategy::dropDeadRaiders()
{
	Int out = 0;
	for (Int i = 0; i < m_numRaiders; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_raiders[i]);
		if (o == nullptr || o->isEffectivelyDead() || o->getControllingPlayer() != m_player)
		{
			++m_raidLosses;
			continue;
		}
		m_raiders[out++] = m_raiders[i];
	}
	m_numRaiders = out;
}

/**
 * Is it safe to go for a target at 'pos'?  No known defence that shoots ground units covers it, and the armed
 * units that were seen around it (or, with 'from', on the way there) in the last seconds are worth less than the
 * guard share of the party.
 */
Bool AIStrategy::raidTargetSafe( const Coord3D &pos, Real partyValue, Real *guard, const Coord3D *from ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Real guardValue = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_DEFENCE)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || !f->m_canHitGround)
			continue;
		const Real d = dist2D(c.m_pos, pos);
		if (f->m_structure)
		{
			if (d < f->m_range + 50.0f)
				return FALSE;
		}
		else if (now - c.m_lastSeen <= 12 * LOGICFRAMES_PER_SECOND && (d < 350.0f || (from && distToSegment(c.m_pos, *from, pos) < 200.0f)))
		{
			guardValue += f->m_cost;		// around the target, or on the way there
		}
	}
	if (guard)
		*guard = guardValue;
	return guardValue <= skill().m_raidGuardShare * partyValue;
}

/**
 * The best gatherer to hit from 'from'.  With liveObjects the world around the party is searched (what the
 * units see right now), else the enemy model's memory (the position is where it was seen, not where it is).
 */
Bool AIStrategy::findRaidTarget( const Coord3D &from, const Object *raider, Real partyValue, Real radius, Bool liveObjects, ObjectID except, ObjectID *id, Coord3D *pos ) const
{
	const AICombatFigures *rf = AICombatModel::figures(raider->getTemplate());
	const UnsignedInt now = TheGameLogic->getFrame();
	Bool found = FALSE;
	Real bestScore = 0.0f;

	if (liveObjects)
	{
		PartitionFilterAlive alive;
		PartitionFilterRelationship foes(raider, PartitionFilterRelationship::ALLOW_ENEMIES);
		PartitionFilter *filters[] = { &foes, &alive, nullptr };
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&from, radius, FROM_CENTER_2D, filters);
		MemoryPoolObjectHolder hold(iter);
		for (Object *o = iter->first(); o; o = iter->next())
		{
			const AICombatFigures *tf = AICombatModel::figures(o->getTemplate());
			if (!isGathererFigures(tf) || !enemyCanSee(o) || o->isContained() || o->getID() == except)
				continue;
			if (AICombatModel::damagePerSecond(rf, tf) <= 0.0f)
				continue;
			if (!raidTargetSafe(*o->getPosition(), partyValue, nullptr, &from))
				continue;
			const Real score = tf->m_cost / (1.0f + dist2D(from, *o->getPosition()) / 300.0f);
			if (!found || score > bestScore)
			{
				found = TRUE;
				bestScore = score;
				*id = o->getID();
				*pos = *o->getPosition();
			}
		}
		return found;
	}

	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role != AIROLE_ECONOMY || now - c.m_lastSeen > 25 * LOGICFRAMES_PER_SECOND)
			continue;
		if (c.m_id == except)
			continue;
		const AICombatFigures *tf = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (!isGathererFigures(tf) || AICombatModel::damagePerSecond(rf, tf) <= 0.0f)
			continue;
		const Real d = dist2D(from, c.m_pos);
		if (d > radius)
			continue;
		// A contact whose place we can see and where it is not (any more) is no target.
		const Object *known = TheGameLogic->findObjectByID(c.m_id);
		if ((known == nullptr || known->isEffectivelyDead()) &&
				ThePartitionManager->getShroudStatusForPlayer(m_player->getPlayerIndex(), &c.m_pos) == CELLSHROUD_CLEAR)
			continue;
		if (!raidTargetSafe(c.m_pos, partyValue, nullptr, &from))
			continue;
		const Real score = tf->m_cost / (1.0f + d / 800.0f);
		if (!found || score > bestScore)
		{
			found = TRUE;
			bestScore = score;
			*id = c.m_id;
			*pos = c.m_pos;
		}
	}
	return found;
}

/**
 * The fastest armed units of the field teams that the party may take: clearly faster than the army's average,
 * healthy, able to hurt the target (when given), and not committed to a wave, a retreat or the defence of the base.
 */
Int AIStrategy::collectRaiders( Object **out, Int maxCount, const Object *forTarget )
{
	const AISkillSettings &sk = skill();
	const AICombatFigures *tf = forTarget ? AICombatModel::figures(forTarget->getTemplate()) : nullptr;

	// The army's average speed (all armed mobile units of the field teams).
	Real speedSum = 0.0f;
	Int units = 0;
	for (Int i = 0; i < m_numTeams; ++i)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead())
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || f->m_structure || f->m_speed <= 0.0f)
				continue;
			speedSum += f->m_speed;
			++units;
		}
	}
	if (units < 3)
		return 0;
	const Real threshold = sk.m_raidSpeedFactor * speedSum / units;

	Int count = 0;
	Real speeds[MAX_RAIDERS];
	for (Int i = 0; i < m_numTeams; ++i)
	{
		const AITeamRecord &rec = m_teams[i];
		if (rec.m_mode != AITEAM_FREE && rec.m_mode != AITEAM_REGROUPING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr)
				continue;
			if (isDetached(obj->getID()))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || f->m_structure || f->m_airborne || f->m_speed < threshold)
				continue;
			if (healthShare(obj) < 0.7f)
				continue;
			if (tf && AICombatModel::damagePerSecond(f, tf) <= 0.0f)
				continue;

			// Insert by speed (fastest first); the first of equal units wins, which keeps the choice stable.
			Int pos = count;
			while (pos > 0 && speeds[pos - 1] < f->m_speed)
				--pos;
			if (pos >= maxCount)
				continue;
			const Int last = count < maxCount ? count : maxCount - 1;
			for (Int k = last; k > pos; --k)
			{
				out[k] = out[k - 1];
				speeds[k] = speeds[k - 1];
			}
			out[pos] = obj;
			speeds[pos] = f->m_speed;
			if (count < maxCount)
				++count;
		}
	}
	return count;
}

void AIStrategy::orderRaiders( Object *target, const Coord3D *aim )
{
	for (Int i = 0; i < m_numRaiders; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_raiders[i]);
		if (o == nullptr || o->getAI() == nullptr)
			continue;
		AIUpdateInterface *ai = o->getAI();
		if (target)
		{
			if (!(ai->isAttacking() && ai->getCurrentVictim() == target))
				ai->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
		}
		else if (aim)
		{
			ai->aiMoveToPosition(aim, CMD_FROM_AI);
		}
	}
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::tryStartRaid()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	if (now < secondsToFrames(sk.m_raidStartSeconds) || now < m_raidCooldown)
		return;
	if (m_threatSince != 0 || m_player->getAttackedFrame() + 10 * LOGICFRAMES_PER_SECOND > now)
		return;		// the base is under fire: the units stay
	if (m_enemy.roleCount(AIROLE_ECONOMY) == 0)
		return;

	Object *party[MAX_RAIDERS];
	const Int maxUnits = sk.m_raidUnits < MAX_RAIDERS ? (sk.m_raidUnits > 0 ? sk.m_raidUnits : 1) : MAX_RAIDERS;
	Int n = collectRaiders(party, maxUnits, nullptr);
	if (n == 0)
		return;

	Coord3D center;
	center.zero();
	Real value = 0.0f;
	for (Int i = 0; i < n; ++i)
	{
		center.x += party[i]->getPosition()->x;
		center.y += party[i]->getPosition()->y;
		value += AICombatModel::figures(party[i]->getTemplate())->m_cost * healthShare(party[i]);
	}
	center.x /= n;
	center.y /= n;
	center.z = 0.0f;

	ObjectID targetID = INVALID_ID;
	Coord3D targetPos;
	if (!findRaidTarget(center, party[0], value, 2500.0f, FALSE, INVALID_ID, &targetID, &targetPos))
	{
		if (m_trace && now >= m_raidNoteFrame)
		{
			m_raidNoteFrame = now + 30 * LOGICFRAMES_PER_SECOND;
			AI_TRACE("raid party of %d (value %.0f) ready, but none of the %d known gatherers is safe to hit", n, value, m_enemy.roleCount(AIROLE_ECONOMY));
		}
		return;
	}
	const AIContact *contact = nullptr;
	for (Int i = 0; i < m_enemy.numContacts() && contact == nullptr; ++i)
		if (m_enemy.contacts()[i].m_id == targetID)
			contact = &m_enemy.contacts()[i];
	if (contact == nullptr)
		return;
	const ThingTemplate *targetTemplate = TheThingFactory->findByTemplateID(contact->m_templateID);

	// Only the units that can hurt this target go.
	Object *chosen[MAX_RAIDERS];
	n = collectRaiders(chosen, maxUnits, nullptr);
	{
		const AICombatFigures *tf = AICombatModel::figures(targetTemplate);
		Int k = 0;
		for (Int i = 0; i < n; ++i)
			if (AICombatModel::damagePerSecond(AICombatModel::figures(chosen[i]->getTemplate()), tf) > 0.0f)
				chosen[k++] = chosen[i];
		n = k;
	}
	if (n == 0)
		return;

	m_numRaiders = n;
	m_raidPartyValue = 0.0f;
	for (Int i = 0; i < n; ++i)
	{
		m_raiders[i] = chosen[i]->getID();
		m_raidPartyValue += AICombatModel::figures(chosen[i]->getTemplate())->m_cost * healthShare(chosen[i]);
	}
	m_raidTarget = targetID;
	m_raidAim = targetPos;
	m_raidPhase = 1;
	m_raidStart = now;
	m_raidPhaseFrame = now;
	m_raidBadSince = 0;
	++m_raidsLaunched;
	AI_TRACE("RAID %d launches: %d units (value %.0f) -> %s %u at (%.0f,%.0f)", m_raidsLaunched, n, m_raidPartyValue,
		targetTemplate->getName().str(), targetID, m_raidAim.x, m_raidAim.y);
	orderRaiders(nullptr, &m_raidAim);
}

/// The party is out: weigh the situation, keep it on its target, take the next one.
void AIStrategy::updateRaidOut()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	dropDeadRaiders();
	if (m_numRaiders == 0)
	{
		AI_TRACE("RAID over: the party is lost");
		endRaid();
		return;
	}

	Coord3D center;
	center.zero();
	Real health = 0.0f;
	for (Int i = 0; i < m_numRaiders; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_raiders[i]);
		center.x += o->getPosition()->x;
		center.y += o->getPosition()->y;
		health += healthShare(o);
	}
	center.x /= m_numRaiders;
	center.y /= m_numRaiders;
	center.z = 0.0f;
	health /= m_numRaiders;
	Object *lead = TheGameLogic->findObjectByID(m_raiders[0]);

	if (now - m_raidStart >= secondsToFrames(sk.m_raidMaxSeconds))
	{
		sendRaidersHome("time is up");
		return;
	}
	if (health < 0.45f)
	{
		++m_raidPullbacks;
		sendRaidersHome("the party is hurt");
		return;
	}
	if (m_ai->isFeatureOff(AIPlayer::AIF_FIGHT) == FALSE)
	{
		// The same weighing as the fight check: our units around against what shoots at them.
		Real ours = 0.0f, theirs = 0.0f;
		const Real advantage = fightAdvantage(&center, 220.0f, &ours, &theirs);
		if (theirs > 0.0f && advantage < sk.m_raidPullbackAdvantage)
		{
			if (m_raidBadSince == 0)
				m_raidBadSince = now;
			if (now - m_raidBadSince >= LOGICFRAMES_PER_SECOND / 2)
			{
				++m_raidPullbacks;
				char why[64];
				snprintf(why, sizeof(why), "defenders: advantage %.2f (ours %.0f, theirs %.0f)", advantage, ours, theirs);
				sendRaidersHome(why);
				return;
			}
		}
		else
		{
			m_raidBadSince = 0;
		}
	}

	// The target.  Its position is only known while a unit sees it; out of sight the party goes to where it was.
	Object *target = m_raidTarget != INVALID_ID ? TheGameLogic->findObjectByID(m_raidTarget) : nullptr;
	const Bool exists = target != nullptr && !target->isEffectivelyDead() && target->getControllingPlayer() != m_player;
	const Bool seen = exists && enemyCanSee(target);
	Bool finished = FALSE;
	if (seen)
	{
		m_raidAim = *target->getPosition();
		m_raidPhaseFrame = now;
	}
	else
	{
		const Bool aimVisible = ThePartitionManager->getShroudStatusForPlayer(m_player->getPlayerIndex(), &m_raidAim) == CELLSHROUD_CLEAR;
		if (aimVisible || dist2D(center, m_raidAim) < 60.0f || now - m_raidPhaseFrame > 20 * LOGICFRAMES_PER_SECOND)
		{
			finished = TRUE;
			if (!exists && aimVisible)
			{
				++m_raidKills;
				AI_TRACE("RAID: target %u is gone", m_raidTarget);
			}
		}
		else
		{
			orderRaiders(nullptr, &m_raidAim);
			return;
		}
	}
	if (finished)
	{
		const ObjectID old = m_raidTarget;
		m_raidTarget = INVALID_ID;
		ObjectID nextID = INVALID_ID;
		Coord3D nextPos;
		if (!findRaidTarget(center, lead, m_raidPartyValue, 380.0f, TRUE, old, &nextID, &nextPos) &&
				!findRaidTarget(center, lead, m_raidPartyValue, 800.0f, FALSE, old, &nextID, &nextPos))
		{
			sendRaidersHome("no gatherer in reach");
			return;
		}
		m_raidTarget = nextID;
		m_raidAim = nextPos;
		m_raidPhaseFrame = now;
		target = TheGameLogic->findObjectByID(nextID);
		AI_TRACE("RAID retargets: %u at (%.0f,%.0f)", nextID, nextPos.x, nextPos.y);
	}
	orderRaiders((target && enemyCanSee(target)) ? target : nullptr, &m_raidAim);
}

void AIStrategy::sendRaidersHome( const char *why )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Coord3D rally;
	if (!rallyPoint(&rally))
	{
		endRaid();
		return;
	}
	AI_TRACE("RAID %d comes home: %s (kills %d)", m_raidsLaunched, why, m_raidKills);
	m_raidPhase = 2;
	m_raidPhaseFrame = now;
	m_raidTarget = INVALID_ID;
	for (Int i = 0; i < m_numRaiders; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_raiders[i]);
		if (o && o->getAI())
			o->getAI()->aiMoveToPosition(&rally, CMD_FROM_AI);
	}
}

void AIStrategy::endRaid()
{
	m_numRaiders = 0;
	m_raidPhase = 0;
	m_raidTarget = INVALID_ID;
	m_raidCooldown = TheGameLogic->getFrame() + secondsToFrames(skill().m_raidCooldownSeconds);
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::updateRaid()
{
	if (!raidOn())
	{
		if (m_raidPhase != 0)
			endRaid();
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextRaidCheck)
		return;
	m_nextRaidCheck = now + LOGICFRAMES_PER_SECOND / 2;

	switch (m_raidPhase)
	{
		case 0:
			tryStartRaid();
			break;
		case 1:
			updateRaidOut();
			break;
		case 2:
		{
			dropDeadRaiders();
			Coord3D rally;
			Bool home = m_numRaiders == 0 || !rallyPoint(&rally);
			if (!home)
			{
				home = TRUE;
				for (Int i = 0; i < m_numRaiders && home; ++i)
				{
					Object *o = TheGameLogic->findObjectByID(m_raiders[i]);
					if (o && dist2D(*o->getPosition(), rally) > 250.0f)
						home = FALSE;
				}
			}
			if (home || now - m_raidPhaseFrame > 25 * LOGICFRAMES_PER_SECOND)
				endRaid();
			break;
		}
		default:
			endRaid();
			break;
	}
}
