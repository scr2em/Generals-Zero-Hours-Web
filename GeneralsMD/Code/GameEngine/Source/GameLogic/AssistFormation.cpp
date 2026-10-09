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

// AssistFormation.cpp
// Formations for the player: line, column, wedge, box, loose, and "keep the shape" (the game's own formation).
//
// A formation is kept per unit (so a hotkey group that is created from units with a formation keeps it).  When a
// move order reaches AIGroup::groupMoveToPosition, prepareFormationMove() works out the slots of the formation at
// the destination, turned to face the way the group moves (or the way the player dragged), gives every unit the
// slot as its formation offset and marks the units as one formation of the game.  The game's formation movement
// then takes over: the units follow the same path shifted by their offsets, at the speed of the slowest member,
// so they arrive together and in shape.
//
// Which unit gets which slot: the roles come from the weapons, armor and body of the thing template (no names).
// Tough, short range units take the front rows, long range units the back rows, support units (healers, repairers),
// anti-air only units, unarmed units and transports the middle rows.  Within a row every unit, in object id order,
// takes the nearest free slot.  Everything is a function of the synchronised state, so every client agrees.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <algorithm>

#include "Common/AssistOptions.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIStrategy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PlayerAssist.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Pathfinder/PathfindConstants.h"

//-------------------------------------------------------------------------------------------------
static Bool slotLess( const AssistSlot &a, const AssistSlot &b )
{
	if (a.m_row != b.m_row)
		return a.m_row < b.m_row;
	const Real ax = a.m_x < 0.0f ? -a.m_x : a.m_x;
	const Real bx = b.m_x < 0.0f ? -b.m_x : b.m_x;
	if (ax != bx)
		return ax < bx;
	return a.m_x < b.m_x;
}

//-------------------------------------------------------------------------------------------------
void PlayerAssist::layoutSlots( Int type, Int count, Real spacing, Real depthSpacing, Real width, std::vector<AssistSlot> &out )
{
	out.clear();
	if (count <= 0)
		return;

	Int perRow = count;
	Bool triangle = FALSE;
	Bool stagger = FALSE;
	const Int fromWidth = width > 0.0f ? (Int)(width / spacing) + 1 : 0;

	switch (type)
	{
		case AFORM_COLUMN:
			perRow = fromWidth > 0 ? fromWidth : (count >= 20 ? 3 : 2);
			break;

		case AFORM_WEDGE:
			triangle = TRUE;
			perRow = fromWidth > 0 ? fromWidth : count;
			break;

		case AFORM_BOX:
			perRow = fromWidth > 0 ? fromWidth : (Int)(sqrtf( (Real)count ) + 0.999f);
			break;

		case AFORM_LOOSE:
			spacing *= 2.4f;
			depthSpacing *= 2.4f;
			stagger = TRUE;
			perRow = fromWidth > 0 ? (Int)(fromWidth / 2.4f) + 1 : (Int)(sqrtf( (Real)count * 1.3f ) + 0.999f);
			break;

		case AFORM_LINE:
		default:
			perRow = fromWidth > 0 ? fromWidth : (count < 12 ? count : 12);
			break;
	}
	if (perRow < 1)
		perRow = 1;
	if (perRow > count)
		perRow = count;

	Int placed = 0;
	for (Int row = 0; placed < count; ++row)
	{
		Int inRow = triangle ? row + 1 : perRow;
		if (inRow > perRow)
			inRow = perRow;
		if (inRow > count - placed)
			inRow = count - placed;

		const Real shift = (stagger && (row & 1)) ? 0.5f * spacing : 0.0f;
		for (Int i = 0; i < inRow; ++i)
		{
			AssistSlot s;
			s.m_x = ((Real)i - 0.5f * (Real)(inRow - 1)) * spacing + shift;
			s.m_depth = (Real)row * depthSpacing;
			s.m_row = row;
			out.push_back( s );
		}
		placed += inRow;
	}
	std::stable_sort( out.begin(), out.end(), slotLess );
}

