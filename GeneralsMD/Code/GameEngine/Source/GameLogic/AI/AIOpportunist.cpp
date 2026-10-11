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

// AIOpportunist.cpp
// Better targets for units of the Expert that knock down buildings undisturbed.
//
// A team that is not in a fight (nothing armed of the enemy around it), whose units shoot at buildings, looks around (OpportunistRadius):
//   1. an enemy worker or gatherer (a dozer, a supply truck) is killed first: it would rebuild or bring money,
//   2. else a building the enemy has just begun (under construction) is knocked down before it is finished.
// Then the units go back to the buildings (when the new target is gone they stand idle, and the razing of the team takes them on).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void AIStrategy::updateOpportunist()
{
	const AISkillSettings &sk = skill();
	if (!sk.m_opportunist)
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextOpportunist)
		return;
	m_nextOpportunist = now + LOGICFRAMES_PER_SECOND;

	for (Int t = 0; t < m_numTeams; ++t)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[t].m_team);
		const Coord3D *at = team ? team->getEstimateTeamPosition() : nullptr;
		if (at == nullptr)
			continue;
		// Members that shoot at an enemy building.
		Object *shooters[32];
		Int numShooters = 0;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done() && numShooters < 32; it.advance())
		{
			Object *obj = it.cur();
			if (obj == nullptr || obj->isEffectivelyDead() || obj->getAI() == nullptr || isDetached(obj->getID()))
				continue;
			Object *victim = obj->getAI()->getCurrentVictim();
			if (victim && victim->isKindOf(KINDOF_STRUCTURE) && m_player->getRelationship(victim->getTeam()) == ENEMIES)
				shooters[numShooters++] = obj;
		}
		if (numShooters == 0)
			continue;
		// Undisturbed: nothing of the enemy that fights (units or defences) around.
		Real ours = 0.0f, theirs = 0.0f;
		fightAdvantage(at, 400.0f, &ours, &theirs);
		if (theirs > 0.0f)
			continue;

		// The best one around: a worker or gatherer, else a building under construction.
		PartitionFilterAlive alive;
		PartitionFilter *filters[] = { &alive, nullptr };
		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(at, sk.m_opportunistRadius, FROM_CENTER_2D, filters, ITER_SORTED_NEAR_TO_FAR);
		MemoryPoolObjectHolder hold(iter);
		Object *best = nullptr;
		Int bestRank = 0;
		for (Object *obj = iter->first(); obj; obj = iter->next())
		{
			if (obj->isOffMap() || obj->isContained() || m_player->getRelationship(obj->getTeam()) != ENEMIES)
				continue;
			if (obj->getControllingPlayer() == nullptr || !obj->getControllingPlayer()->isPlayableSide())
				continue;
			if (obj->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
				continue;
			if (obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED))
				continue;
			if (obj->isSignificantlyAboveTerrain())
				continue;	// a gatherer in the air (a helicopter): not for ground troops
			Int rank = 0;
			if (obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_HARVESTER))
				rank = 2;
			else if (obj->isKindOf(KINDOF_STRUCTURE) && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
				rank = 1;
			if (rank > bestRank)	// near to far: the first of the best kind is the nearest
			{
				best = obj;
				bestRank = rank;
			}
		}
		if (best == nullptr)
			continue;
		const AICombatFigures *bf = AICombatModel::figures(best->getTemplate());
		Int sent = 0;
		for (Int i = 0; i < numShooters; ++i)
		{
			Object *u = shooters[i];
			if (u->getAI()->getCurrentVictim() == best || u->getAI()->getGoalObject() == best)
				continue;
			const AICombatFigures *uf = AICombatModel::figures(u->getTemplate());
			if (uf == nullptr || bf == nullptr || AICombatModel::damagePerSecond(uf, bf) <= 0.0f)
				continue;
			u->getAI()->aiAttackObject(best, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
			++sent;
		}
		if (sent > 0)
		{
			++m_opportunistOrders;
			AI_TRACE("OPPORTUNIST: team %u: %d unit(s) leave the buildings for %s %u (%s)", team->getID(), sent, best->getTemplate()->getName().str(), best->getID(),
				bestRank == 2 ? "a worker" : "a building under construction");
		}
	}
}
