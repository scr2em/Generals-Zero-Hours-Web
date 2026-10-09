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

// AIAirborne.cpp
// Airborne insertion for the Expert computer player.
//
// When the player owns a transport aircraft (KindOf AIRCRAFT with a contain module that holds passengers) it
//  1. picks a squad of units that fit (the action manager decides whether a unit may enter) and a soft target the
//     player has seen: a gatherer, a power plant, a production building, a superweapon,
//  2. picks a drop point next to the target that no known ground defence covers, and plans the flight around the
//     coverage of every anti-air weapon the player has seen (units and structures; template data: a weapon that can
//     hit airborne units, its range plus a margin) - both the way there and the way back.  If there is no way, it
//     does not go,
//  3. loads the squad at the rally point, flies the way, lets the squad out at the drop point, sends the squad at
//     the target and the aircraft home.
//
// Only what has been seen (AIEnemyModel) and template data.  The squad and the aircraft belong to the mission
// (AIStrategy::isDetached) until it is over.

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
#include "GameLogic/TerrainLogic.h"
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

enum
{
	AIR_NONE = 0,
	AIR_TO_PICKUP,			///< the aircraft flies to the rally point
	AIR_LOADING,				///< the squad boards
	AIR_FLYING,					///< on the way to the drop point
	AIR_DROPPING,				///< the squad leaves
	AIR_ASSAULT,				///< the squad is on the ground; the aircraft is on its way back
	AIR_RETURNING				///< the squad is done; the aircraft is still on its way
};

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::airborneOn() const
{
	return (skill().m_useAirborne || m_ai->isFeatureForced(AIPlayer::AIF_AIRBORNE)) && !m_ai->isFeatureOff(AIPlayer::AIF_AIRBORNE);
}

/// A transport aircraft: an aircraft whose contain module takes passengers.
static Bool isTransportAircraft(Object *obj)
{
	if (obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_AIRCRAFT) || obj->isKindOf(KINDOF_STRUCTURE))
		return FALSE;
	ContainModuleInterface *contain = obj->getContain();
	return contain != nullptr && contain->getContainMax() > 0 && !contain->isGarrisonable();
}

/// Is the unit part of the mission (the aircraft, or the squad), or a transport aircraft that missions use?
Bool AIStrategy::isAirborne( ObjectID id ) const
{
	if (id == INVALID_ID)
		return FALSE;
	if (m_airPhase != AIR_NONE)
	{
		if (id == m_airTransport)
			return TRUE;
		for (Int i = 0; i < m_numSquad; ++i)
			if (m_squad[i] == id)
				return TRUE;
	}
	if (airborneOn())
	{
		// Transport aircraft are the mission's: the army's orders do not take them along.
		Object *obj = TheGameLogic->findObjectByID(id);
		if (obj && isTransportAircraft(obj))
			return TRUE;
	}
	return FALSE;
}

/// The seen anti-air coverage as circles: weapon range plus the margin of the settings.
Int AIStrategy::collectAntiAirCircles( AIRouteCircle *circles, Int maxCircles ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const Real margin = skill().m_airMargin;
	Int n = 0;
	for (Int i = 0; i < m_enemy.numContacts() && n < maxCircles; ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || !f->m_canHitAir || f->m_range <= 0.0f)
			continue;
		// Units move; a sighting that is old tells little.
		if (!f->m_structure && now - c.m_lastSeen > 25 * LOGICFRAMES_PER_SECOND)
			continue;
		circles[n].m_center = c.m_pos;
		circles[n].m_radius = f->m_range + margin;
		++n;
	}
	return n;
}

/// Is the point outside the reach of every known weapon that could hit our units on the ground?
Bool AIStrategy::dropZoneSafe( const Coord3D &p, Real squadValue ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Real guard = 0.0f;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		if (c.m_role > AIROLE_DEFENCE)
			continue;
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(c.m_templateID));
		if (f == nullptr || !f->m_armed || !f->m_canHitGround)
			continue;
		const Real d = dist2D(c.m_pos, p);
		if (f->m_structure)
		{
			if (d < f->m_range + 40.0f)
				return FALSE;
		}
		else if (now - c.m_lastSeen <= 15 * LOGICFRAMES_PER_SECOND && d < 280.0f)
		{
			guard += f->m_cost;
		}
	}
	return guard <= skill().m_airGuardShare * squadValue;
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::endAirMission( const char *why )
{
	AI_TRACE("AIRBORNE mission %d over: %s", m_airMissions, why);
	m_airPhase = AIR_NONE;
	m_numSquad = 0;
	m_airTransport = INVALID_ID;
	m_airPathLen = m_airBackLen = 0;
	m_airCooldown = TheGameLogic->getFrame() + secondsToFrames(skill().m_airCooldownSeconds);
}

