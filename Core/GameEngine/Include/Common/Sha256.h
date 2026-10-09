/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: Sha256.h /////////////////////////////////////////////////////////////
// SHA-256 (FIPS 180-4), used to check the content hash of an army package.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"

class Sha256
{
public:
	Sha256() { reset(); }

	void reset();
	void update(const void *data, UnsignedInt size);
	/// Finishes the hash; returns 64 lower case hex characters. The object must be reset() before it is used again.
	AsciiString finish();

private:
	void block(const unsigned char *chunk);

	UnsignedInt m_state[8];
	unsigned char m_buffer[64];
	UnsignedInt m_bufferUsed;
	UnsignedInt m_lengthLow;   ///< total length in bytes, low 32 bits
	UnsignedInt m_lengthHigh;
};
