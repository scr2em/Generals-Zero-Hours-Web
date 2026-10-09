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

// AICombatModel.cpp
// Pair-wise combat estimates between thing templates for the strategic AI, derived from the weapon,
// armor and body data only (no template names).  Pure functions of the game data, so every client
// gets the same numbers; nothing here touches the random number generator.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <vector>

#include "Common/ThingTemplate.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSet.h"


// One slot per thing template id, allocated when the template is first asked about.
static std::vector<AICombatFigures *> s_figures;

//-------------------------------------------------------------------------------------------------
void AICombatModel::reset()
{
	for (size_t i = 0; i < s_figures.size(); ++i)
	{
		delete s_figures[i];
	}
	s_figures.clear();
}

//-------------------------------------------------------------------------------------------------
/// Max health from the body module data of the template (every body but InactiveBody derives from ActiveBody).
static Real templateMaxHealth(const ThingTemplate *tt)
{
	const ModuleInfo &modules = tt->getBehaviorModuleInfo();
	for (Int i = 0; i < modules.getCount(); ++i)
	{
		const AsciiString name = modules.getNthName(i);
		if (name.compare("ActiveBody") == 0 || name.compare("StructureBody") == 0 || name.compare("HiveStructureBody") == 0 ||
				name.compare("HighlanderBody") == 0 || name.compare("ImmortalBody") == 0 || name.compare("UndeadBody") == 0)
		{
			const ActiveBodyModuleData *data = static_cast<const ActiveBodyModuleData *>(modules.getNthData(i));
			if (data)
				return data->m_maxHealth;
		}
	}
	return 0.0f;
}

