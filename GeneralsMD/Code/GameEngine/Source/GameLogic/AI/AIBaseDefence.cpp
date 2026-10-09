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

// AIBaseDefence.cpp
// Base defence has priority over every other state of the Expert computer player.
//
// A threat to the base is an armed enemy force that has been seen inside the base zone (the base radius plus a margin),
// or an enemy that has just damaged one of our objects inside it.  While there is one, the teams that are at home
// (gathering at the rally point, waiting for the next wave, held by the fight check, merged reinforcements, a wave that has
// not got far) go for the threat; nothing sends them back to the rally point or launches the next wave until the base has
// been clear for a few seconds.  A wave that is deep in enemy territory only turns round when the threat outweighs what
// is left at home (the combat model, fightAdvantage).
//
// Only what the player has seen (the enemy model) and the damage reports of its own objects are used.

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
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"


// Decisions are printed when the test bench gives the player the variant "trace".
#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::baseDefenceOn() const
{
	return skill().m_useBaseDefence && !m_ai->isFeatureOff(AIPlayer::AIF_BASEDEF);
}

/// One of our objects took damage: when it stands inside the base zone and an enemy did it, that is a threat (reported by the body module).
void AIStrategy::noteBaseDamage( Object *victim, ObjectID attacker, Real amount )
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base) || amount <= 0.0f)
		return;
	const Real zone = m_ai->m_baseRadius + skill().m_baseDefenceMargin;
	if (dist2D(*victim->getPosition(), base) > zone)
		return;
	Object *source = TheGameLogic->findObjectByID(attacker);
	m_bdDamageFrame = TheGameLogic->getFrame();
	// Where it comes from, when we can see it; else the place that was hit.
	m_bdDamagePos = (source && enemyCanSee(source)) ? *source->getPosition() : *victim->getPosition();
}

/// Armed enemy units seen in the base zone in the last seconds (or the place of the latest damage): their value and where they are.
Bool AIStrategy::findBaseThreat( Coord3D *where, Real *value ) const
{
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return FALSE;
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	const Real zone = m_ai->m_baseRadius + sk.m_baseDefenceMargin;
	Real threat = 0.0f, best = 0.0f;
	Coord3D at;
	at.zero();
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_AIRCRAFT || now - c.m_lastSeen > 3 * LOGICFRAMES_PER_SECOND)
			continue;
		if (dist2D(c.m_pos, base) > zone)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed)
			continue;
		threat += f->m_cost;
		if (f->m_cost > best)
		{
			best = f->m_cost;
			at = c.m_pos;
		}
	}
	const Bool damaged = m_bdDamageFrame != 0 && now - m_bdDamageFrame <= 4 * LOGICFRAMES_PER_SECOND;
	if (threat < sk.m_baseDefenceMinValue && !damaged)
		return FALSE;
	if (threat < sk.m_baseDefenceMinValue)
	{
		at = m_bdDamagePos;		// hit by something we do not see: go and look
		threat = sk.m_baseDefenceMinValue;
	}
	*where = at;
	*value = threat;
	return TRUE;
}

/// Sends a team to the threat.
void AIStrategy::sendTeamToBase( Team *team, AITeamRecord &rec, const Coord3D &where )
{
	const UnsignedInt now = TheGameLogic->getFrame();
	rec.m_mode = AITEAM_DEFENDING;
	rec.m_modeFrame = now;
	rec.m_orderFrame = now;
	rec.m_target = where;
	rec.m_idleSince = 0;
	rec.m_badSince = 0;
	rec.m_baseDefence = TRUE;
	orderTeamAttackMove(team, &where);
}