//-------------------------------------------------------------------------------------------------
Int PlayerAssist::formationOf( ObjectID id ) const
{
	UnitMap::const_iterator it = m_units.find( id );
	return it == m_units.end() ? (Int)AFORM_NONE : (Int)it->second.m_formation;
}

//-------------------------------------------------------------------------------------------------
Int PlayerAssist::sharedFormation( const std::vector<ObjectID> &ids ) const
{
	Int shared = AFORM_NONE;
	Bool first = TRUE;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		const Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || obj->isKindOf( KINDOF_IMMOBILE ) || obj->getAI() == nullptr)
			continue;
		const Int f = formationOf( ids[i] );
		if (first)
		{
			shared = f;
			first = FALSE;
		}
		else if (f != shared)
			return AFORM_NONE;
	}
	return shared;
}

//-------------------------------------------------------------------------------------------------
/// Make the selected units one formation of the given type (or none).
void PlayerAssist::setFormation( AIGroup *group, Int type )
{
	if (type < AFORM_NONE || type >= AFORM_COUNT)
		return;

	const VecObjectID ids = group->getAllIDs();	// a copy: the group may change below

	// whatever the game's own formation had, it is replaced
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj && obj->getAI())
			obj->setFormationID( NO_FORMATION_ID );
	}

	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || obj->getAI() == nullptr || obj->isKindOf( KINDOF_IMMOBILE ))
			continue;
		if (type == AFORM_NONE)
			m_units.erase( ids[i] );
		else
			m_units[ids[i]].m_formation = (UnsignedByte)type;
	}

	ASSIST_DEBUG(( "ASSIST formation set type=%d units=%d", type, (int)ids.size() ));

	// "keep shape" is the game's own formation: the units keep the offsets they have now
	if (type == AFORM_KEEP)
		group->groupCreateFormation( CMD_FROM_PLAYER );
}

//-------------------------------------------------------------------------------------------------
/// The hotkey group is made of these units: they share the formation most of them have.
void PlayerAssist::onTeamCreated( const std::vector<Object *> &members )
{
	if (!m_allowed)
		return;

	Int votes[AFORM_COUNT] = { 0 };
	for (size_t i = 0; i < members.size(); ++i)
	{
		const Int f = formationOf( members[i]->getID() );
		if (f > AFORM_NONE && f < AFORM_COUNT)
			++votes[f];
	}
	Int best = AFORM_NONE;
	for (Int f = AFORM_LINE; f < AFORM_COUNT; ++f)
	{
		if (votes[f] > (best == AFORM_NONE ? 0 : votes[best]))
			best = f;
	}
	if (best == AFORM_NONE || best == AFORM_KEEP)
		return;	// the game's own shape belongs to the units that had it; nothing to hand on

	for (size_t i = 0; i < members.size(); ++i)
	{
		if (members[i]->getAI() && !members[i]->isKindOf( KINDOF_IMMOBILE ))
			m_units[members[i]->getID()].m_formation = (UnsignedByte)best;
	}
}

