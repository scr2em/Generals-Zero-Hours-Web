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

// AIAirSupport.cpp
// Close air support for the Expert computer player.
//
//  * A team of ours on the ground that is in a fight, where no anti-air weapon has been seen (units or structures that can hit
//    aircraft: their reach plus AirMargin), calls our idle combat aircraft that can hurt what it fights (AirSupportPlanes at most).
//    An aircraft goes only when its way there stays out of the seen anti-air: straight, or around it (AIPlanDetour); else it stays.
//  * It attacks the enemy nearest to the team; when that one is gone, whatever enemy it can hurt within its vision; when there is
//    nothing left (or it is hurt, or it was out long), it flies home the same careful way: around the anti-air seen by then.
// Only what the player has seen (AIEnemyModel), template data, and what the aircraft itself sees.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIRoute.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static Real healthOf(const Object *obj)
{
	const BodyModuleInterface *body = obj->getBodyModule();
	return body && body->getMaxHealth() > 0.0f ? body->getHealth() / body->getMaxHealth() : 1.0f;
}

/// A combat aircraft of ours that can hurt things on the ground (not a transport).
static Bool isStrikeAircraft(const Object *obj)
{
	if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_AIRCRAFT) || obj->isKindOf(KINDOF_STRUCTURE) || obj->getAI() == nullptr)
		return FALSE;
	const ContainModuleInterface *contain = const_cast<Object *>(obj)->getContain();
	if (contain && contain->getContainMax() > 0 && !contain->isGarrisonable())
		return FALSE;	// a transport
	const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
	return f && f->m_armed && f->m_canHitGround;
}

Bool AIStrategy::airSupportOn() const
{
	return skill().m_airSupportPlanes > 0;
}

Bool AIStrategy::isAirSortie( ObjectID id ) const
{
	for (Int i = 0; i < m_numSorties; ++i)
		if (m_sorties[i].m_unit == id)
			return TRUE;
	return FALSE;
}

/// The way for an aircraft from one place to another around the seen anti-air: waypoints into `path`.  FALSE when there is no such way.
Bool AIStrategy::airWay( const Coord3D &from, const Coord3D &to, Coord3D *path, Int *len ) const
{
	AIRouteCircle circles[AIROUTE_MAX_CIRCLES];
	const Int n = collectAntiAirCircles(circles, AIROUTE_MAX_CIRCLES);
	*len = 0;
	if (n == 0)
		return TRUE;
	// A place inside the reach of anti-air (the target itself) has no safe way at all.
	for (Int i = 0; i < n; ++i)
		if (dist2D(circles[i].m_center, to) < circles[i].m_radius)
			return FALSE;
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	const Int count = AIPlanDetour(from, to, circles, n, path, AIROUTE_MAX_POINTS, skill().m_routeMaxDetour, &extent);
	if (count < 0)
		return FALSE;
	*len = count;
	return TRUE;
}

/// The enemy that an aircraft should hit: within `radius` of `where`, visible to us, one it can hurt; the most dangerous and nearest first.
Object *AIStrategy::airTarget( const Object *plane, const Coord3D &where, Real radius ) const
{
	const AICombatFigures *mine = AICombatModel::figures(plane->getTemplate());
	if (mine == nullptr)
		return nullptr;
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(&where, radius, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	Object *best = nullptr;
	Real bestScore = 0.0f;
	for (Object *obj = iter->first(); obj; obj = iter->next())
	{
		if (obj->isOffMap() || obj->isContained() || m_player->getRelationship(obj->getTeam()) != ENEMIES)
			continue;
		// A player's things only (no civilian buildings), and no missiles in flight.
		if (obj->getControllingPlayer() == nullptr || !obj->getControllingPlayer()->isPlayableSide() || obj->isKindOf(KINDOF_PROJECTILE))
			continue;
		if (obj->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
			continue;
		if (obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED))
			continue;
		const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
		if (f == nullptr || f->m_airborne || AICombatModel::damagePerSecond(mine, f) <= 0.0f)
			continue;
		// What can shoot aircraft first (units, defences or infantry with anti-air), then workers and gatherers, vehicles, buildings,
		// the other infantry; within a kind the nearer and the quicker to kill.
		Real rank;
		if (f->m_armed && f->m_canHitAir)
			rank = 5.0f;
		else if (obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER))
			rank = 4.0f;
		else if (obj->isKindOf(KINDOF_VEHICLE))
			rank = 3.0f;
		else if (obj->isKindOf(KINDOF_STRUCTURE))
			rank = 2.0f;
		else
			rank = 1.0f;
		const Real dps = AICombatModel::damagePerSecond(mine, f);
		const Real kill = dps > 0.0f ? f->m_cost / dps : 1.0e6f;	// a stand-in for the time to kill: dearer things are tougher
		const Real score = rank * 1000.0f - dist2D(*obj->getPosition(), where) * 0.5f - kill * 0.01f;
		if (best == nullptr || score > bestScore)
		{
			best = obj;
			bestScore = score;
		}
	}
	return best;
}