void AIStrategy::updateBaseDefence()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextBaseDefence)
		return;
	m_nextBaseDefence = now + LOGICFRAMES_PER_SECOND / 2;

	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return;
	const AISkillSettings &sk = skill();
	const Bool respond = baseDefenceOn();
	Coord3D where;
	Real value = 0.0f;
	const Bool threat = findBaseThreat(&where, &value);

	if (!threat)
	{
		if (m_bdActive && now - m_bdLastThreat >= secondsToFrames(sk.m_baseDefenceClearSeconds))
		{
			m_bdActive = FALSE;
			Int released = 0;
			for (Int i = 0; i < m_numTeams; ++i)
			{
				AITeamRecord &rec = m_teams[i];
				if (!rec.m_baseDefence)
					continue;
				rec.m_baseDefence = FALSE;
				if (rec.m_mode == AITEAM_DEFENDING)
				{
					rec.m_mode = AITEAM_FREE;		// back to the rally point, or to the next wave
					rec.m_modeFrame = now;
					rec.m_orderFrame = 0;
					++released;
				}
			}
			AI_TRACE("BASEDEF clear after %u s: %d team(s) go back to their role", (now - m_bdSince) / LOGICFRAMES_PER_SECOND, released);
		}
		return;
	}

	m_bdLastThreat = now;
	m_bdPos = where;
	m_bdValue = value;
	if (!m_bdActive)
	{
		m_bdActive = TRUE;
		m_bdSince = now;
		++m_bdAlarms;
		// What stands idle at the rally point at this moment (the trace of the problem this feature solves).
		Coord3D rally;
		Int idle = 0, units = 0;
		if (rallyPoint(&rally))
		{
			for (Int i = 0; i < m_numTeams; ++i)
			{
				Team *team = TheTeamFactory->findTeamByID(m_teams[i].m_team);
				if (team == nullptr)
					continue;
				for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
				{
					Object *obj = it.cur();
					if (obj == nullptr || obj->isEffectivelyDead() || obj->getAI() == nullptr || dist2D(*obj->getPosition(), rally) > 350.0f)
						continue;
					++units;
					if (obj->getAI()->isIdle())
						++idle;
				}
			}
		}
		m_bdIdleAtAlarm += idle;
		AI_TRACE("BASEDEF alarm %d: enemy force %.0f at (%.0f,%.0f) in the base zone; %d of %d units near the rally point stand idle; army state %s; response %s",
			m_bdAlarms, value, where.x, where.y, idle, units, m_armyState == ARMY_GATHER ? "gather" : "wave out", respond ? "on" : "OFF");
	}
	if (!respond)
		return;

	// Local response: the teams at home go together (an enemy that is fought team by team wins), unless the force at home is
	// much weaker than what was seen and nothing of ours is being hit: then it stays at the rally point as one body.
	enum { MAX_ORDER = 24 };
	Int order[MAX_ORDER];
	Int count = 0;
	Real homeValue = 0.0f;
	for (Int i = 0; i < m_numTeams && count < MAX_ORDER; ++i)
	{
		AITeamRecord &rec = m_teams[i];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr || !isManageableTeam(team) || rec.m_mode == AITEAM_RETREATING)
			continue;
		const Coord3D *p = team->getEstimateTeamPosition();
		if (p == nullptr)
			continue;
		if (rec.m_baseDefence && rec.m_mode == AITEAM_DEFENDING)
		{
			homeValue += teamValue(team);
			// Follow the threat when it has moved on.
			if (dist2D(rec.m_target, where) > 200.0f && now - rec.m_orderFrame >= 3 * LOGICFRAMES_PER_SECOND)
				sendTeamToBase(team, rec, where);
			continue;
		}
		// Still near home: not far out on a wave.
		if (dist2D(*p, base) > m_ai->m_baseRadius + 250.0f + 600.0f)
			continue;
		homeValue += teamValue(team);
		order[count++] = i;
	}
	const Bool hit = m_bdDamageFrame != 0 && now - m_bdDamageFrame <= 4 * LOGICFRAMES_PER_SECOND;
	if (count > 0 && !hit && homeValue < sk.m_baseDefenceMinAdvantage * value)
	{
		if (now - m_bdWeakFrame >= 10 * LOGICFRAMES_PER_SECOND)
		{
			m_bdWeakFrame = now;
			AI_TRACE("BASEDEF: %d team(s) at home (%.0f) stay together at the rally point against %.0f (nothing of ours is hit)", count, homeValue, value);
		}
		return;
	}
	Int sent = 0;
	for (Int c = 0; c < count; ++c)
	{
		AITeamRecord &rec = m_teams[order[c]];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		sendTeamToBase(team, rec, where);
		++sent;
		++m_bdOrders;
	}
	if (sent > 0)
		AI_TRACE("BASEDEF: %d team(s) go to (%.0f,%.0f) against %.0f (%.0f at home)", sent, where.x, where.y, value, homeValue);
}