//-------------------------------------------------------------------------------------------------
const AICombatFigures *AICombatModel::figures(const ThingTemplate *tt)
{
	if (tt == nullptr)
		return nullptr;

	const size_t id = tt->getTemplateID();
	if (id >= s_figures.size())
		s_figures.resize(id + 1, nullptr);
	if (s_figures[id])
		return s_figures[id];

	AICombatFigures *f = new AICombatFigures;
	memset(f, 0, sizeof(*f));
	s_figures[id] = f;
	f->m_valid = true;

	f->m_structure = tt->isKindOf(KINDOF_STRUCTURE);
	f->m_infantry = tt->isKindOf(KINDOF_INFANTRY);
	f->m_airborne = tt->isKindOf(KINDOF_AIRCRAFT);
	f->m_maxHealth = templateMaxHealth(tt);

	// Weapons of the default weapon set.  Slots are alternatives (the unit picks the best one for the target),
	// they do not add up.
	const WeaponTemplateSet *ws = tt->findWeaponTemplateSet(WeaponSetFlags());
	Real bestRange = 0.0f;
	if (ws)
	{
		WeaponBonus noBonus;
		for (Int slot = 0; slot < WEAPONSLOT_COUNT; ++slot)
		{
			const WeaponTemplate *w = ws->getNth((WeaponSlotType)slot);
			if (w == nullptr)
				continue;

			// A weapon that heals makes its owner a support unit, not a fighter.
			if (w->getDamageType() == DAMAGE_HEALING)
			{
				if (w->getPrimaryDamage(noBonus) > 0.0f)
					f->m_supportLevel = 2;
				continue;
			}

			Real damage = w->getPrimaryDamage(noBonus);
			const Real secondary = w->getSecondaryDamage(noBonus);
			if (secondary > 0.0f)
				damage += 0.35f * secondary;	// the outer ring of the blast, on average
			if (damage <= 0.0f)
				continue;

			Real delay = w->getAverageDelayBetweenShots();
			if (delay < 1.0f)
				delay = 1.0f;
			const Int clip = w->getClipSize();
			Real cycleFrames = delay;
			Real shotsPerCycle = 1.0f;
			if (clip > 0)
			{
				cycleFrames = clip * delay + w->getClipReloadTime(noBonus);
				shotsPerCycle = (Real)clip;
			}

			if (f->m_numWeapons < AICombatFigures::MAX_WEAPONS)
			{
				AICombatFigures::Weapon &fw = f->m_weapons[f->m_numWeapons++];
				fw.m_damage = damage;
				fw.m_shotsPerSecond = shotsPerCycle * LOGICFRAMES_PER_SECOND / cycleFrames;
				fw.m_splashRadius = w->getPrimaryDamageRadius(noBonus);
				fw.m_damageType = (Int)w->getDamageType();
				fw.m_antiMask = w->getAntiMask();
				if (fw.m_antiMask & WEAPON_ANTI_AIRBORNE_VEHICLE)
					f->m_canHitAir = TRUE;
				if (fw.m_antiMask & WEAPON_ANTI_GROUND)
					f->m_canHitGround = TRUE;
			}

			const Real range = w->getUnmodifiedAttackRange();
			if (range > bestRange)
			{
				bestRange = range;
				f->m_minRange = w->getMinimumAttackRange();
				// The wait between two volleys of this weapon.
				f->m_reloadFrames = (Int)(clip > 0 ? (clip > 0 && w->getClipReloadTime(noBonus) > delay ? w->getClipReloadTime(noBonus) : delay) : delay);
			}
		}
	}
	f->m_range = bestRange;
	f->m_armed = f->m_numWeapons > 0;

	// Things whose weapons are their spawn (garrisons, drones) have no weapon of their own.  Give them a
	// nominal punch in proportion to what they cost so they are not mistaken for harmless.
	Real cost = (Real)tt->friend_getBuildCost();
	if (!f->m_armed && tt->isKindOf(KINDOF_SPAWNS_ARE_THE_WEAPONS))
	{
		f->m_armed = TRUE;
		f->m_canHitGround = TRUE;
		f->m_canHitAir = TRUE;
		f->m_numWeapons = 1;
		f->m_weapons[0].m_damage = (cost > 0.0f ? cost : 200.0f) * 0.04f;
		f->m_weapons[0].m_shotsPerSecond = 1.0f;
		f->m_weapons[0].m_splashRadius = 0.0f;
		f->m_weapons[0].m_damageType = (Int)DAMAGE_SMALL_ARMS;
		f->m_weapons[0].m_antiMask = WEAPON_ANTI_GROUND | WEAPON_ANTI_AIRBORNE_VEHICLE;
		f->m_range = 150.0f;
	}

	if (f->m_maxHealth <= 0.0f)
		f->m_maxHealth = cost > 0.0f ? cost * 0.5f : 100.0f;
	if (cost <= 0.0f)
		cost = f->m_maxHealth * 0.5f;	// neutral things: worth about what it takes to kill them
	f->m_cost = cost;

	const ArmorTemplateSet *as = tt->findArmorTemplateSet(ArmorSetFlags());
	f->m_armor = as ? as->getArmorTemplate() : nullptr;

	// Speed: the fastest locomotor of the normal set, in world units per second.
	f->m_speed = 0.0f;
	if (!f->m_structure)
	{
		const ModuleInfo &modules = tt->getBehaviorModuleInfo();
		for (Int i = 0; i < modules.getCount(); ++i)
		{
			const ModuleData *md = modules.getNthData(i);
			if (md == nullptr || !md->isAiModuleData())
				continue;
			const LocomotorTemplateVector *set = static_cast<const AIUpdateModuleData *>(md)->findLocomotorTemplateVector(LOCOMOTORSET_NORMAL);
			if (set == nullptr)
				continue;
			for (size_t k = 0; k < set->size(); ++k)
			{
				const Real speed = (*set)[k]->getMaxSpeed() * LOGICFRAMES_PER_SECOND;
				if (speed > f->m_speed)
					f->m_speed = speed;
			}
		}
	}

	// Support: healing and repairing units (by their modules), and workers that build and repair structures.
	if (!f->m_structure)
	{
		const ModuleInfo &mods = tt->getBehaviorModuleInfo();
		for (Int i = 0; i < mods.getCount(); ++i)
		{
			const AsciiString name = mods.getNthName(i);
			if (strstr(name.str(), "HealContain") != nullptr || strstr(name.str(), "RepairDock") != nullptr)
				f->m_supportLevel = 2;
		}
		if (f->m_supportLevel < 1 && tt->isKindOf(KINDOF_DOZER))
			f->m_supportLevel = 1;
	}

	// Role.
	AIRole role = AIROLE_STRUCTURE;
	if (f->m_structure)
	{
		if (f->m_armed)
		{
			// Anti-air only defences are worth nothing against a ground army, shoot-everything defences count as defence.
			role = (f->m_canHitAir && !f->m_canHitGround) ? AIROLE_AIRDEFENCE : AIROLE_DEFENCE;
		}
		else if (tt->isKindOf(KINDOF_COMMANDCENTER) || tt->isKindOf(KINDOF_FS_FACTORY) || tt->isKindOf(KINDOF_FS_BARRACKS) ||
						 tt->isKindOf(KINDOF_FS_WARFACTORY) || tt->isKindOf(KINDOF_FS_AIRFIELD))
		{
			role = AIROLE_PRODUCTION;
		}
		else if (tt->isKindOf(KINDOF_FS_SUPPLY_CENTER) || tt->isKindOf(KINDOF_CASH_GENERATOR))
		{
			role = AIROLE_ECONOMY;
		}
		if (tt->isKindOf(KINDOF_COMMANDCENTER) || tt->isKindOf(KINDOF_FS_FACTORY))
			role = AIROLE_PRODUCTION;	// they keep their rank even when they have a weapon
	}
	else if (tt->isKindOf(KINDOF_HARVESTER) || tt->isKindOf(KINDOF_DOZER))
	{
		role = AIROLE_ECONOMY;
	}
	else if (f->m_airborne)
	{
		role = AIROLE_AIRCRAFT;
	}
	else if (f->m_infantry)
	{
		role = AIROLE_INFANTRY;
	}
	else if (tt->isKindOf(KINDOF_VEHICLE))
	{
		role = AIROLE_VEHICLE;
	}
	else
	{
		role = AIROLE_STRUCTURE;	// whatever else sits on the map
	}
	f->m_role = role;

	return f;
}

