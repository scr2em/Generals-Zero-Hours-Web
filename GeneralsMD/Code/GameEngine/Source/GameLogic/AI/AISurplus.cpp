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

// AISurplus.cpp
// Surplus production for the Expert computer player.
//
// The computer player builds only the teams that its scripts offer, and a team is not built again while it exists.  With the real
// game data the money piles up (tens of thousands at the end of a match) while the factories stand idle most of the time.  The
// Expert spends it:
//   * every two seconds, after SurplusStartSeconds, a factory of ours that is idle orders one army unit when the money left after it
//     is at least SurplusReserve,
//   * the unit is the one of the factory's command set that counters best what the enemy model has seen (the weights of the counter
//     pick of the teams, AICombatModel::matchup per unit), the dearer one when two are equal; armed ground units only (no workers,
//     transports or aircraft, which the teams handle in their own way),
//   * when it is out of the factory it is given to a team of the army (one that is not on a wave, else any), so the waves, the
//     reinforcement rules and the tactics take it over.  Until a team exists it waits at the rally point.
// Read from the templates (command sets, KindOf, weapons), no unit names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "GameLogic/SidesList.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::surplusOn() const
{
	return skill().m_useSurplus;
}

/// How well one unit counters what has been seen of the enemy (-1..1, 0 = even, AICombatModel::matchup); 0 when nothing has been seen.
Real AIStrategy::unitCounterScore( const AICombatFigures *f ) const
{
	if (f == nullptr || m_enemy.numComposition() == 0)
		return 0.0f;
	Real weight[AIEnemyModel::MAX_COMPOSITION];
	const AICombatFigures *enemyFig[AIEnemyModel::MAX_COMPOSITION];
	const Real totalWeight = counterWeights(weight, enemyFig);
	if (totalWeight <= 0.0f)
		return 0.0f;
	Real score = 0.0f;
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		if (weight[e] > 0.0f)
			score += (weight[e] / totalWeight) * AICombatModel::matchup(f, enemyFig[e]);
	}
	return score;
}

//-------------------------------------------------------------------------------------------------
void AIStrategy::updateSurplus()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!surplusOn() || now < m_nextSurplus)
		return;
	m_nextSurplus = now + 2 * LOGICFRAMES_PER_SECOND;

	// Orders that never came out (the factory was lost, or the unit was taken by a team order) are forgotten.
	for (Int i = m_numSurplusOrders - 1; i >= 0; --i)
	{
		if (now - m_surplusOrderFrame[i] > 90 * LOGICFRAMES_PER_SECOND)
		{
			m_surplusFactory[i] = m_surplusFactory[m_numSurplusOrders - 1];
			m_surplusThing[i] = m_surplusThing[m_numSurplusOrders - 1];
			m_surplusOrderFrame[i] = m_surplusOrderFrame[m_numSurplusOrders - 1];
			--m_numSurplusOrders;
		}
	}

	adoptSurplusUnits();

	if (now < secondsToFrames(skill().m_surplusStartSeconds) || TheControlBar == nullptr)
		return;

	// The base comes first: the next structure that the build list is waiting for (its tech, the income buildings of the
	// faction, rebuilds) keeps its money.
	Real pending = 0.0f;
	for (BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext())
	{
		if (info->getTemplateName().isEmpty() || !info->isBuildable() || TheGameLogic->findObjectByID(info->getObjectID()) != nullptr)
			continue;
		const ThingTemplate *plan = TheThingFactory->findTemplate(info->getTemplateName());
		// A superweapon is not waited for: the build list (re)builds it when the money is there, the army does not stop for it.
		if (plan && !plan->isKindOf(KINDOF_FS_SUPERWEAPON))
		{
			pending = (Real)plan->calcCostToBuild(m_player);
			break;
		}
	}
	const Real reserve = skill().m_surplusReserve + m_expandCost + pending;
	const Real infantryShare = skill().m_surplusInfantryShare;
	Real money = (Real)m_player->getMoney()->countMoney();
	if (money < reserve)
		return;

	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr)
				continue;
			for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *factory = oit.cur();
				if (m_numSurplusOrders >= MAX_SURPLUS_ORDERS || m_numSurplusUnits + m_numSurplusOrders >= MAX_SURPLUS_UNITS)
					return;
				if (factory == nullptr || factory->isEffectivelyDead() || !factory->isKindOf(KINDOF_STRUCTURE) ||
						factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || factory->testStatus(OBJECT_STATUS_SOLD))
					continue;
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				if (pu == nullptr || pu->getProductionCount() > 0)
					continue;
				const CommandSet *set = TheControlBar->findCommandSet(factory->getCommandSetString());
				if (set == nullptr)
					continue;

				const ThingTemplate *best = nullptr;
				Real bestScore = 0.0f, bestCost = 0.0f;
				for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
				{
					const CommandButton *button = set->getCommandButton(i);
					if (button == nullptr || button->getCommandType() != GUI_COMMAND_UNIT_BUILD)
						continue;
					const ThingTemplate *tmpl = button->getThingTemplate();
					if (tmpl == nullptr || tmpl->isKindOf(KINDOF_DOZER) || tmpl->isKindOf(KINDOF_HARVESTER) ||
							tmpl->isKindOf(KINDOF_TRANSPORT) || (tmpl->isKindOf(KINDOF_AIRCRAFT) && !skill().m_surplusAircraft) || tmpl->isKindOf(KINDOF_STRUCTURE))
						continue;
					const AICombatFigures *f = AICombatModel::figures(tmpl);
					if (f == nullptr || !f->m_armed || !f->m_canHitGround)
						continue;
					if (tmpl->isKindOf(KINDOF_INFANTRY) && (Real)(m_surplusInfantry + 1) > infantryShare * (Real)(m_surplusOrdered + 1))
						continue;
					const Real cost = (Real)tmpl->calcCostToBuild(m_player);
					if (cost <= 0.0f || money - cost < reserve)
						continue;
					if (TheBuildAssistant->canMakeUnit(factory, tmpl) != CANMAKE_OK)
						continue;
					const Real score = unitCounterScore(f);
					if (best == nullptr || score > bestScore + 0.01f || (score > bestScore - 0.01f && cost > bestCost))
					{
						best = tmpl;
						bestScore = score;
						bestCost = cost;
					}
				}
				if (best == nullptr || !pu->queueCreateUnit(best, pu->requestUniqueUnitID()))
					continue;
				m_surplusFactory[m_numSurplusOrders] = factory->getID();
				m_surplusThing[m_numSurplusOrders] = best->getTemplateID();
				m_surplusOrderFrame[m_numSurplusOrders] = now;
				++m_numSurplusOrders;
				++m_surplusOrdered;
				if (best->isKindOf(KINDOF_INFANTRY))
					++m_surplusInfantry;
				m_surplusSpent += bestCost;
				money -= bestCost;
				AI_TRACE("SURPLUS: %s ordered at %s %u (counter score %.2f, cost %.0f, money left %.0f)", best->getName().str(),
					factory->getTemplate()->getName().str(), factory->getID(), bestScore, bestCost, money);
			}
		}
	}
}

