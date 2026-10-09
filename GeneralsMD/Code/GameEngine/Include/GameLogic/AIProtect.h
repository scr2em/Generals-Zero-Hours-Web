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

// AIProtect.h
// A "protect" relation: protectors (armed units) are assigned to a set of protected objects.  When an enemy
// damages a protected object the nearest suitable protectors answer, fight the attacker, and go back to where
// they stood once things are calm, they have strayed too far (the leash), or their time is up.
//
// The module does not depend on the strategic AI: the Expert computer player uses it to guard its gatherers and
// workers, and a Protect command for human players can use the same interface (assign when the command is given,
// release when the player gives the unit another order, update once per logic frame, and forward the damage
// notice).  All of it is deterministic: it reads synchronised state only and keeps its memory in plain arrays.

#pragma once

#include "Common/GameCommon.h"
#include "Common/GameType.h"

class Object;
class Xfer;

/// How a protect relation behaves.
struct AIProtectParams
{
	Real	m_leashRadius;			///< protectors never follow a fight farther than this from their home spot
	Real	m_responseRadius;		///< only protectors within this distance of the damaged object answer
	Real	m_calmSeconds;			///< a response ends this long after the protected objects were last hit and no enemy is near
	Real	m_maxSeconds;				///< longest a protector stays away
	Int		m_maxResponders;		///< protectors sent per alarm

	AIProtectParams() : m_leashRadius(450.0f), m_responseRadius(500.0f), m_calmSeconds(6.0f), m_maxSeconds(75.0f), m_maxResponders(4) {}
};

class AIProtect
{
public:
	enum { MAX_GROUPS = 4, MAX_PROTECTORS = 16, MAX_PROTECTED = 24 };
	enum { STATE_HOME = 0, STATE_RESPONDING, STATE_RETURNING };

	AIProtect();
	void reset();

	/**
	 * The protectors guard the protected objects (relation 'key': a second call with the same key replaces the
	 * first).  A protector that is away answering an alarm keeps doing so.  'home' is where the protectors return
	 * to; null: the spot where each protector stands when it is not on a response (updated at every call).
	 * Returns false when there is no room.
	 */
	Bool assign( Int key, const ObjectID *protectors, Int numProtectors, const ObjectID *protectedSet, Int numProtected,
		const AIProtectParams &params, const Coord3D *home );
	/// Ends a relation.  Protectors that are away still come home.
	void release( Int key );
	/// Ends the relations of one protector (a human player gave the unit another order).  It comes home if it is away.
	void releaseProtector( ObjectID protector );

	/// A protected object has been damaged by 'attacker' (may be invalid: unknown).  Called from the damage code; only notes it.
	void onDamaged( const Object *victim, ObjectID attacker, Real amount );

	/// Once per logic frame (cheap; the work is spread over frames).
	void update();

	/// Is the unit away from its post on a response (or on its way back), so that other logic must leave it alone?
	Bool isAway( ObjectID unit ) const;
	Bool isProtector( ObjectID unit ) const;
	Bool isProtected( ObjectID object ) const;

	Int numAlarms() const { return m_alarms; }
	Int numResponses() const { return m_responses; }
	Int numReturns() const { return m_returns; }
	Int numAway() const;
	/// Set by the owner for the trace lines of the test bench.
	void setTrace( Bool trace, Int playerIndex ) { m_trace = trace; m_playerIndex = playerIndex; }

	void xfer( Xfer *xfer );

private:
	struct Protector
	{
		ObjectID		m_unit;
		Int					m_key;							///< relation
		Int					m_state;
		Coord3D			m_home;
		ObjectID		m_target;						///< the enemy being fought (0: none yet)
		Coord3D			m_aim;							///< where the alarm was
		UnsignedInt	m_since;						///< frame the state was entered
		Bool				m_keep;							///< scratch: still assigned after the last assign()
	};
	struct Relation
	{
		Int					m_key;
		Int					m_numProtected;
		ObjectID		m_protected[MAX_PROTECTED];
		AIProtectParams	m_params;
		Bool				m_alarm;						///< damage noted, not yet answered
		ObjectID		m_attacker;
		ObjectID		m_victim;
		Coord3D			m_victimPos;
		UnsignedInt	m_lastHit;
	};

	Relation *findRelation( Int key );
	Protector *findProtector( ObjectID unit );
	void dropProtector( Protector *p );
	void answerAlarm( Relation *rel );
	void keepResponding( Protector *p, Relation *rel );
	void sendHome( Protector *p );
	Object *nearestEnemy( const Object *unit, const Coord3D &around, Real radius ) const;

	Protector		m_protectors[MAX_PROTECTORS];
	Int					m_numProtectors;
	Relation		m_relations[MAX_GROUPS];
	Int					m_numRelations;
	UnsignedInt	m_nextUpdate;
	Int					m_alarms;				///< statistics
	Int					m_responses;
	Int					m_returns;
	Bool				m_trace;
	Int					m_playerIndex;
};

/// The damage code tells the owner of a damaged object (see ActiveBody::attemptDamage); for computer players that use a protect relation.
void AIProtectNotifyDamage( Object *victim, ObjectID attacker, Real amount );