/**
 * Looks for an aircraft, a squad, a target and a safe way; starts the mission when all four are there.
 */
void AIStrategy::tryStartAirMission()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	const AISkillSettings &sk = skill();
	if (now < m_airCooldown || m_enemy.numContacts() == 0)
		return;
	if (m_threatSince != 0 || m_player->getAttackedFrame() + 10 * LOGICFRAMES_PER_SECOND > now)
		return;

	// The aircraft.
	Object *transport = nullptr;
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end() && transport == nullptr; ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done() && transport == nullptr; iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *obj = oit.cur();
				if (obj == nullptr || !isTransportAircraft(obj) || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr)
					continue;
				if (healthShare(obj) < 0.6f || obj->getContain()->getContainCount() > 0)
					continue;
				transport = obj;
				break;
			}
		}
	}
	if (transport == nullptr)
		return;

	// The squad: armed units of the field teams that may board and are near the aircraft or the rally point.
	Coord3D rally;
	if (!rallyPoint(&rally))
		return;
	Object *squad[MAX_SQUAD];
	Int numSquad = 0;
	Int slots = 0;
	const Int capacity = transport->getContain()->getContainMax();
	Real squadValue = 0.0f;
	for (Int i = 0; i < m_numTeams && numSquad < MAX_SQUAD; ++i)
	{
		const AITeamRecord &rec = m_teams[i];
		if (rec.m_mode != AITEAM_FREE && rec.m_mode != AITEAM_REGROUPING)
			continue;
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done() && numSquad < MAX_SQUAD; it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->isContained() || obj->isDisabled() || obj->getAI() == nullptr || isDetached(obj->getID()))
				continue;
			const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
			if (f == nullptr || !f->m_armed || !f->m_canHitGround || f->m_airborne || f->m_structure || healthShare(obj) < 0.7f)
				continue;
			if (dist2D(*obj->getPosition(), rally) > 500.0f)
				continue;
			const Int need = obj->getTransportSlotCount();
			if (need <= 0 || slots + need > capacity || !TheActionManager->canEnterObject(obj, transport, CMD_FROM_AI, DONT_CHECK_CAPACITY))
				continue;
			squad[numSquad++] = obj;
			slots += need;
			squadValue += f->m_cost * healthShare(obj);
		}
	}
	if (numSquad < 2 || squadValue < sk.m_airMinSquadValue)
		return;

	// The target and the drop point.
	AIRouteCircle aa[AIROUTE_MAX_CIRCLES];
	const Int numAA = collectAntiAirCircles(aa, AIROUTE_MAX_CIRCLES);
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	const Coord3D start = *transport->getPosition();

	const AICombatFigures *soldier = AICombatModel::figures(squad[0]->getTemplate());
	Real bestScore = 0.0f;
	Bool found = FALSE;
	AIContact bestContact;
	Coord3D bestDrop;
	Coord3D bestPath[AIROUTE_MAX_POINTS];
	Int bestPathLen = 0;
	Coord3D bestBack[AIROUTE_MAX_POINTS];
	Int bestBackLen = 0;
	Int rejectedNoPath = 0;
	for (Int i = 0; i < m_enemy.numContacts(); ++i)
	{
		const AIContact &c = m_enemy.contacts()[i];
		const ThingTemplate *tt = TheThingFactory->findByTemplateID(c.m_templateID);
		const AICombatFigures *f = AICombatModel::figures(tt);
		if (f == nullptr || tt == nullptr)
			continue;
		Real mult = 0.0f;
		if (tt->isKindOf(KINDOF_FS_SUPERWEAPON))
			mult = 2.5f;
		else if (c.m_role == AIROLE_ECONOMY && !f->m_structure)
			mult = 1.0f;
		else if (tt->isKindOf(KINDOF_FS_POWER))
			mult = 1.4f;
		else if (c.m_role == AIROLE_PRODUCTION)
			mult = 1.2f;
		else if (c.m_role == AIROLE_ECONOMY)
			mult = 1.0f;
		if (mult <= 0.0f)
			continue;
		if (f->m_structure && AICombatModel::damagePerSecond(soldier, f) <= 0.0f)
			continue;
		if (!f->m_structure && now - c.m_lastSeen > 25 * LOGICFRAMES_PER_SECOND)
			continue;
		const Real value = f->m_cost * mult / (1.0f + dist2D(start, c.m_pos) / 1500.0f);
		if (found && value <= bestScore)
			continue;

		// A drop point next to the target: eight directions, the one nearest to us that is safe.
		static const Real cosines[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
		static const Real sines[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f };
		Coord3D drop;
		Bool haveDrop = FALSE;
		Real dropD = 0.0f;
		for (Int k = 0; k < 8; ++k)
		{
			Coord3D p;
			p.x = c.m_pos.x + cosines[k] * 110.0f;
			p.y = c.m_pos.y + sines[k] * 110.0f;
			p.z = TheTerrainLogic->getGroundHeight(p.x, p.y);
			if (p.x < extent.lo.x + 60.0f || p.x > extent.hi.x - 60.0f || p.y < extent.lo.y + 60.0f || p.y > extent.hi.y - 60.0f)
				continue;
			// The aircraft must not drop into anti-air fire.
			Bool inAA = FALSE;
			for (Int a = 0; a < numAA && !inAA; ++a)
				inAA = dist2D(aa[a].m_center, p) < aa[a].m_radius;
			if (inAA || !dropZoneSafe(p, squadValue))
				continue;
			const Real d = dist2D(start, p);
			if (!haveDrop || d < dropD)
			{
				drop = p;
				dropD = d;
				haveDrop = TRUE;
			}
		}
		if (!haveDrop)
			continue;

		// The way there and back around the anti-air coverage.
		Coord3D out[AIROUTE_MAX_POINTS], back[AIROUTE_MAX_POINTS];
		const Int nOut = AIPlanDetour(start, drop, aa, numAA, out, AIROUTE_MAX_POINTS - 1, sk.m_airMaxDetour, &extent);
		const Int nBack = nOut < 0 ? -1 : AIPlanDetour(drop, rally, aa, numAA, back, AIROUTE_MAX_POINTS - 1, sk.m_airMaxDetour, &extent);
		if (nOut < 0 || nBack < 0)
		{
			++rejectedNoPath;
			continue;
		}
		found = TRUE;
		bestScore = value;
		bestContact = c;
		bestDrop = drop;
		bestPathLen = nOut;
		for (Int k = 0; k < nOut; ++k)
			bestPath[k] = out[k];
		bestBackLen = nBack;
		for (Int k = 0; k < nBack; ++k)
			bestBack[k] = back[k];
	}
	if (!found)
	{
		if (rejectedNoPath > 0 && now >= m_airNoteFrame)
		{
			m_airNoteFrame = now + 30 * LOGICFRAMES_PER_SECOND;
			++m_airNoPath;
			AI_TRACE("AIRBORNE: squad of %d (value %.0f) and aircraft %u ready, but no safe way to a target (%d anti-air areas known, %d targets rejected): staying home",
				numSquad, squadValue, transport->getID(), numAA, rejectedNoPath);
		}
		return;
	}

	// Go.
	m_airTransport = transport->getID();
	m_numSquad = numSquad;
	for (Int i = 0; i < numSquad; ++i)
		m_squad[i] = squad[i]->getID();
	for (Int i = 0; i < bestPathLen; ++i)
		m_airPath[i] = bestPath[i];
	m_airPath[bestPathLen] = bestDrop;
	m_airPathLen = bestPathLen + 1;
	m_airPathIdx = 0;
	for (Int i = 0; i < bestBackLen; ++i)
		m_airBack[i] = bestBack[i];
	m_airBack[bestBackLen] = rally;
	m_airBackLen = bestBackLen + 1;
	m_airTargetID = bestContact.m_id;
	m_airTarget = bestContact.m_pos;
	m_airPhase = AIR_TO_PICKUP;
	m_airPhaseFrame = now;
	m_airStart = now;
	++m_airMissions;
	AI_TRACE("AIRBORNE mission %d: aircraft %u takes %d units (value %.0f) to %s %u at (%.0f,%.0f); drop at (%.0f,%.0f); %d waypoint(s) out, %d back; %d anti-air areas avoided",
		m_airMissions, transport->getID(), numSquad, squadValue, TheThingFactory->findByTemplateID(bestContact.m_templateID)->getName().str(), bestContact.m_id,
		bestContact.m_pos.x, bestContact.m_pos.y, bestDrop.x, bestDrop.y, bestPathLen, bestBackLen, numAA);
	transport->getAI()->aiMoveToPosition(&rally, CMD_FROM_AI);
}