void AIStrategy::sendSortieHome( AIAirSortie &s, Object *plane, const char *why )
{
	Coord3D home;
	if (!m_ai->getBaseCenter(&home))
		home = *plane->getPosition();
	s.m_phase = SORTIE_RETURNING;
	s.m_since = TheGameLogic->getFrame();
	s.m_target = INVALID_ID;
	if (!airWay(*plane->getPosition(), home, s.m_path, &s.m_pathLen))
		s.m_pathLen = 0;	// no way around: the straight way (staying is worse)
	s.m_path[s.m_pathLen < AIROUTE_MAX_POINTS ? s.m_pathLen : AIROUTE_MAX_POINTS - 1] = home;
	if (s.m_pathLen < AIROUTE_MAX_POINTS)
		++s.m_pathLen;
	s.m_pathIdx = 0;
	plane->getAI()->aiMoveToPosition(&s.m_path[0], CMD_FROM_AI);
	AI_TRACE("AIR SUPPORT: %s %u flies home (%s), %d waypoint(s)", plane->getTemplate()->getName().str(), plane->getID(), why, s.m_pathLen - 1);
}

void AIStrategy::updateAirSupport()
{
	if (!airSupportOn())
	{
		m_numSorties = 0;
		return;
	}
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextAirSupport)
		return;
	m_nextAirSupport = now + LOGICFRAMES_PER_SECOND;
	const AISkillSettings &sk = skill();

	// The sorties under way.
	Int out = 0;
	for (Int i = 0; i < m_numSorties; ++i)
	{
		AIAirSortie &s = m_sorties[i];
		Object *plane = TheGameLogic->findObjectByID(s.m_unit);
		if (plane == nullptr || plane->isEffectivelyDead() || plane->getControllingPlayer() != m_player || plane->getAI() == nullptr)
			continue;
		Bool keep = TRUE;
		if (s.m_phase == SORTIE_GOING || s.m_phase == SORTIE_RETURNING)
		{
			const Coord3D &wp = s.m_path[s.m_pathIdx];
			if (dist2D(*plane->getPosition(), wp) < 150.0f || plane->getAI()->isIdle())
			{
				if (s.m_pathIdx + 1 < s.m_pathLen)
				{
					++s.m_pathIdx;
					plane->getAI()->aiMoveToPosition(&s.m_path[s.m_pathIdx], CMD_FROM_AI);
				}
				else if (s.m_phase == SORTIE_RETURNING)
				{
					keep = FALSE;	// home: back to its role (a jet lands to reload by itself)
					plane->getAI()->aiIdle(CMD_FROM_AI);
				}
				else
				{
					s.m_phase = SORTIE_ATTACKING;
					s.m_since = now;
				}
			}
		}
		if (keep && s.m_phase == SORTIE_ATTACKING)
		{
			Object *target = TheGameLogic->findObjectByID(s.m_target);
			const Bool done = target == nullptr || target->isEffectivelyDead() || m_player->getRelationship(target->getTeam()) != ENEMIES;
			if (healthOf(plane) < 0.4f)
				sendSortieHome(s, plane, "hurt");
			else if (now - s.m_start > secondsToFrames(sk.m_airSupportSeconds))
				sendSortieHome(s, plane, "out long enough");
			else if (done || plane->getAI()->isIdle())
			{
				// What it can hurt within its own sight (and still out of the seen anti-air), else home.
				Object *next = airTarget(plane, *plane->getPosition(), plane->getVisionRange());
				AIRouteCircle circles[AIROUTE_MAX_CIRCLES];
				// (An anti-air weapon itself is the target to take first; anything else must be out of the anti-air.)
				const AICombatFigures *nf = next ? AICombatModel::figures(next->getTemplate()) : nullptr;
				const Bool nextIsAntiAir = nf && nf->m_armed && nf->m_canHitAir;
				const Int n = next && !nextIsAntiAir ? collectAntiAirCircles(circles, AIROUTE_MAX_CIRCLES) : 0;
				for (Int c = 0; c < n && next; ++c)
					if (dist2D(circles[c].m_center, *next->getPosition()) < circles[c].m_radius)
						next = nullptr;
				if (next)
				{
					s.m_target = next->getID();
					plane->getAI()->aiAttackObject(next, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					++m_airSupportRetargets;
					AI_TRACE("AIR SUPPORT: %s %u takes the next target in sight: %s %u", plane->getTemplate()->getName().str(), plane->getID(),
						next->getTemplate()->getName().str(), next->getID());
				}
				else
					sendSortieHome(s, plane, done ? "nothing left in sight" : "idle");
			}
		}
		if (keep)
			m_sorties[out++] = s;
	}
	m_numSorties = out;

	// New sorties: a team of ours in a fight, no anti-air seen there, idle aircraft with a safe way.
	if (m_numSorties >= MAX_SORTIES || now < m_nextAirCall)
		return;
	AIRouteCircle circles[AIROUTE_MAX_CIRCLES];
	const Int numCircles = collectAntiAirCircles(circles, AIROUTE_MAX_CIRCLES);
	for (Int t = 0; t < m_numTeams && m_numSorties < MAX_SORTIES; ++t)
	{
		const AITeamRecord &rec = m_teams[t];
		Team *team = TheTeamFactory->findTeamByID(rec.m_team);
		const Coord3D *at = team ? team->getEstimateTeamPosition() : nullptr;
		if (at == nullptr)
			continue;
		Real ours = 0.0f, theirs = 0.0f;
		fightAdvantage(at, 450.0f, &ours, &theirs);
		if (theirs <= 0.0f)
			continue;	// not in a fight
		Bool covered = FALSE;
		for (Int c = 0; c < numCircles && !covered; ++c)
			covered = dist2D(circles[c].m_center, *at) < circles[c].m_radius + 200.0f;
		if (covered)
			continue;	// anti-air there: the aircraft stay out of it
		// Aircraft already helping this team count.
		Int helping = 0;
		for (Int i = 0; i < m_numSorties; ++i)
			if (m_sorties[i].m_team == rec.m_team && m_sorties[i].m_phase != SORTIE_RETURNING)
				++helping;
		if (helping >= sk.m_airSupportPlanes)
			continue;
		// Our idle combat aircraft, the nearest first.
		for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end() && helping < sk.m_airSupportPlanes; ++it)
		{
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done() && helping < sk.m_airSupportPlanes; iter.advance())
			{
				for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done() && helping < sk.m_airSupportPlanes && m_numSorties < MAX_SORTIES; oit.advance())
				{
					Object *plane = oit.cur();
					if (!isStrikeAircraft(plane) || isAirSortie(plane->getID()) || isDetached(plane->getID()) || !plane->getAI()->isIdle() || healthOf(plane) < 0.7f)
						continue;
					Object *target = airTarget(plane, *at, 450.0f);
					if (target == nullptr)
						continue;	// nothing there that it can hurt
					AIAirSortie &s = m_sorties[m_numSorties];
					memset(&s, 0, sizeof(s));
					if (!airWay(*plane->getPosition(), *target->getPosition(), s.m_path, &s.m_pathLen))
					{
						++m_airSupportUnsafe;
						continue;	// no way there out of the seen anti-air
					}
					s.m_unit = plane->getID();
					s.m_team = rec.m_team;
					s.m_target = target->getID();
					s.m_start = now;
					s.m_since = now;
					s.m_pathIdx = 0;
					if (s.m_pathLen > 0)
					{
						s.m_phase = SORTIE_GOING;
						plane->getAI()->aiMoveToPosition(&s.m_path[0], CMD_FROM_AI);
					}
					else
					{
						s.m_phase = SORTIE_ATTACKING;
						plane->getAI()->aiAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
					}
					++m_numSorties;
					++helping;
					++m_airSupportSorties;
					m_nextAirCall = now + 3 * LOGICFRAMES_PER_SECOND;
					AI_TRACE("AIR SUPPORT: %s %u supports team %u at (%.0f,%.0f) against %s %u (enemy %.0f, ours %.0f), %d waypoint(s) around the anti-air",
						plane->getTemplate()->getName().str(), plane->getID(), rec.m_team, at->x, at->y, target->getTemplate()->getName().str(), target->getID(),
						theirs, ours, s.m_pathLen);
				}
			}
		}
	}
}
