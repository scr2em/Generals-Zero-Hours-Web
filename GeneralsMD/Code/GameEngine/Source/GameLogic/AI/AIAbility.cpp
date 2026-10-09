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

// AIAbility.cpp
// Targeted unit abilities for the Expert computer player.
//
// A unit with a special power in its command set (a command button of type SPECIAL_POWER that asks for a target)
// uses it on a good target near it.  Everything is read from the data of the unit:
//   * the buttons of the unit's command set, their options (NEED_TARGET_ENEMY_OBJECT, NEED_TARGET_ALLY_OBJECT,
//     NEED_TARGET_POS ...) and the special power they fire,
//   * whether the power is ready (its module's reload), whether the player has the science it needs,
//   * whether the target is valid for the power (CommandButton::isValidToUseOn, which asks the action manager and so
//     follows the rules of that power's type), and whether the player has seen the target.
// Targets: for an object power the most valuable enemy (or the most hurt ally) within reach; for a position power the
// place where most enemy value stands within the power's radius with little of ours.  Nothing here knows a unit or a
// power by name.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"


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

//-------------------------------------------------------------------------------------------------
Bool AIStrategy::abilityOn() const
{
	return (skill().m_useAbilities || m_ai->isFeatureForced(AIPlayer::AIF_ABILITY)) && !m_ai->isFeatureOff(AIPlayer::AIF_ABILITY);
}

/// A unit that used a power a moment ago is left alone until the power has had time to work (the unit may walk to the target first).
Bool AIStrategy::abilityRecentlyUsed( ObjectID unit ) const
{
	const UnsignedInt now = TheGameLogic->getFrame();
	for (Int i = 0; i < ABILITY_MEMORY; ++i)
		if (m_abilityUsed[i].m_unit == unit && now - m_abilityUsed[i].m_frame < 6 * LOGICFRAMES_PER_SECOND)
			return TRUE;
	return FALSE;
}

void AIStrategy::noteAbilityUsed( ObjectID unit )
{
	Int slot = 0;
	for (Int i = 0; i < ABILITY_MEMORY; ++i)
	{
		if (m_abilityUsed[i].m_unit == unit)
		{
			slot = i;
			break;
		}
		if (m_abilityUsed[i].m_frame < m_abilityUsed[slot].m_frame)
			slot = i;
	}
	m_abilityUsed[slot].m_unit = unit;
	m_abilityUsed[slot].m_frame = TheGameLogic->getFrame();
}

/**
 * The best target for an object power of 'unit', or null.  'wantEnemy', 'wantAlly' follow the button's options.
 */
