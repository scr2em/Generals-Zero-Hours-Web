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

// AIEnemyModel.cpp
// What the computer player has seen of its enemies.
//
// The model is fed from the enemy objects that are visible to the player right now (not shrouded, not
// stealthed) and remembers them for a while.  It never reads an object that the player cannot see, so
// the strategic AI does not play with a map hack.  The sweep over the object list is spread over
// several frames, and the object list order is the same on every client, so the contact list is too.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"


// How long a sighting stays valid.  A structure does not walk away, a unit does.
static const UnsignedInt MOBILE_MEMORY_FRAMES = 40 * LOGICFRAMES_PER_SECOND;
static const UnsignedInt STRUCTURE_MEMORY_FRAMES = 15 * 60 * LOGICFRAMES_PER_SECOND;

//-------------------------------------------------------------------------------------------------
AIEnemyModel::AIEnemyModel()
{
	reset();
}

//-------------------------------------------------------------------------------------------------
void AIEnemyModel::reset()
{
	memset(m_contacts, 0, sizeof(m_contacts));
	memset(m_comp, 0, sizeof(m_comp));
	m_numContacts = 0;
	m_numComp = 0;
	m_scanResume = INVALID_ID;
	m_lastScanFrame = 0;
	m_scanStartFrame = 0;
	m_armedValue = 0.0f;
	m_allyValue = 0.0f;
	m_scanAlly = 0.0f;
	for (Int i = 0; i < AIROLE_COUNT; ++i)
	{
		m_roleValue[i] = 0.0f;
		m_roleCount[i] = 0;
	}
}

//-------------------------------------------------------------------------------------------------
Real AIEnemyModel::totalValue() const
{
	Real v = 0.0f;
	for (Int i = 0; i < AIROLE_COUNT; ++i)
		v += m_roleValue[i];
	return v;
}

//-------------------------------------------------------------------------------------------------
void AIEnemyModel::noteObject(Object *obj, Player *me, UnsignedInt now)
{
	if (obj->isEffectivelyDead() || obj->isOffMap() || obj->isContained())
		return;

	Player *owner = obj->getControllingPlayer();
	if (owner == nullptr || owner == me)
		return;
	if (me->getRelationship(obj->getTeam()) == ALLIES)
	{
		// The armed units of the allies (team games): their army fights on our side.
		if (obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER) || obj->isKindOf(KINDOF_PROJECTILE) ||
				obj->isKindOf(KINDOF_MINE) || obj->isKindOf(KINDOF_INERT) || obj->isKindOf(KINDOF_UNATTACKABLE))
			return;
		const AICombatFigures *af = AICombatModel::figures(obj->getTemplate());
		if (af != nullptr && af->m_armed && af->m_role <= AIROLE_AIRCRAFT)
			m_scanAlly += af->m_cost;
		return;
	}
	if (me->getRelationship(obj->getTeam()) != ENEMIES)
		return;
	// Civilian and neutral things (tech buildings nobody has captured, for one) are not an enemy to defeat: an army sent to them
	// would stand there for ever.
	if (!owner->isPlayableSide())
		return;

	// Things that are not a part of the fight.
	static const Int ignoreKinds[] = { KINDOF_UNATTACKABLE, KINDOF_INERT, KINDOF_PROP, KINDOF_SHRUBBERY, KINDOF_PROJECTILE,
		KINDOF_MINE, KINDOF_CRATE, KINDOF_NO_COLLIDE, KINDOF_BRIDGE, KINDOF_BRIDGE_TOWER, KINDOF_HULK };
	static const KindOfMaskType ignore(KindOfMaskType::kInit, ignoreKinds, ARRAY_SIZE(ignoreKinds));
	if (obj->isAnyKindOf(ignore))
		return;

	// Only what the player can see right now.
	const ObjectShroudStatus shroud = obj->getShroudedStatus(me->getPlayerIndex());
	if (shroud != OBJECTSHROUD_CLEAR && shroud != OBJECTSHROUD_PARTIAL_CLEAR)
		return;
	if (obj->testStatus(OBJECT_STATUS_STEALTHED) && !obj->testStatus(OBJECT_STATUS_DETECTED) && !obj->testStatus(OBJECT_STATUS_DISGUISED))
		return;

	const AICombatFigures *f = AICombatModel::figures(obj->getTemplate());
	if (f == nullptr)
		return;

	// Health at sight.
	UnsignedByte healthPct = 100;
	BodyModuleInterface *body = obj->getBodyModule();
	if (body && body->getMaxHealth() > 0.0f)
	{
		Real pct = 100.0f * body->getHealth() / body->getMaxHealth();
		healthPct = (UnsignedByte)(pct < 0.0f ? 0 : (pct > 100.0f ? 100 : (Int)pct));
	}

	// Find the contact, or make room for it.
	Int slot = -1;
	for (Int i = 0; i < m_numContacts; ++i)
	{
		if (m_contacts[i].m_id == obj->getID())
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
	{
		if (m_numContacts < MAX_CONTACTS)
		{
			slot = m_numContacts++;
		}
		else
		{
			// Replace the stalest contact, preferring to lose mobile units over structures.
			Int victim = 0;
			for (Int i = 1; i < m_numContacts; ++i)
			{
				const Bool iMobile = m_contacts[i].m_role < AIROLE_DEFENCE;
				const Bool vMobile = m_contacts[victim].m_role < AIROLE_DEFENCE;
				if ((iMobile && !vMobile) || (iMobile == vMobile && m_contacts[i].m_lastSeen < m_contacts[victim].m_lastSeen))
					victim = i;
			}
			slot = victim;
		}
	}

	AIContact &c = m_contacts[slot];
	c.m_id = obj->getID();
	c.m_templateID = obj->getTemplate()->getTemplateID();
	c.m_owner = (Short)owner->getPlayerIndex();
	c.m_role = (UnsignedByte)f->m_role;
	c.m_healthPct = healthPct;
	c.m_lastSeen = now;
	c.m_pos = *obj->getPosition();
}

