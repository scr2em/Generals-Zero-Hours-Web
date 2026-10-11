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

// AICapture.cpp
// The Expert computer player captures the buildings that pay money (oil derricks): they earn more than any building a faction can
// put up (with the real data a derrick pays 200 every 12 s and 1000 at the capture), and in team games nobody took them.  Neutral
// ones on explored ground, and the enemy's ones that it sees and that are not guarded (less than 300 of armed units seen around).
//   * from CaptureStartSeconds on, every three seconds: the neutral structures with an AutoDepositUpdate module on ground we have
//     explored are the targets,
//   * the capture is the special power of a command button that needs a neutral target (the infantry capture); when that button needs
//     an upgrade we do not have, the factory that offers the upgrade researches it (once, when the money allows),
//   * each target gets one capturer: the nearest infantry whose button can be used on it now (not of a team that pulls back or defends
//     the base; of a wave only when the target is near it).  It is detached from its team until the capture is done, it died, or
//     180 s are over,
//   * when we have no infantry with the button at all, a factory trains one (one every 30 s).  The money for the upgrade or the
//     capturer is kept back from the surplus production.
// Known by modules, command buttons and special power types, no names.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPlayer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Object.h"

#define AI_TRACE(...) do { if (m_trace) { printf("AISTRAT[p%d f%u] ", m_player->getPlayerIndex(), TheGameLogic->getFrame()); printf(__VA_ARGS__); printf("\n"); } } while (0)

static inline Real dist2D(const Coord3D &a, const Coord3D &b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

/// Does the template have a behavior module of this name?
static Bool hasBehavior( const ThingTemplate *tt, const char *name )
{
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int i = 0; i < modules.getCount(); ++i)
		if (modules.getNthName(i).compare(name) == 0)
			return TRUE;
	return FALSE;
}

/// The capture button in a command set: a special power that takes a neutral object as its target.
static const CommandButton *captureButton( const CommandSet *set )
{
	if (set == nullptr)
		return nullptr;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
	{
		const CommandButton *button = set->getCommandButton(i);
		if (button == nullptr || button->getCommandType() != GUI_COMMAND_SPECIAL_POWER || button->getSpecialPowerTemplate() == nullptr)
			continue;
		const SpecialPowerType type = button->getSpecialPowerTemplate()->getSpecialPowerType();
		if ((type == SPECIAL_INFANTRY_CAPTURE_BUILDING || type == SPECIAL_BLACKLOTUS_CAPTURE_BUILDING) && (button->getOptions() & NEED_TARGET_NEUTRAL_OBJECT))
			return button;
	}
	return nullptr;
}

Bool AIStrategy::isCapturer( ObjectID id ) const
{
	for (Int i = 0; i < m_numCaptures; ++i)
		if (m_captureUnit[i] == id)
			return TRUE;
	return FALSE;
}