Object *AIStrategy::pickAbilityTarget( Object *unit, const CommandButton *button, Bool wantEnemy, Bool wantAlly, Real *score ) const
{
	const AISkillSettings &sk = skill();
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(unit, sk.m_abilityRange, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	Object *best = nullptr;
	Real bestScore = 0.0f;
	for (Object *o = iter->first(); o; o = iter->next())
	{
		if (o == unit || o->isOffMap() || o->isKindOf(KINDOF_UNATTACKABLE) || o->isKindOf(KINDOF_PROJECTILE) || o->isKindOf(KINDOF_MINE))
			continue;
		const Relationship r = unit->getRelationship(o);
		Real s;
		const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
		if (f == nullptr)
			continue;
		if (r == ENEMIES && wantEnemy)
		{
			if (!enemyCanSee(o))
				continue;
			s = f->m_cost * (f->m_armed ? 1.0f : 0.6f) * (f->m_structure ? 0.5f : 1.0f);
		}
		else if ((r == ALLIES || o->getControllingPlayer() == m_player) && wantAlly)
		{
			// A friendly target is worth it when it is hurt.
			const Real h = healthShare(o);
			if (h >= 0.8f)
				continue;
			s = f->m_cost * (1.0f - h) * 1.5f;
		}
		else
		{
			continue;
		}
		if (s < sk.m_abilityMinValue || (best != nullptr && s <= bestScore))
			continue;
		if (!button->isValidToUseOn(unit, o, nullptr, CMD_FROM_AI))
			continue;
		best = o;
		bestScore = s;
	}
	*score = bestScore;
	return best;
}

/**
 * The best place for a position power: the center of the most enemy value that fits in the power's radius, our own
 * things counting against it.
 */
Bool AIStrategy::pickAbilityPlace( Object *unit, const CommandButton *button, Coord3D *where, Real *score ) const
{
	const AISkillSettings &sk = skill();
	Real radius = 50.0f;
	if (button->getSpecialPowerTemplate() && button->getSpecialPowerTemplate()->getRadiusCursorRadius() > radius)
		radius = button->getSpecialPowerTemplate()->getRadiusCursorRadius();

	enum { MAX_SEEN = 24 };
	Coord3D pos[MAX_SEEN];
	Real value[MAX_SEEN];
	Int n = 0;
	PartitionFilterAlive alive;
	PartitionFilter *filters[] = { &alive, nullptr };
	SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(unit, sk.m_abilityRange, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);
	Real friends = 0.0f;
	Coord3D friendPos[MAX_SEEN];
	Real friendValue[MAX_SEEN];
	Int nf = 0;
	for (Object *o = iter->first(); o; o = iter->next())
	{
		if (o->isOffMap() || o->isKindOf(KINDOF_UNATTACKABLE) || o->isKindOf(KINDOF_PROJECTILE) || o->isKindOf(KINDOF_MINE))
			continue;
		const AICombatFigures *f = AICombatModel::figures(o->getTemplate());
		if (f == nullptr)
			continue;
		const Relationship r = unit->getRelationship(o);
		if (r == ENEMIES)
		{
			if (!enemyCanSee(o) || n >= MAX_SEEN)
				continue;
			pos[n] = *o->getPosition();
			value[n] = f->m_cost * (f->m_structure ? 0.5f : 1.0f);
			++n;
		}
		else if ((r == ALLIES || o->getControllingPlayer() == m_player) && nf < MAX_SEEN)
		{
			friendPos[nf] = *o->getPosition();
			friendValue[nf] = f->m_cost;
			++nf;
		}
	}
	(void)friends;
	Real best = 0.0f;
	Bool found = FALSE;
	for (Int i = 0; i < n; ++i)
	{
		Real s = 0.0f;
		for (Int k = 0; k < n; ++k)
			if (dist2D(pos[i], pos[k]) < radius)
				s += value[k];
		for (Int k = 0; k < nf; ++k)
			if (dist2D(pos[i], friendPos[k]) < radius)
				s -= 3.0f * friendValue[k];
		if (s > best && s >= sk.m_abilityMinValue && button->isValidToUseOn(unit, nullptr, &pos[i], CMD_FROM_AI))
		{
			best = s;
			*where = pos[i];
			found = TRUE;
		}
	}
	*score = best;
	return found;
}

/// Uses the best ready targeted power of one unit, if there is a good target.
void AIStrategy::useAbilities( Object *unit )
{
	if (unit->isEffectivelyDead() || unit->isContained() || unit->isDisabled() || unit->getAI() == nullptr || isDetached(unit->getID()))
		return;
	if (abilityRecentlyUsed(unit->getID()))
		return;
	if (TheControlBar == nullptr)
		return;
	const CommandSet *set = TheControlBar->findCommandSet(unit->getCommandSetString());
	if (set == nullptr)
		return;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
	{
		const CommandButton *button = set->getCommandButton(i);
		if (button == nullptr || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER)
			continue;
		const SpecialPowerTemplate *power = button->getSpecialPowerTemplate();
		if (power == nullptr || button->getUpgradeTemplate() != nullptr)
			continue;
		const UnsignedInt options = button->getOptions();
		const Bool wantEnemy = (options & NEED_TARGET_ENEMY_OBJECT) != 0;
		const Bool wantAlly = (options & NEED_TARGET_ALLY_OBJECT) != 0;
		const Bool wantPlace = (options & NEED_TARGET_POS) != 0 && !wantEnemy && !wantAlly;
		if (!wantEnemy && !wantAlly && !wantPlace)
			continue;		// a power without a target is not handled here (deploy, stealth ...)
		SpecialPowerModuleInterface *module = unit->getSpecialPowerModule(power);
		if (module == nullptr || !module->isReady() || module->getPercentReady() < 1.0f)
			continue;
		if (!TheSpecialPowerStore->canUseSpecialPower(unit, power))
			continue;

		Real score = 0.0f;
		if (wantPlace)
		{
			Coord3D where;
			if (!pickAbilityPlace(unit, button, &where, &score))
				continue;
			++m_abilityUses;
			noteAbilityUsed(unit->getID());
			if (m_abilityUses <= 24)
				AI_TRACE("ABILITY: %s %u uses %s at (%.0f,%.0f), enemy value there %.0f", unit->getTemplate()->getName().str(), unit->getID(), button->getName().str(), where.x, where.y, score);
			unit->doCommandButtonAtPosition(button, &where, CMD_FROM_AI);
			return;
		}
		Object *target = pickAbilityTarget(unit, button, wantEnemy, wantAlly, &score);
		if (target == nullptr)
			continue;
		++m_abilityUses;
		noteAbilityUsed(unit->getID());
		if (m_abilityUses <= 24)
			AI_TRACE("ABILITY: %s %u uses %s on %s %u (worth %.0f)", unit->getTemplate()->getName().str(), unit->getID(), button->getName().str(),
				target->getTemplate()->getName().str(), target->getID(), score);
		unit->doCommandButtonAtObject(button, target, CMD_FROM_AI);
		return;
	}
}

//-------------------------------------------------------------------------------------------------
/// A few units of the field teams per call, round robin.
void AIStrategy::updateAbilities()
{
	if (!abilityOn())
		return;
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_nextAbility)
		return;
	m_nextAbility = now + 10;

	Int budget = 3;
	Int guard = m_numTeams + 4;
	while (budget > 0 && guard-- > 0 && m_numTeams > 0)
	{
		if (m_abilityTeam >= m_numTeams)
		{
			m_abilityTeam = 0;
			m_abilityUnit = 0;
		}
		Team *team = TheTeamFactory->findTeamByID(m_teams[m_abilityTeam].m_team);
		Object *unit = nullptr;
		if (team)
		{
			Int index = 0;
			for (DLINK_ITERATOR<Object> it = team->iterate_TeamMemberList(); !it.done(); it.advance(), ++index)
			{
				if (index == m_abilityUnit)
				{
					unit = it.cur();
					break;
				}
			}
		}
		if (unit == nullptr)
		{
			++m_abilityTeam;
			m_abilityUnit = 0;
			continue;
		}
		++m_abilityUnit;
		--budget;
		useAbilities(unit);
	}
}