//-------------------------------------------------------------------------------------------------
Bool AIEnemyModel::scan(Player *me, Int maxObjects)
{
	const UnsignedInt now = TheGameLogic->getFrame();

	Object *obj = nullptr;
	if (m_scanResume != INVALID_ID)
		obj = TheGameLogic->findObjectByID(m_scanResume);
	if (obj == nullptr)
	{
		// Start a new pass.  (An object that disappeared under the cursor also lands here: the pass restarts, nothing breaks.)
		obj = TheGameLogic->getFirstObject();
		m_scanStartFrame = now;
		m_scanAlly = 0.0f;
	}

	Int budget = maxObjects;
	while (obj && budget-- > 0)
	{
		noteObject(obj, me, now);
		obj = obj->getNextObject();
	}

	if (obj)
	{
		m_scanResume = obj->getID();
		return FALSE;
	}

	m_scanResume = INVALID_ID;
	m_lastScanFrame = now;
	m_allyValue = m_scanAlly;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void AIEnemyModel::refresh(Player *me, UnsignedInt now)
{
	// A contact is dropped when the player can see the place it was and the object is no longer there
	// (destroyed, captured, or driven off).  Out of sight the contact stays until it is too old: the
	// player cannot know better.
	Int out = 0;
	for (Int i = 0; i < m_numContacts; ++i)
	{
		const AIContact &c = m_contacts[i];
		Bool gone = FALSE;
		const Object *o = TheGameLogic->findObjectByID(c.m_id);
		if (o == nullptr || o->isEffectivelyDead() || o->getControllingPlayer() == me ||
				me->getRelationship(o->getTeam()) != ENEMIES)
		{
			gone = ThePartitionManager->getShroudStatusForPlayer(me->getPlayerIndex(), &c.m_pos) == CELLSHROUD_CLEAR;
		}
		if (gone)
			continue;
		if (out != i)
			m_contacts[out] = c;
		++out;
	}
	m_numContacts = out;
	rebuildSummary(now);
}

//-------------------------------------------------------------------------------------------------
void AIEnemyModel::rebuildSummary(UnsignedInt now)
{
	Int out = 0;
	for (Int i = 0; i < m_numContacts; ++i)
	{
		const AIContact &c = m_contacts[i];
		const Bool mobile = c.m_role < AIROLE_DEFENCE || c.m_role == AIROLE_ECONOMY;
		const UnsignedInt memory = mobile ? MOBILE_MEMORY_FRAMES : STRUCTURE_MEMORY_FRAMES;
		if (now - c.m_lastSeen > memory)
			continue;
		if (out != i)
			m_contacts[out] = c;
		++out;
	}
	m_numContacts = out;

	m_armedValue = 0.0f;
	m_numComp = 0;
	for (Int i = 0; i < AIROLE_COUNT; ++i)
	{
		m_roleValue[i] = 0.0f;
		m_roleCount[i] = 0;
	}

	for (Int i = 0; i < m_numContacts; ++i)
	{
		const AIContact &c = m_contacts[i];
		const ThingTemplate *tt = TheThingFactory->findByTemplateID(c.m_templateID);
		const AICombatFigures *f = AICombatModel::figures(tt);
		if (f == nullptr)
			continue;

		// A badly hurt unit is worth less, but never nothing.
		const Real health = 0.25f + 0.75f * (c.m_healthPct / 100.0f);
		const Real value = f->m_cost * health;
		m_roleValue[c.m_role] += value;
		m_roleCount[c.m_role] += 1;
		if (f->m_armed)
			m_armedValue += value;

		// The composition is what the army has to be chosen against: armed things, and the buildings that
		// have to come down.  Workers and harvesters are not worth a counter.
		if (!f->m_armed && !f->m_structure)
			continue;
		Int j = 0;
		while (j < m_numComp && m_comp[j].m_templateID != c.m_templateID)
			++j;
		if (j == m_numComp)
		{
			if (m_numComp >= MAX_COMPOSITION)
				continue;
			m_comp[j].m_templateID = c.m_templateID;
			m_comp[j].m_count = 0;
			m_comp[j].m_value = 0.0f;
			++m_numComp;
		}
		m_comp[j].m_count += 1;
		m_comp[j].m_value += value;
	}
}

//-------------------------------------------------------------------------------------------------
void AIEnemyModel::xfer(Xfer *xfer)
{
	xfer->xferInt(&m_numContacts);
	xfer->xferUser(m_contacts, sizeof(m_contacts));
	xfer->xferObjectID(&m_scanResume);
	xfer->xferUnsignedInt(&m_lastScanFrame);
	xfer->xferUnsignedInt(&m_scanStartFrame);
	if (xfer->getXferMode() == XFER_LOAD)
		rebuildSummary(TheGameLogic->getFrame());
}