void AIStrategy::updateCapture()
{
	const AISkillSettings &sk = skill();
	const UnsignedInt now = TheGameLogic->getFrame();
	if (!sk.m_useCapture || now < m_nextCapture || now < secondsToFrames(sk.m_captureStartSeconds) || TheControlBar == nullptr)
		return;
	m_nextCapture = now + 3 * LOGICFRAMES_PER_SECOND;
	Coord3D base;
	if (!m_ai->getBaseCenter(&base))
		return;

	// Captures under way: done (the target is ours or gone), the capturer died, or too long.
	Int out = 0;
	for (Int i = 0; i < m_numCaptures; ++i)
	{
		Object *unit = TheGameLogic->findObjectByID(m_captureUnit[i]);
		Object *target = TheGameLogic->findObjectByID(m_captureTarget[i]);
		// An ally (another computer player, or the human) may take it first: then it is no target any more.
		const Player *owner = target ? target->getControllingPlayer() : nullptr;
		const Bool allyTook = owner && owner != m_player && owner->isPlayableSide() && m_player->getRelationship(target->getTeam()) != ENEMIES;
		if (unit == nullptr || unit->isEffectivelyDead() || target == nullptr || target->isEffectivelyDead() || allyTook ||
				target->getControllingPlayer() == m_player || now - m_captureFrame[i] > 180 * LOGICFRAMES_PER_SECOND)
		{
			if (target && target->getControllingPlayer() == m_player)
			{
				++m_captured;
				AI_TRACE("CAPTURE: %s %u is ours", target->getTemplate()->getName().str(), target->getID());
			}
			else
			{
				const Bool died = unit == nullptr || unit->isEffectivelyDead();
				AI_TRACE("CAPTURE: given up (%s)", died ? "the capturer died" : (target == nullptr || target->isEffectivelyDead() ? "the target is gone" : (allyTook ? "an ally took it" : "time is up")));
				// Back to the army (it stood by the target).
				if (!died && unit->getAI() && dist2D(*unit->getPosition(), base) > 300.0f)
				{
					Coord3D home;
					if (rallyPoint(&home))
						unit->getAI()->aiMoveToPosition(&home, CMD_FROM_AI);
				}
				// A place where the capturer was killed waits: longer after every loss there.
				if (died)
				{
					Int k = 0;
					while (k < m_numCaptureBans && m_captureBan[k] != m_captureTarget[i])
						++k;
					if (k == m_numCaptureBans && m_numCaptureBans < MAX_CAPTURES)
					{
						m_captureBan[k] = m_captureTarget[i];
						m_captureBanCount[k] = 0;
						++m_numCaptureBans;
					}
					if (k < m_numCaptureBans)
					{
						++m_captureBanCount[k];
						m_captureBanUntil[k] = now + m_captureBanCount[k] * 150 * LOGICFRAMES_PER_SECOND;
					}
				}
			}
			continue;
		}
		// On its way: the capture order once the target can be captured from here (it is in sight), else closer.
		if (unit->getAI() && (unit->getAI()->isIdle() || !m_captureOrdered[i]))
		{
			const CommandButton *button = captureButton(TheControlBar->findCommandSet(unit->getCommandSetString()));
			if (button && button->isValidToUseOn(unit, target, nullptr, CMD_FROM_AI))
			{
				unit->doCommandButtonAtObject(button, target, CMD_FROM_AI);
				if (!m_captureOrdered[i])
					AI_TRACE("CAPTURE: %s %u captures %s %u", unit->getTemplate()->getName().str(), unit->getID(), target->getTemplate()->getName().str(), target->getID());
				m_captureOrdered[i] = TRUE;
			}
			else if (unit->getAI()->isIdle() || dist2D(*unit->getPosition(), *target->getPosition()) > 150.0f)
			{
				Coord3D at = *target->getPosition();
				unit->getAI()->aiMoveToPosition(&at, CMD_FROM_AI);
			}
		}
		m_captureUnit[out] = m_captureUnit[i];
		m_captureTarget[out] = m_captureTarget[i];
		m_captureFrame[out] = m_captureFrame[i];
		m_captureOrdered[out] = m_captureOrdered[i];
		++out;
	}
	m_numCaptures = out;

	// The targets: neutral money buildings on explored ground, the nearest to the base first.
	enum { MAX_TARGETS = 16 };
	Object *targets[MAX_TARGETS];
	Int numTargets = 0;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (!obj->isKindOf(KINDOF_STRUCTURE) || obj->isEffectivelyDead())
			continue;
		Player *owner = obj->getControllingPlayer();
		if (owner == m_player || !hasBehavior(obj->getTemplate(), "AutoDepositUpdate"))
			continue;
		if (owner && owner->isPlayableSide())
		{
			// An enemy's: only one we see now.
			if (m_player->getRelationship(obj->getTeam()) != ENEMIES || obj->getShroudedStatus(m_player->getPlayerIndex()) != OBJECTSHROUD_CLEAR)
				continue;
		}
		else if (obj->getShroudedStatus(m_player->getPlayerIndex()) == OBJECTSHROUD_SHROUDED)
			continue;
		// Only where the enemy has no more than a few armed units seen around it, and not where a capturer was killed lately.
		Real guard = 0.0f;
		for (Int c = 0; c < m_enemy.numContacts(); ++c)
		{
			const AIContact &ct = m_enemy.contacts()[c];
			const AICombatFigures *f = ct.m_role <= AIROLE_DEFENCE ? AICombatModel::figures(TheThingFactory->findByTemplateID(ct.m_templateID)) : nullptr;
			if (f && f->m_armed && dist2D(ct.m_pos, *obj->getPosition()) < 400.0f)
				guard += f->m_cost;
		}
		if (guard >= 300.0f)
			continue;
		Bool banned = FALSE;
		for (Int k = 0; k < m_numCaptureBans && !banned; ++k)
			banned = m_captureBan[k] == obj->getID() && now < m_captureBanUntil[k];
		if (banned)
			continue;
		Bool taken = FALSE;
		for (Int i = 0; i < m_numCaptures && !taken; ++i)
			taken = m_captureTarget[i] == obj->getID();
		if (taken)
			continue;
		Int pos = numTargets;
		while (pos > 0 && dist2D(*targets[pos - 1]->getPosition(), base) > dist2D(*obj->getPosition(), base))
			--pos;
		if (pos >= MAX_TARGETS)
			continue;
		for (Int k = (numTargets < MAX_TARGETS ? numTargets : MAX_TARGETS - 1); k > pos; --k)
			targets[k] = targets[k - 1];
		targets[pos] = obj;
		if (numTargets < MAX_TARGETS)
			++numTargets;
	}
	m_captureCost = 0.0f;
	if (numTargets == 0)
		return;

	// The capturers: infantry of the teams at home, or loose, whose capture button works on the target now.
	Int sent = 0;
	Bool anyButton = FALSE;
	const UpgradeTemplate *needed = nullptr;
	for (Int t = 0; t < numTargets && sent < 2 && m_numCaptures < MAX_CAPTURES; ++t)
	{
		Object *target = targets[t];
		Object *best = nullptr;
		const CommandButton *bestButton = nullptr;
		Real bestDist = 0.0f;
		// The reserved capturer (trained for this) goes first.
		Object *reserve = TheGameLogic->findObjectByID(m_captureReserve);
		if (reserve && !reserve->isEffectivelyDead() && !reserve->isContained() && reserve->getAI() && !isCapturer(reserve->getID()))
		{
			const CommandButton *button = captureButton(TheControlBar->findCommandSet(reserve->getCommandSetString()));
			if (button)
			{
				anyButton = TRUE;
				// (While the upgrade is researched it walks already: it arrives about when the button works.)
				if (button->getUpgradeTemplate() && !m_player->hasUpgradeComplete(button->getUpgradeTemplate()) && !m_player->hasUpgradeInProduction(button->getUpgradeTemplate()))
					needed = button->getUpgradeTemplate();
				else
				{
					best = reserve;
					bestButton = button;
					bestDist = dist2D(*reserve->getPosition(), *target->getPosition());
				}
			}
		}
		for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end() && best == nullptr; ++it)
		{
			for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
			{
				Team *team = iter.cur();
				const AITeamRecord *rec = findRecord(team->getID(), FALSE);
				if (rec && (rec->m_mode == AITEAM_RETREATING || rec->m_mode == AITEAM_DEFENDING))
					continue;	// pulling back, or defending the base: not now
				for (DLINK_ITERATOR<Object> oit = team->iterate_TeamMemberList(); !oit.done(); oit.advance())
				{
					Object *unit = oit.cur();
					if (unit == nullptr || unit->isEffectivelyDead() || !unit->isKindOf(KINDOF_INFANTRY) || unit->isContained() || unit->isDisabled() ||
							unit->getAI() == nullptr || isDetached(unit->getID()) || isBunkerMan(unit->getID()))
						continue;
					const CommandButton *button = captureButton(TheControlBar->findCommandSet(unit->getCommandSetString()));
					if (button == nullptr)
						continue;
					anyButton = TRUE;
					// A unit of a wave only takes a target that the wave passes by.
					if (rec && rec->m_inWave && dist2D(*unit->getPosition(), *target->getPosition()) > 1200.0f)
						continue;
					const Bool researching = button->getUpgradeTemplate() && !m_player->hasUpgradeComplete(button->getUpgradeTemplate()) &&
						m_player->hasUpgradeInProduction(button->getUpgradeTemplate());
					if (button->getUpgradeTemplate() && !m_player->hasUpgradeComplete(button->getUpgradeTemplate()) && !researching)
					{
						needed = button->getUpgradeTemplate();
						continue;
					}
					SpecialPowerModuleInterface *module = unit->getSpecialPowerModule(button->getSpecialPowerTemplate());
					if (!researching && (module == nullptr || !module->isReady() || !TheSpecialPowerStore->canUseSpecialPower(unit, button->getSpecialPowerTemplate())))
						continue;
					const Real d = dist2D(*unit->getPosition(), *target->getPosition());
					if (best == nullptr || d < bestDist)
					{
						best = unit;
						bestButton = button;
						bestDist = d;
					}
				}
			}
		}
		if (best == nullptr)
			continue;
		// It walks there; the capture order follows when the target is in sight (see above).
		Coord3D at = *target->getPosition();
		best->getAI()->aiMoveToPosition(&at, CMD_FROM_AI);
		(void)bestButton;
		m_captureUnit[m_numCaptures] = best->getID();
		m_captureTarget[m_numCaptures] = target->getID();
		m_captureFrame[m_numCaptures] = now;
		m_captureOrdered[m_numCaptures] = FALSE;
		++m_numCaptures;
		++m_captureOrders;
		++sent;
		AI_TRACE("CAPTURE: %s %u walks to %s %u at (%.0f,%.0f), %.0f away", best->getTemplate()->getName().str(), best->getID(), target->getTemplate()->getName().str(),
			target->getID(), target->getPosition()->x, target->getPosition()->y, bestDist);
	}
	if (sent > 0)
		return;
	// Surplus production keeps back the money for the upgrade or the capturer (updateSurplus).
	m_captureCost = needed ? (Real)needed->calcCostToBuild(m_player) : (anyButton ? 0.0f : 300.0f);

	// Nobody could go: research the upgrade the button needs, or (when we have no infantry with the button at all) train one.
	const Real money = (Real)m_player->getMoney()->countMoney();
	for (Player::PlayerTeamList::const_iterator it = m_player->getPlayerTeams()->begin(); it != m_player->getPlayerTeams()->end(); ++it)
	{
		for (DLINK_ITERATOR<Team> iter = (*it)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			for (DLINK_ITERATOR<Object> oit = iter.cur()->iterate_TeamMemberList(); !oit.done(); oit.advance())
			{
				Object *factory = oit.cur();
				if (factory == nullptr || factory->isEffectivelyDead() || !factory->isKindOf(KINDOF_STRUCTURE) || factory->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
					continue;
				ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
				const CommandSet *set = pu ? TheControlBar->findCommandSet(factory->getCommandSetString()) : nullptr;
				if (set == nullptr)
					continue;
				for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
				{
					const CommandButton *button = set->getCommandButton(i);
					if (button == nullptr)
						continue;
					if (needed && button->getCommandType() == GUI_COMMAND_PLAYER_UPGRADE && button->getUpgradeTemplate() == needed &&
							now >= m_captureUpgradeFrame + 60 * LOGICFRAMES_PER_SECOND && money - (Real)needed->calcCostToBuild(m_player) >= sk.m_surplusReserve)
					{
						if (pu->queueUpgrade(needed))
						{
							m_captureUpgradeFrame = now;
							m_captureCost = 0.0f;
							AI_TRACE("CAPTURE: %d target(s); research %s at %s %u", numTargets, needed->getUpgradeName().str(), factory->getTemplate()->getName().str(), factory->getID());
							return;
						}
					}
					if (!anyButton && button->getCommandType() == GUI_COMMAND_UNIT_BUILD && button->getThingTemplate() && pu->getProductionCount() == 0 &&
							now >= m_captureTrainFrame + 30 * LOGICFRAMES_PER_SECOND)
					{
						const ThingTemplate *tmpl = button->getThingTemplate();
						if (!tmpl->isKindOf(KINDOF_INFANTRY) || captureButton(TheControlBar->findCommandSet(tmpl->friend_getCommandSetString())) == nullptr)
							continue;
						if (money - (Real)tmpl->calcCostToBuild(m_player) < sk.m_surplusReserve || TheBuildAssistant->canMakeUnit(factory, tmpl) != CANMAKE_OK)
							continue;
						if (pu->queueCreateUnit(tmpl, pu->requestUniqueUnitID()))
						{
							m_captureTrainFrame = now;
							m_captureOrderFactory = factory->getID();
							m_captureOrderThing = (Int)tmpl->getTemplateID();
							AI_TRACE("CAPTURE: %d target(s), no capturer at home: %s ordered at %s %u", numTargets, tmpl->getName().str(), factory->getTemplate()->getName().str(), factory->getID());
							return;
						}
					}
				}
			}
		}
	}
}