//-------------------------------------------------------------------------------------------------
/// The moves of a formation order: a click moves the group, a drag gives the front line.
void PlayerAssist::formationMove( AIGroup *group, Int type, const Coord3D &a, const Coord3D &b, Bool attackMove )
{
	if (type > AFORM_NONE && type < AFORM_KEEP && sharedFormation( group->getAllIDs() ) != type)
		setFormation( group, type );

	Coord3D dest = a;
	m_aimValid = FALSE;

	const Real dx = b.x - a.x;
	const Real dy = b.y - a.y;
	const Real dragLength = sqrtf( dx * dx + dy * dy );
	if (dragLength > 2.0f * PATHFIND_CELL_SIZE_F && type > AFORM_NONE && type < AFORM_KEEP)
	{
		// The front line runs from a to b; the group faces away from where it is now.
		dest.x = 0.5f * (a.x + b.x);
		dest.y = 0.5f * (a.y + b.y);
		dest.z = TheTerrainLogic->getGroundHeight( dest.x, dest.y );

		Coord3D center;
		group->getCenter( &center );
		Real nx = -dy / dragLength;	// a normal of the line
		Real ny = dx / dragLength;
		if (nx * (dest.x - center.x) + ny * (dest.y - center.y) < 0.0f)
		{
			nx = -nx;
			ny = -ny;
		}
		m_aimAngle = atan2f( ny, nx );
		m_aimWidth = dragLength;
		m_aimValid = TRUE;
	}

	group->releaseWeaponLockForGroup( LOCKED_TEMPORARILY );
	if (attackMove)
		group->groupAttackMoveToPosition( &dest, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER );
	else
		group->groupMoveToPosition( &dest, false, CMD_FROM_PLAYER );

	m_aimValid = FALSE;
	noteGroupMove( group, &dest );
}

//-------------------------------------------------------------------------------------------------
/// Role of a unit in a formation, and a sort key: small keys go to the front, large ones to the back.
Int PlayerAssist::roleOf( Object *obj, Real maxRange, Real maxHealth, Real *key ) const
{
	*key = 0.5f;
	const AICombatFigures *fig = AICombatModel::figures( obj->getTemplate() );
	if (fig == nullptr || !fig->m_armed)
		return AROLE_MIDDLE;
	if (fig->m_supportLevel > 0)
		return AROLE_MIDDLE;
	if (fig->m_canHitAir && !fig->m_canHitGround)
		return AROLE_MIDDLE;
	ContainModuleInterface *contain = obj->getContain();
	if (contain && contain->getContainMax() > 0 && !contain->isGarrisonable() && !obj->isKindOf( KINDOF_STRUCTURE ))
		return AROLE_MIDDLE;

	Real health = 0.0f;
	if (obj->getBodyModule())
		health = obj->getBodyModule()->getMaxHealth();
	const Real rangeNorm = maxRange > 0.0f ? fig->m_range / maxRange : 0.0f;
	const Real healthNorm = maxHealth > 0.0f ? health / maxHealth : 0.0f;
	*key = 0.65f * rangeNorm + 0.35f * (1.0f - healthNorm);
	if (*key < 0.35f)
		return AROLE_FRONT;
	if (*key > 0.65f)
		return AROLE_BACK;
	return AROLE_MIDDLE;
}

//-------------------------------------------------------------------------------------------------
namespace
{
	struct Member
	{
		Object	*m_obj;
		ObjectID m_id;
		Int			m_role;
		Real		m_key;
		Int			m_row;
		Int			m_slot;
		Bool		m_fixedMiddle;
	};

	Bool memberIdLess( const Member &a, const Member &b ) { return a.m_id < b.m_id; }
	Bool memberKeyLess( const Member &a, const Member &b )
	{
		if (a.m_key != b.m_key)
			return a.m_key < b.m_key;
		return a.m_id < b.m_id;
	}
}

