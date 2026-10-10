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

/// The share of the surplus units ordered so far that were of this kind.
Real AIStrategy::surplusShare( const ThingTemplate *tmpl ) const
{
	for (Int i = 0; i < m_numSurplusKinds; ++i)
		if (m_surplusKind[i] == (Int)tmpl->getTemplateID())
			return (Real)m_surplusKindCount[i] / (Real)(m_surplusOrdered + 1);
	return 0.0f;
}

void AIStrategy::noteSurplusKind( const ThingTemplate *tmpl )
{
	Int i = 0;
	while (i < m_numSurplusKinds && m_surplusKind[i] != (Int)tmpl->getTemplateID())
		++i;
	if (i == m_numSurplusKinds)
	{
		if (m_numSurplusKinds >= MAX_SURPLUS_KINDS)
			return;
		m_surplusKind[i] = (Int)tmpl->getTemplateID();
		m_surplusKindCount[i] = 0;
		++m_numSurplusKinds;
	}
	++m_surplusKindCount[i];
}

/// Coverage: for each kind of enemy (infantry, vehicles, aircraft, structures), its share of the enemy value seen, less the share
/// of our army that can damage the strongest of that kind seen (*threat).  gap[c] > 0: the army lacks units against that kind.
void AIStrategy::coverageGaps( Real *gap, const AICombatFigures **threat ) const
{
	Real enemy[NUM_THREATS], cover[NUM_THREATS], top[NUM_THREATS];
	for (Int c = 0; c < NUM_THREATS; ++c)
	{
		gap[c] = enemy[c] = cover[c] = top[c] = 0.0f;
		threat[c] = nullptr;
	}
	for (Int e = 0; e < m_enemy.numComposition(); ++e)
	{
		const AIComposition &comp = m_enemy.composition()[e];
		const AICombatFigures *f = AICombatModel::figures(TheThingFactory->findByTemplateID(comp.m_templateID));
		if (f == nullptr)
			continue;
		const Int c = f->m_structure ? THREAT_STRUCTURE : (f->m_airborne ? THREAT_AIR : (f->m_infantry ? THREAT_INFANTRY : THREAT_VEHICLE));
		enemy[c] += f->m_structure ? 0.5f * comp.m_value : comp.m_value;	// buildings count half: the army comes first
		if (threat[c] == nullptr || f->m_cost > top[c])
		{
			threat[c] = f;
			top[c] = f->m_cost;
		}
	}
	Real enemyAll = 0.0f, ours = 0.0f;
	for (Int c = 0; c < NUM_THREATS; ++c)
		enemyAll += enemy[c];
	if (enemyAll <= 0.0f)
		return;
	for (Int t = 0; t < m_numTeams; ++t)
	{
		Team *team = TheTeamFactory->findTeamByID(m_teams[t].m_team);
		if (team == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance())
		{
			const AICombatFigures *f = it.cur() && !it.cur()->isEffectivelyDead() ? AICombatModel::figures(it.cur()->getTemplate()) : nullptr;
			if (f == nullptr || !f->m_armed)
				continue;
			ours += f->m_cost;
			for (Int c = 0; c < NUM_THREATS; ++c)
				if (threat[c] && AICombatModel::damagePerSecond(f, threat[c]) > 0.0f)
					cover[c] += f->m_cost;
		}
	}
	for (Int c = 0; c < NUM_THREATS; ++c)
	{
		const Real need = enemy[c] / enemyAll - (ours > 0.0f ? cover[c] / ours : 0.0f);
		gap[c] = need > 0.0f ? need : 0.0f;
	}
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
	const Real reserve = skill().m_surplusReserve + m_expandCost + m_captureCost + pending;
	const Real infantryShare = skill().m_surplusInfantryShare;

	// Siege: units that out-range the enemy defences seen and can hurt them come first while the army has too few.
	const AICombatFigures *defence = nullptr;
	Real siegeRange = 0.0f;
	const Int siegeCount = siegeUnits(&defence, &siegeRange);
	const Bool wantSiege = defence != nullptr && siegeCount < skill().m_surplusSiegeUnits;
	Real money = (Real)m_player->getMoney()->countMoney();
	if (money < reserve)
		return;

	// What the army lacks against what the enemy fields (coverage), and how much strength counts against price.
	Real gap[NUM_THREATS];
	const AICombatFigures *threat[NUM_THREATS];
	coverageGaps(gap, threat);
	const Bool rich = money - reserve > skill().m_surplusRichMoney;
	const Real strength = rich ? 1.0f : skill().m_surplusStrength;

	// The best unit of every idle factory first; the orders follow below.
	enum { MAX_CHOICES = 16 };
	struct Choice { Object *factory; const ThingTemplate *tmpl; Real score; Real cost; Bool idle; };
	Choice choices[MAX_CHOICES];
	Int numChoices = 0;
	if (m_numSurplusOrders >= MAX_SURPLUS_ORDERS || m_numSurplusUnits + m_numSurplusOrders >= MAX_SURPLUS_UNITS)
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
				if (factory == nullptr || factory->isEffectivelyDead() || !factory->isKindOf(KINDOF_STRUCTURE) ||
						factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || factory->testStatus(OBJECT_STATUS_SOLD))
					continue;
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				if (pu == nullptr)
					continue;
				const Bool idle = pu->getProductionCount() == 0;	// a busy factory only tells what the army could get
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
					if (f == nullptr || !f->m_armed || (!f->m_canHitGround && !(f->m_canHitAir && gap[THREAT_AIR] > 0.0f)))
						continue;
					if (tmpl->isKindOf(KINDOF_INFANTRY) && (Real)(m_surplusInfantry + 1) > infantryShare * (Real)(m_surplusOrdered + 1))
						continue;
					const Real cost = (Real)tmpl->calcCostToBuild(m_player);
					if (cost <= 0.0f || money - cost < reserve)
						continue;
					if (TheBuildAssistant->canMakeUnit(factory, tmpl) != CANMAKE_OK)
						continue;
					// How well it counters what has been seen (0..2), times its strength: the price matters less the more strength counts
					// (SurplusStrength 0: per dollar, 0.5: by the square root of the cost, 1: by the cost; rich: by the cost), so the army
					// moves up to the stronger units as they unlock.  A kind that is a large part of the surplus army already counts less
					// (a mix), and a unit that answers a kind of enemy the army cannot fight yet counts more (coverage).
					Real score = 1.0f + unitCounterScore(f);
					if (strength >= 0.75f)
						score *= cost / 1000.0f;
					else if (strength >= 0.25f)
						score *= sqrtf(cost / 1000.0f);
					score /= 1.0f + 3.0f * surplusShare(tmpl);
					Real cover = 0.0f;
					for (Int c = 0; c < NUM_THREATS; ++c)
						if (gap[c] > 0.0f && threat[c] && AICombatModel::damagePerSecond(f, threat[c]) > 0.0f)
							cover += gap[c];
					score *= 1.0f + 3.0f * cover;
					if (wantSiege && f->m_groundRange >= siegeRange && AICombatModel::damagePerSecond(f, defence) > 0.0f)
						score *= 3.0f;	// the siege units the army lacks
					if (best == nullptr || score > bestScore + 0.01f || (score > bestScore - 0.01f && cost > bestCost))
					{
						best = tmpl;
						bestScore = score;
						bestCost = cost;
					}
				}
				if (best && numChoices < MAX_CHOICES)
				{
					choices[numChoices].factory = factory;
					choices[numChoices].tmpl = best;
					choices[numChoices].score = bestScore;
					choices[numChoices].cost = bestCost;
					choices[numChoices].idle = idle;
					++numChoices;
				}
			}
		}
	}

	// Across the factories: the best choices first, and a factory whose best unit is far behind the best of all waits (a barracks does
	// not keep adding infantry while tanks are worth much more; it does when infantry answers what the army lacks).
	Real top = 0.0f;
	for (Int c = 0; c < numChoices; ++c)
		if (choices[c].score > top)
			top = choices[c].score;
	for (Int pass = 0; pass < numChoices; ++pass)
	{
		Int pick = -1;
		for (Int c = 0; c < numChoices; ++c)
			if (choices[c].tmpl && (pick < 0 || choices[c].score > choices[pick].score))
				pick = c;
		if (pick < 0)
			break;
		Choice &ch = choices[pick];
		const ThingTemplate *best = ch.tmpl;
		ch.tmpl = nullptr;
		if (!ch.idle || ch.score < 0.6f * top || money - ch.cost < reserve || m_numSurplusOrders >= MAX_SURPLUS_ORDERS)
			continue;
		ProductionUpdateInterface *pu = ch.factory->getProductionUpdateInterface();
		if (!pu->queueCreateUnit(best, pu->requestUniqueUnitID()))
			continue;
		m_surplusFactory[m_numSurplusOrders] = ch.factory->getID();
		m_surplusThing[m_numSurplusOrders] = best->getTemplateID();
		m_surplusOrderFrame[m_numSurplusOrders] = now;
		++m_numSurplusOrders;
		++m_surplusOrdered;
		if (best->isKindOf(KINDOF_INFANTRY))
			++m_surplusInfantry;
		noteSurplusKind(best);
		m_surplusSpent += ch.cost;
		money -= ch.cost;
		AI_TRACE("SURPLUS: %s ordered at %s %u (score %.2f of best %.2f, cost %.0f, money left %.0f)", best->getName().str(),
			ch.factory->getTemplate()->getName().str(), ch.factory->getID(), ch.score, top, ch.cost, money);
	}
}

/// A unit came out of a factory: if it is one of the surplus orders, it waits at the rally point for a team.
void AIStrategy::onUnitProduced( Object *factory, Object *unit )
{
	// The capturer that was trained for a capture (AICapture.cpp): it is ours to send, even when a team order took it.
	if (m_captureOrderFactory == factory->getID() && m_captureOrderThing == (Int)unit->getTemplate()->getTemplateID())
	{
		if (unit->getTeam() != m_player->getDefaultTeam())
			unit->setTeam(m_player->getDefaultTeam());
		m_captureReserve = unit->getID();
		m_captureOrderFactory = INVALID_ID;
		return;
	}
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