/// A unit came out of a factory: if it is one of the surplus orders, it waits at the rally point for a team.
void AIStrategy::onUnitProduced( Object *factory, Object *unit )
{
	if (!surplusOn() || unit->getTeam() != m_player->getDefaultTeam())
		return;	// a team order took it
	for (Int i = 0; i < m_numSurplusOrders; ++i)
	{
		const ThingTemplate *tmpl = TheThingFactory->findByTemplateID((UnsignedShort)m_surplusThing[i]);
		if (m_surplusFactory[i] != factory->getID() || tmpl == nullptr || !unit->getTemplate()->isEquivalentTo(tmpl))
			continue;
		m_surplusFactory[i] = m_surplusFactory[m_numSurplusOrders - 1];
		m_surplusThing[i] = m_surplusThing[m_numSurplusOrders - 1];
		m_surplusOrderFrame[i] = m_surplusOrderFrame[m_numSurplusOrders - 1];
		--m_numSurplusOrders;
		if (m_numSurplusUnits < MAX_SURPLUS_UNITS)
			m_surplusUnits[m_numSurplusUnits++] = unit->getID();
		Coord3D rally;
		if (unit->getAI() && rallyPoint(&rally))
			unit->getAI()->aiMoveToPosition(&rally, CMD_FROM_AI);
		return;
	}
}

/// The surplus units that wait go to a team of the army: one that is not on a wave (it gathers at home), else any.
void AIStrategy::adoptSurplusUnits()
{
	if (m_numSurplusUnits == 0)
		return;
	Team *home = nullptr;
	Team *any = nullptr;
	for (Int t = 0; t < m_numTeams; ++t)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[t].m_team);
		if (team == nullptr || !isManageableTeam(team))
			continue;
		if (any == nullptr)
			any = team;
		if (home == nullptr && !m_teams[t].m_inWave && m_teams[t].m_mode != AITEAM_ATTACKING && m_teams[t].m_mode != AITEAM_RETREATING)
			home = team;
	}
	Team *to = home ? home : any;
	for (Int i = m_numSurplusUnits - 1; i >= 0; --i)
	{
		Object *obj = TheGameLogic->findObjectByID(m_surplusUnits[i]);
		Bool drop = obj == nullptr || obj->isEffectivelyDead() || obj->getTeam() != m_player->getDefaultTeam() ||
			obj->isContained() || isBunkerMan(m_surplusUnits[i]);
		if (!drop && to == nullptr)
			continue;	// no team yet: it waits at the rally point
		if (!drop)
		{
			obj->setTeam(to);
			++m_surplusAdopted;
		}
		m_surplusUnits[i] = m_surplusUnits[m_numSurplusUnits - 1];
		--m_numSurplusUnits;
	}
}