//-------------------------------------------------------------------------------------------------
Bool PlayerAssist::prepareFormationMove( AIGroup *group, const Coord3D *dest )
{
	if (!m_allowed || group == nullptr || m_units.empty())
		return FALSE;

	const VecObjectID &ids = group->getAllIDs();
	if (ids.size() < 2)
		return FALSE;

	std::vector<Member> mem;
	Int type = AFORM_NONE;
	Bool ok = TRUE;
	std::vector<Object *> touched;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Object *obj = TheGameLogic->findObjectByID( ids[i] );
		if (obj == nullptr || obj->isDisabledByType( DISABLED_HELD ) || obj->isKindOf( KINDOF_IMMOBILE ))
			continue;
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai == nullptr)
			continue;

		const Int f = formationOf( ids[i] );
		if (f != AFORM_NONE)
			touched.push_back( obj );
		if (mem.empty())
			type = f;
		else if (f != type)
			ok = FALSE;

		// planes and helicopters keep their own ways
		if (obj->isKindOf( KINDOF_AIRCRAFT ))
			ok = FALSE;

		Member m;
		m.m_obj = obj;
		m.m_id = ids[i];
		m.m_role = AROLE_MIDDLE;
		m.m_key = 0.5f;
		m.m_row = 0;
		m.m_slot = -1;
		m.m_fixedMiddle = FALSE;
		mem.push_back( m );
	}

	if (!ok || mem.size() < 2 || type <= AFORM_NONE || type >= AFORM_KEEP)
	{
		// no layout formation for this move: make sure no offsets of an earlier formation stay in force
		for (size_t i = 0; i < touched.size(); ++i)
		{
			if (formationOf( touched[i]->getID() ) != AFORM_KEEP)
				touched[i]->setFormationID( NO_FORMATION_ID );
		}
		return FALSE;
	}

	std::sort( mem.begin(), mem.end(), memberIdLess );

	Coord3D center;
	if (!group->getCenter( &center ))
		return FALSE;

	// heading
	Real angle;
	Real width = 0.0f;
	if (m_aimValid)
	{
		angle = m_aimAngle;
		width = m_aimWidth;
	}
	else
	{
		const Real dx = dest->x - center.x;
		const Real dy = dest->y - center.y;
		if (dx * dx + dy * dy > 25.0f * 25.0f)
			angle = atan2f( dy, dx );
		else
			angle = mem[0].m_obj->getOrientation();
	}

	// spacing from the size of the units
	Real radius = 0.0f;
	Real biggest = 0.0f;
	Real maxRange = 0.0f;
	Real maxHealth = 0.0f;
	for (size_t i = 0; i < mem.size(); ++i)
	{
		const Real r = mem[i].m_obj->getGeometryInfo().getBoundingCircleRadius();
		radius += r;
		if (r > biggest)
			biggest = r;
		const AICombatFigures *fig = AICombatModel::figures( mem[i].m_obj->getTemplate() );
		if (fig && fig->m_armed && fig->m_range > maxRange)
			maxRange = fig->m_range;
		if (mem[i].m_obj->getBodyModule() && mem[i].m_obj->getBodyModule()->getMaxHealth() > maxHealth)
			maxHealth = mem[i].m_obj->getBodyModule()->getMaxHealth();
	}
	radius /= (Real)mem.size();
	// between the average and the biggest unit, so that the biggest ones do not push the others
	Real spacing = (0.5f * (radius + biggest)) * 2.3f + 4.0f;
	if (spacing < 16.0f)
		spacing = 16.0f;
	if (spacing > 70.0f)
		spacing = 70.0f;

	std::vector<AssistSlot> slots;
	layoutSlots( type, (Int)mem.size(), spacing, spacing, width, slots );
	if (slots.size() != mem.size())
		return FALSE;

	Real maxDepth = 0.0f;
	for (size_t i = 0; i < slots.size(); ++i)
	{
		if (slots[i].m_depth > maxDepth)
			maxDepth = slots[i].m_depth;
	}

	// roles
	Int numMiddle = 0;
	for (size_t i = 0; i < mem.size(); ++i)
	{
		mem[i].m_role = roleOf( mem[i].m_obj, maxRange, maxHealth, &mem[i].m_key );
		const AICombatFigures *fig = AICombatModel::figures( mem[i].m_obj->getTemplate() );
		const ContainModuleInterface *contain = mem[i].m_obj->getContain();
		mem[i].m_fixedMiddle = mem[i].m_role == AROLE_MIDDLE &&
			(fig == nullptr || !fig->m_armed || fig->m_supportLevel > 0 || (fig->m_canHitAir && !fig->m_canHitGround) ||
			 (contain && contain->getContainMax() > 0));
		if (mem[i].m_fixedMiddle)
			++numMiddle;
	}

	// ranks front to back: the fixed middle units take the middle ranks, the others fill the rest in order of their key
	const Int n = (Int)mem.size();
	std::vector<Member> others;
	std::vector<Member> middles;
	for (size_t i = 0; i < mem.size(); ++i)
	{
		mem[i].m_slot = (Int)i;	// index of the member, until the slots are given out
		(mem[i].m_fixedMiddle ? middles : others).push_back( mem[i] );
	}
	std::sort( others.begin(), others.end(), memberKeyLess );

	std::vector<Int> rankMember( n, -1 );
	const Int firstMiddle = (n - numMiddle) / 2;
	for (Int i = 0; i < numMiddle; ++i)
		rankMember[firstMiddle + i] = middles[i].m_slot;
	size_t nextOther = 0;
	for (Int rank = 0; rank < n; ++rank)
	{
		if (rankMember[rank] < 0 && nextOther < others.size())
			rankMember[rank] = others[nextOther++].m_slot;
	}
	for (Int rank = 0; rank < n; ++rank)
	{
		if (rankMember[rank] >= 0)
			mem[rankMember[rank]].m_row = slots[rank].m_row;
	}
	for (size_t i = 0; i < mem.size(); ++i)
		mem[i].m_slot = -1;

	// slot positions in the world
	const Real fx = cosf( angle );
	const Real fy = sinf( angle );
	const Real lx = -fy;
	const Real ly = fx;
	std::vector<Coord2D> slotOffset( slots.size() );
	for (size_t s = 0; s < slots.size(); ++s)
	{
		const Real ahead = 0.5f * maxDepth - slots[s].m_depth;
		slotOffset[s].x = fx * ahead + lx * slots[s].m_x;
		slotOffset[s].y = fy * ahead + ly * slots[s].m_x;
	}

	// within a row, in id order, every unit takes the nearest free slot of the row
	std::vector<Bool> taken( slots.size(), FALSE );
	for (size_t i = 0; i < mem.size(); ++i)
	{
		const Coord3D *pos = mem[i].m_obj->getPosition();
		Real best = 1e30f;
		Int bestSlot = -1;
		for (size_t s = 0; s < slots.size(); ++s)
		{
			if (taken[s] || slots[s].m_row != mem[i].m_row)
				continue;
			const Real sx = dest->x + slotOffset[s].x - pos->x;
			const Real sy = dest->y + slotOffset[s].y - pos->y;
			const Real d = sx * sx + sy * sy;
			if (d < best)
			{
				best = d;
				bestSlot = (Int)s;
			}
		}
		if (bestSlot < 0)
		{
			// the rows did not add up (cannot happen); take any free slot
			for (size_t s = 0; s < slots.size(); ++s)
			{
				if (!taken[s])
				{
					bestSlot = (Int)s;
					break;
				}
			}
		}
		mem[i].m_slot = bestSlot;
		if (bestSlot >= 0)
			taken[bestSlot] = TRUE;
	}

	// hand the slots to the game's formation movement
	ASSIST_DEBUG(( "ASSIST formation move type=%d units=%d angle=%.1f width=%.0f dest=%.0f,%.0f spacing=%.0f", type, (int)mem.size(),
		angle * 180.0f / 3.14159265f, width, dest->x, dest->y, spacing ));
	const FormationID fid = TheAI->getNextFormationID();
	for (size_t i = 0; i < mem.size(); ++i)
	{
		if (mem[i].m_slot < 0)
			return FALSE;
		ASSIST_DEBUG(( "ASSIST slot id=%d %s role=%d row=%d offset=%.0f,%.0f", (int)mem[i].m_id, mem[i].m_obj->getTemplate()->getName().str(),
			mem[i].m_role, mem[i].m_row, slotOffset[mem[i].m_slot].x, slotOffset[mem[i].m_slot].y ));
		mem[i].m_obj->setFormationOffset( slotOffset[mem[i].m_slot] );
		mem[i].m_obj->setFormationID( fid );
	}
	return TRUE;
}