/// The mission under way.
void AIStrategy::updateAirMission()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	Object *transport = TheGameLogic->findObjectByID(m_airTransport);
	if (transport == nullptr || transport->isEffectivelyDead())
	{
		++m_airLost;
		endAirMission("the aircraft is lost");
		return;
	}
	AIUpdateInterface *tai = transport->getAI();
	ContainModuleInterface *contain = transport->getContain();
	Coord3D rally;
	if (tai == nullptr || contain == nullptr || !rallyPoint(&rally))
	{
		endAirMission("the aircraft cannot be commanded");
		return;
	}

	// The squad: the living ones.
	Int out = 0;
	for (Int i = 0; i < m_numSquad; ++i)
	{
		Object *o = TheGameLogic->findObjectByID(m_squad[i]);
		if (o && !o->isEffectivelyDead())
			m_squad[out++] = m_squad[i];
	}
	m_numSquad = out;

	switch (m_airPhase)
	{
		case AIR_TO_PICKUP:
		{
			if (dist2D(*transport->getPosition(), rally) < 110.0f || now - m_airPhaseFrame > 30 * LOGICFRAMES_PER_SECOND)
			{
				if (m_numSquad < 2)
				{
					endAirMission("the squad is gone");
					return;
				}
				m_airPhase = AIR_LOADING;
				m_airPhaseFrame = now;
				for (Int i = 0; i < m_numSquad; ++i)
				{
					Object *o = TheGameLogic->findObjectByID(m_squad[i]);
					if (o && o->getAI())
						o->getAI()->aiEnter(transport, CMD_FROM_AI);
				}
			}
			break;
		}

		case AIR_LOADING:
		{
			Int aboard = 0;
			for (Int i = 0; i < m_numSquad; ++i)
			{
				Object *o = TheGameLogic->findObjectByID(m_squad[i]);
				if (o && o->isContained() && o->getContainedBy() == transport)
					++aboard;
			}
			const Bool all = aboard == m_numSquad;
			const Bool enough = aboard >= 2 && aboard * 3 >= m_numSquad * 2 && now - m_airPhaseFrame > 12 * LOGICFRAMES_PER_SECOND;
			if (all || enough)
			{
				// Anyone left behind does not count as part of the mission.
				Int keep = 0;
				for (Int i = 0; i < m_numSquad; ++i)
				{
					Object *o = TheGameLogic->findObjectByID(m_squad[i]);
					if (o && o->isContained() && o->getContainedBy() == transport)
						m_squad[keep++] = m_squad[i];
				}
				m_numSquad = keep;
				m_airPhase = AIR_FLYING;
				m_airPhaseFrame = now;
				m_airPathIdx = 0;
				AI_TRACE("AIRBORNE: %d aboard, flying out; first (%.0f,%.0f)", aboard, m_airPath[0].x, m_airPath[0].y);
				tai->aiMoveToPosition(&m_airPath[0], CMD_FROM_AI);
			}
			else if (now - m_airPhaseFrame > 30 * LOGICFRAMES_PER_SECOND)
			{
				contain->orderAllPassengersToExit(CMD_FROM_AI, FALSE);
				endAirMission("the squad did not board in time");
			}
			break;
		}

		case AIR_FLYING:
		{
			if (healthShare(transport) < 0.35f)
			{
				// Too hurt to get there: put the squad down where we are.
				AI_TRACE("AIRBORNE: the aircraft is badly hurt, dropping the squad here");
				m_airPathIdx = m_airPathLen - 1;
				m_airPath[m_airPathIdx] = *transport->getPosition();
			}
			if (dist2D(*transport->getPosition(), m_airPath[m_airPathIdx]) < 70.0f)
			{
				if (m_airPathIdx + 1 < m_airPathLen)
				{
					++m_airPathIdx;
					tai->aiMoveToPosition(&m_airPath[m_airPathIdx], CMD_FROM_AI);
				}
				else
				{
					m_airPhase = AIR_DROPPING;
					m_airPhaseFrame = now;
					AI_TRACE("AIRBORNE: at the drop point (%.0f,%.0f), the squad leaves the aircraft", m_airPath[m_airPathIdx].x, m_airPath[m_airPathIdx].y);
					tai->aiEvacuate(FALSE, CMD_FROM_AI);
				}
			}
			else if (now - m_airPhaseFrame > secondsToFrames(skill().m_airMaxSeconds))
			{
				// Lost on the way: home with the squad.
				contain->orderAllPassengersToExit(CMD_FROM_AI, FALSE);
				endAirMission("the flight took too long");
			}
			else if (tai->isIdle())
			{
				tai->aiMoveToPosition(&m_airPath[m_airPathIdx], CMD_FROM_AI);
			}
			break;
		}

		case AIR_DROPPING:
		{
			if (contain->getContainCount() == 0 || now - m_airPhaseFrame > 10 * LOGICFRAMES_PER_SECOND)
			{
				if (contain->getContainCount() > 0)
					contain->orderAllPassengersToExit(CMD_FROM_AI, FALSE);
				++m_airDrops;
				// The squad attacks the target; the aircraft goes home the way it was planned.
				for (Int i = 0; i < m_numSquad; ++i)
				{
					Object *o = TheGameLogic->findObjectByID(m_squad[i]);
					if (o == nullptr || o->getAI() == nullptr)
						continue;
					Object *target = TheGameLogic->findObjectByID(m_airTargetID);
					if (target && enemyCanSee(target))
						o->getAI()->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					else
						o->getAI()->aiAttackMoveToPosition(&m_airTarget, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
				}
				m_airPhase = AIR_ASSAULT;
				m_airPhaseFrame = now;
				m_airPathIdx = 0;
				m_airAssaultStart = now;
				tai->aiMoveToPosition(&m_airBack[0], CMD_FROM_AI);
				AI_TRACE("AIRBORNE: the squad (%d) attacks, the aircraft flies home via (%.0f,%.0f)", m_numSquad, m_airBack[0].x, m_airBack[0].y);
			}
			break;
		}

		case AIR_ASSAULT:
		case AIR_RETURNING:
		{
			// The aircraft: along the way back.
			if (m_airPathIdx < m_airBackLen)
			{
				if (dist2D(*transport->getPosition(), m_airBack[m_airPathIdx]) < 90.0f)
				{
					++m_airPathIdx;
					if (m_airPathIdx < m_airBackLen)
						tai->aiMoveToPosition(&m_airBack[m_airPathIdx], CMD_FROM_AI);
				}
				else if (tai->isIdle())
				{
					tai->aiMoveToPosition(&m_airBack[m_airPathIdx], CMD_FROM_AI);
				}
			}
			const Bool home = m_airPathIdx >= m_airBackLen;

			// The squad: until the target is gone, the squad is gone, or the time is up.
			if (m_airPhase == AIR_ASSAULT)
			{
				Object *target = TheGameLogic->findObjectByID(m_airTargetID);
				const Bool targetGone = target == nullptr || target->isEffectivelyDead();
				if (m_numSquad == 0 || now - m_airAssaultStart > secondsToFrames(skill().m_airAssaultSeconds) || (targetGone && now - m_airAssaultStart > 5 * LOGICFRAMES_PER_SECOND))
				{
					AI_TRACE("AIRBORNE: assault over (%d of the squad left%s)", m_numSquad, targetGone ? ", target destroyed" : "");
					if (targetGone)
						++m_airKills;
					for (Int i = 0; i < m_numSquad; ++i)
					{
						Object *o = TheGameLogic->findObjectByID(m_squad[i]);
						if (o)
							sendPatientBack(o);
					}
					m_numSquad = 0;
					m_airPhase = AIR_RETURNING;
				}
				else
				{
					for (Int i = 0; i < m_numSquad; ++i)
					{
						Object *o = TheGameLogic->findObjectByID(m_squad[i]);
						if (o && o->getAI() && o->getAI()->isIdle())
							o->getAI()->aiAttackMoveToPosition(&m_airTarget, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					}
				}
			}
			if (home && m_airPhase == AIR_RETURNING)
				endAirMission("the aircraft is home");
			else if (home && m_airPhase == AIR_ASSAULT)
				tai->aiMoveToPosition(&rally, CMD_FROM_AI);
			if (now - m_airStart > 240 * LOGICFRAMES_PER_SECOND)
				endAirMission("the mission took too long");
			break;
		}

		default:
			endAirMission("unknown phase");
			break;
	}
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::updateAirborne()
{
	if (!airborneOn())
	{
		if (m_airPhase != AIR_NONE)
			endAirMission("switched off");
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextAirCheck)
		return;
	m_nextAirCheck = now + (m_airPhase == AIR_NONE ? 2 * LOGICFRAMES_PER_SECOND : LOGICFRAMES_PER_SECOND / 2);
	if (m_airPhase == AIR_NONE)
		tryStartAirMission();
	else
		updateAirMission();
}