//-------------------------------------------------------------------------------------------------
Real AICombatModel::damagePerSecond(const AICombatFigures *a, const AICombatFigures *d)
{
	if (a == nullptr || d == nullptr)
		return 0.0f;

	Real best = 0.0f;
	for (Int i = 0; i < a->m_numWeapons; ++i)
	{
		const AICombatFigures::Weapon &w = a->m_weapons[i];

		// Can this weapon hit the target at all?
		if (d->m_airborne)
		{
			if ((w.m_antiMask & WEAPON_ANTI_AIRBORNE_VEHICLE) == 0)
				continue;
		}
		else if ((w.m_antiMask & WEAPON_ANTI_GROUND) == 0)
		{
			continue;
		}

		Real damage = w.m_damage;
		if (d->m_armor)
			damage = d->m_armor->adjustDamage((DamageType)w.m_damageType, damage);

		// Splash hurts more when it can catch several targets: infantry stand close together.
		if (w.m_splashRadius > 0.0f)
		{
			const Real r = w.m_splashRadius;
			damage *= d->m_infantry ? 1.0f + (r > 20.0f ? 0.6f : r * 0.03f) : 1.0f + (r > 20.0f ? 0.2f : r * 0.01f);
		}

		const Real dps = damage * w.m_shotsPerSecond;
		if (dps > best)
			best = dps;
	}
	return best;
}

//-------------------------------------------------------------------------------------------------
Real AICombatModel::killRate(const AICombatFigures *a, const AICombatFigures *d)
{
	if (a == nullptr || d == nullptr || d->m_maxHealth <= 0.0f)
		return 0.0f;
	return damagePerSecond(a, d) / d->m_maxHealth;
}

//-------------------------------------------------------------------------------------------------
Real AICombatModel::matchup(const AICombatFigures *a, const AICombatFigures *d)
{
	if (a == nullptr || d == nullptr)
		return 0.0f;

	const Real ka = killRate(a, d);
	const Real kd = killRate(d, a);
	if (ka <= 0.0f && kd <= 0.0f)
		return 0.0f;

	// The target cannot fight back (a building, a worker): what counts is how fast and cheaply it comes down,
	// not an exchange.  Value destroyed per minute per value spent, squashed into [0, 1).
	if (kd <= 0.0f)
	{
		const Real x = ka * 60.0f * d->m_cost / a->m_cost;
		return x / (1.0f + x);
	}

	// Value destroyed per second against value lost per second.  Only +, -, *, / and comparisons here:
	// no libm calls, so the result is bit-identical on every platform.
	Real num = ka * d->m_cost;
	Real den = kd * a->m_cost;

	// Reach: out-ranging the enemy is worth something even in this simple model.
	if (!a->m_structure && !d->m_structure && a->m_range > 0.0f && d->m_range > 0.0f)
	{
		if (a->m_range > d->m_range * 1.25f)
			num *= 1.15f;
		else if (a->m_range * 1.25f < d->m_range)
			num *= 0.87f;
	}

	Real ratio;
	if (num <= 0.0f)
		ratio = 1.0f / 16.0f;
	else if (den <= 0.0f)
		ratio = 16.0f;
	else
		ratio = num / den;
	if (ratio > 16.0f) ratio = 16.0f;
	if (ratio < 1.0f / 16.0f) ratio = 1.0f / 16.0f;

	// Squash into (-1, 1): 0 = even, +0.88 = 16 to 1 in our favour.
	return (ratio - 1.0f) / (ratio + 1.0f);
}
